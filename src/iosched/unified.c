/*
 **
 **  OO_Copyright_BEGIN
 **
 **
 **  Copyright 2010, 2020 IBM Corp. All rights reserved.
 **
 **  Redistribution and use in source and binary forms, with or without
 **   modification, are permitted provided that the following conditions
 **  are met:
 **  1. Redistributions of source code must retain the above copyright
 **     notice, this list of conditions and the following disclaimer.
 **  2. Redistributions in binary form must reproduce the above copyright
 **     notice, this list of conditions and the following disclaimer in the
 **  documentation and/or other materials provided with the distribution.
 **  3. Neither the name of the copyright holder nor the names of its
 **     contributors may be used to endorse or promote products derived from
 **     this software without specific prior written permission.
 **
 **  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 **  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 **  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 **  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 **  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 **  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 **  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 **  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 **  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 **  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 **  POSSIBILITY OF SUCH DAMAGE.
 **
 **
 **  OO_Copyright_END
 **
 *************************************************************************************
 **
 ** COMPONENT NAME:  IBM Linear Tape File System
 **
 ** FILE NAME:       iosched/unified.c
 **
 ** DESCRIPTION:     Implements the Unified I/O scheduler.
 **
 ** AUTHOR:          Brian Biskeborn
 **                  IBM Almaden Research Center
 **                  bbiskebo@us.ibm.com
 **
 **                  Lucas C. Villa Real
 **                  IBM Almaden Research Center
 **                  lucasvr@us.ibm.com
 **
 **                  Atsushi Abe
 **                  IBM Tokyo Lab., Japan
 **                  piste@jp.ibm.com
 **
 *************************************************************************************
 */

#include "libltfs/ltfs.h"
#include "libltfs/tape.h"
#include "libltfs/ltfs_fsops_raw.h"
#include "libltfs/index_criteria.h"
#include "libltfs/iosched_ops.h"
#include "libltfs/arch/time_internal.h"
#include "cache_manager.h"

/**
 * Maximum number of requests targeting the Index Partition to keep before flushing
 * them to the tape, as a fraction of the total number of cache blocks in the pool.
 */
#define IP_HIGH_WATERMARK 0.6

/**
 * Each outstanding write request is in one of the following states.
 */
enum request_state {
	REQUEST_PARTIAL,  /**< Partially filled request, will not normally be written until full */
	REQUEST_DP,       /**< Full request, ready to be written to the medium */
	REQUEST_IP        /**< A request already written to the DP, waiting to be written to the IP */
};

/**
 * A read request. These are used internally by unified_read to queue up read requests and issue
 * them after releasing the dentry's iosched_lock.
 */
struct read_request {
	TAILQ_ENTRY(read_request) list; /**< Pointers for linked list of requests */
	uint64_t offset;                /**< File offset for the read request */
	char *buf;                      /**< Buffer which will receive data */
	size_t count;                   /**< Number of bytes to read */
};

/**
 * A write request.
 */
struct write_request {
	TAILQ_ENTRY(write_request) list; /**< Pointers for linked list of requests */
	uint64_t offset;                 /**< Starting file offset for this request */
	size_t count;                    /**< Current request length, always <= cache block size */
	void *write_cache;               /**< Cache block containing this request's data */
	enum request_state state;        /**< Current state of the request */
};

/**
 * Per-dentry private data structure. It records a list of outstanding write requests
 * and associated data.
 */
struct dentry_priv {
	struct dentry *dentry;   /**< Dentry associated with this request list */
	ltfs_mutex_t io_lock; /**< Lock controlling file I/O to this dentry */
	uint64_t file_size;      /**< Real file size, including outstanding write requests */

	/**
	 * Index partition write flag. This is set if the file's name and size match the volume's
	 * data placement criteria. If set, the scheduler writes this file's data to the index
	 * partition and saves the resulting extents in the alt_extentlist.
	 */
	bool write_ip;

	/**
	 * Write error code.
	 * Since writes are handled asynchronously, errors hit by the background thread need to be
	 * propagated on a future request to write, flush or close. The error code is reset once
	 * it's been propagated to the caller.
	 * The caller needs to have (a) a write lock on priv->lock or (b) a read lock on priv->lock
	 * plus a lock on dentry->iosched_lock before taking the write_error_lock.
	 * Never take any other locks while holding write_error_lock.
	 */
	int write_error;
	ltfs_mutex_t write_error_lock;

	/* Linked list membership flags and pointers.
	 * Depending on the type of requests this dentry_priv contains, it may belong to one or
	 * more lists: the working set, the data writer queue, and the index writer queue.
	 * The 'in_' counters reflect the number of requests attached to each of the corresponding
	 * lists.
	 */
	uint32_t in_working_set, in_dp_queue, in_ip_queue;
	TAILQ_ENTRY(dentry_priv) working_set;
	TAILQ_ENTRY(dentry_priv) dp_queue;
	TAILQ_ENTRY(dentry_priv) ip_queue;

	/** Pointer for alternate (IP) extent queue. If this dentry_priv has a non-empty
	 * alt_extentlist, it is placed in the ext_queue. It is removed from the ext_queue when
	 * write_ip is unset or when the alt_extentlist is pushed to libltfs. */
	TAILQ_ENTRY(dentry_priv) ext_queue;

	/** List of write requests, sorted by offset */
	TAILQ_HEAD(req_struct, write_request) requests;

	/**
	 * List of index partition extents. These will be inserted into the file's real extent
	 * list when all handles to it are closed, provided that the file still matches the
	 * data placement criteria at that time.
	 * A dentry_priv is placed in the ext_queue if and only if it has extents in this list.
	 */
	TAILQ_HEAD(ext_struct, extent_info) alt_extentlist;
};

/**
 * Main scheduler data structure. Each scheduler instance has exactly one of these.
 */
struct unified_data {
	/**
	 * Global scheduler lock. Any thread working on scheduler data structures must take this lock
	 * for read. Taking this lock for write will stop all other scheduler activity, which
	 * is useful when performing operations that could cause serious tape contention
	 * (e.g. an index partition write or a full flush).
	 */
	MultiReaderSingleWriter lock;

	/**
	 * Cache allocation lock. Take this lock before making calls into the cache manager.
	 * Note: because it is accessed by the background thread, the cache_requests variable
	 * is protected by queue_lock, NOT by cache_lock!
	 * It is okay to take the queue_lock while holding this lock. Do not take any other locks
	 * while holding this lock.
	 */
	ltfs_thread_mutex_t cache_lock;
	ltfs_thread_cond_t  cache_cond; /**< Signal this variable when a cache block becomes available */
	uint32_t cache_requests;   /**< Number of threads waiting for a cache block */
	size_t cache_size;         /**< Size of each cache block */
	size_t cache_blocks;       /**< Maximum cache block count */

	/**
	 * dentry_priv queue lock.
	 * Take this before manipulating the working_set, dp_queue and ip_queue lists
	 * or the corresponding request counters. Do not take the sched_lock or any dentry_priv lock
	 * while holding this lock.
	 */
	ltfs_thread_mutex_t queue_lock;
	ltfs_thread_cond_t  queue_cond; /**< Signal this variable when the dentry_priv lists are modified */

	/* Lists of dentry_priv structures. Note that each dentry_priv may be in more than one of
	 * these lists. */
	TAILQ_HEAD(workingset_struct, dentry_priv) working_set; /**< Files with partial requests */
	TAILQ_HEAD(writequeue_struct, dentry_priv) dp_queue;    /**< Files with full (DP) requests */
	TAILQ_HEAD(indexqueue_struct, dentry_priv) ip_queue;    /**< Files with IP requests */
	TAILQ_HEAD(extqueue_struct, dentry_priv) ext_queue;     /**< Files with dirty IP extents */

	/* Queue lengths. These variables count the number of dentry_privs in each queue, not the
	 * number of requests in each state.
	 */
	uint32_t ws_count; /**< Number of entries in the working_set */
	uint32_t dp_count; /**< Number of entries in the dp_queue */
	uint32_t ip_count; /**< Number of entries in the ip_queue */

	/* Counters for various types of requests.
	 * NOTE: these variables count write_requests, not dentry_priv structures, so they
	 * DO NOT equal the lengths of the working_set/dp_queue/ip_queue lists! */
	uint32_t ws_request_count; /**< Number of requests in REQUEST_PARTIAL state */
	uint32_t dp_request_count; /**< Number of requests in REQUEST_DP state which will NOT change to IP state */
	uint32_t ip_request_count; /**< Number of requests in REQUEST_IP state */

	ltfs_thread_t writer_thread; /**< Background writer thread ID */
	bool writer_keepalive;   /**< Used to terminate the background writer thread */
	void *pool;              /**< Handle to the cache manager */
	struct ltfs_volume *vol; /**< Each scheduler instance is associated with a single LTFS volume */

	ltfs_mutex_t proflock;
	FILE* profiler; /**< The file pointer for profiler */
};


/* Prototypes */
int  _unified_get_dentry_priv(struct dentry *d, struct dentry_priv **dentry_priv,
	struct unified_data *priv);
ltfs_thread_return _unified_writer_thread(void *iosched_handle);
void _unified_process_queue(enum request_state queue, struct unified_data *priv);
void _unified_process_index_queue(struct unified_data *priv);
void _unified_process_data_queue(enum request_state queue, struct unified_data *priv);
void _unified_free_request(struct write_request *req, struct unified_data *priv);
void _unified_update_alt_extentlist(struct extent_info *newext, struct dentry_priv *dpr,
	struct unified_data *priv);
void _unified_clear_alt_extentlist(bool save, struct dentry_priv *dpr, struct unified_data *priv);
int  _unified_update_queue_membership(bool add, bool all, enum request_state queue,
	 struct dentry_priv *dentry_priv, struct unified_data *priv);
int _unified_cache_alloc(void **cache, struct dentry *d, struct unified_data *priv);
void _unified_cache_free(void *cache, size_t count, struct unified_data *priv);
ssize_t _unified_insert_new_request(const char *buf, off_t offset, size_t count, void **cache,
	bool ip_state, struct write_request *req, struct dentry *d, struct unified_data *priv);
size_t _unified_update_request(const char *buf, off_t offset, size_t size,
	struct dentry_priv *dpr, struct write_request *req, struct unified_data *priv);
int _unified_merge_requests(struct write_request *dest, struct write_request *src,
	void **spare_cache, struct dentry_priv *dpr, struct unified_data *priv);
int _unified_flush_unlocked(struct dentry *d, struct unified_data *priv);
int _unified_flush_all(struct unified_data *priv);
void _unified_free_dentry_priv_conditional(struct dentry *d, uint32_t target_handles,
	struct unified_data *priv);
void _unified_free_dentry_priv(struct dentry *d, struct unified_data *priv);
void _unified_set_write_ip(struct dentry_priv *dpr, struct unified_data *priv);
void _unified_unset_write_ip(struct dentry_priv *dpr, struct unified_data *priv);
void _unified_handle_write_error(ssize_t write_ret, struct write_request *req,
	struct dentry_priv *dpr, struct unified_data *priv);
int _unified_get_write_error(struct dentry_priv *dpr);
int _unified_write_index_after_perm(int write_ret, struct unified_data *priv);

/**
 * Initialize an instance of the unified scheduler.
 * @param vol LTFS volume to schedule writes for.
 * @return A handle to the scheduler state for this volume, or NULL on failure.
 */
void *unified_init(struct ltfs_volume *vol)
{
	int ret;
	struct unified_data *priv;
	size_t pool_size, max_pool_size, cache_size;

	CHECK_ARG_NULL(vol, NULL);

	cache_size = vol->label->blocksize;
	pool_size = (ltfs_min_cache_size(vol) * 1024LL * 1024LL) / cache_size;
	max_pool_size = (ltfs_max_cache_size(vol) * 1024LL * 1024LL) / cache_size;

	priv = (struct unified_data *) calloc(1, sizeof(struct unified_data));
	if (! priv) {
		ltfsmsg(LTFS_ERR, 10001E, "unified_init: scheduler private data");
		return NULL;
	}

	/* Initialize cache manager */
	priv->cache_size = cache_size;
	priv->cache_blocks = max_pool_size;
	priv->pool = cache_manager_init(cache_size, pool_size, max_pool_size);
	if (! priv->pool) {
		/* Cannot initialize scheduler: failed to initialize cache manager */
		ltfsmsg(LTFS_ERR, 13005E);
		free(priv);
		return NULL;
	}

	/* Initialize mutexes and condition variables.
	 * These calls never fail on Linux, but they can fail on OS X. */
	ret = ltfs_thread_mutex_init(&priv->cache_lock);
	if (ret) {
		/* Cannot initialize scheduler: failed to initialize mutex %s (%d) */
		ltfsmsg(LTFS_ERR, 13006E, "cache_lock", ret);
		cache_manager_destroy(priv->pool);
		free(priv);
		return NULL;
	}
	ret = ltfs_thread_cond_init(&priv->cache_cond);
	if (ret) {
		/* Cannot initialize scheduler: failed to initialize condition variable %s (%d) */
		ltfsmsg(LTFS_ERR, 13007E, "cache_cond", ret);
		ltfs_thread_mutex_destroy(&priv->cache_lock);
		cache_manager_destroy(priv->pool);
		free(priv);
		return NULL;
	}
	ret = ltfs_thread_mutex_init(&priv->queue_lock);
	if (ret) {
		/* Cannot initialize scheduler: failed to initialize mutex %s (%d) */
		ltfsmsg(LTFS_ERR, 13006E, "queue_lock", ret);
		ltfs_thread_cond_destroy(&priv->cache_cond);
		ltfs_thread_mutex_destroy(&priv->cache_lock);
		cache_manager_destroy(priv->pool);
		free(priv);
		return NULL;
	}
	ret = ltfs_thread_cond_init(&priv->queue_cond);
	if (ret) {
		/* Cannot initialize scheduler: failed to initialize condition variable %s (%d) */
		ltfsmsg(LTFS_ERR, 13007E, "queue_cond", ret);
		ltfs_thread_mutex_destroy(&priv->queue_lock);
		ltfs_thread_cond_destroy(&priv->cache_cond);
		ltfs_thread_mutex_destroy(&priv->cache_lock);
		cache_manager_destroy(priv->pool);
		free(priv);
		return NULL;
	}

	ret = init_mrsw(&priv->lock);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 13006E, "lock", ret);
		ltfs_thread_cond_destroy(&priv->queue_cond);
		ltfs_thread_mutex_destroy(&priv->queue_lock);
		ltfs_thread_cond_destroy(&priv->cache_cond);
		ltfs_thread_mutex_destroy(&priv->cache_lock);
		cache_manager_destroy(priv->pool);
		free(priv);
		return NULL;
	}

	TAILQ_INIT(&priv->working_set);
	TAILQ_INIT(&priv->dp_queue);
	TAILQ_INIT(&priv->ip_queue);
	TAILQ_INIT(&priv->ext_queue);
	priv->ws_request_count = priv->dp_request_count = priv->ip_request_count = 0;
	priv->writer_keepalive = true;
	priv->vol = vol;

	ret = ltfs_thread_create(&priv->writer_thread, _unified_writer_thread, priv);
	if (ret) {
		/* Cannot initialize scheduler: failed to create thread */
		ltfsmsg(LTFS_ERR, 13008E, "queue_cond", ret);
		ltfs_thread_cond_destroy(&priv->queue_cond);
		ltfs_thread_mutex_destroy(&priv->queue_lock);
		ltfs_thread_cond_destroy(&priv->cache_cond);
		ltfs_thread_mutex_destroy(&priv->cache_lock);
		destroy_mrsw(&priv->lock);
		cache_manager_destroy(priv->pool);
		free(priv);
		return NULL;
	}

	/* Unified I/O scheduler initialized */
	ltfsmsg(LTFS_DEBUG, 13015D);
	return priv;
}

/**
 * Tear down an instance of the I/O scheduler.
 * This flushes all write requests and frees all dentry_priv structures.
 * @param iosched_handle Scheduler instance to destroy.
 * @return 0 on success or a negative value on error.
 */
int unified_destroy(void *iosched_handle)
{
	struct unified_data *priv = (struct unified_data *) iosched_handle;
	struct dentry_priv *dpr, *aux;

	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);

	/* Flush everything and wait for the writer thread */
	acquirewrite_mrsw(&priv->lock);
	ltfs_thread_mutex_lock(&priv->queue_lock);
	priv->writer_keepalive = false;
	ltfs_thread_cond_signal(&priv->queue_cond);
	ltfs_thread_mutex_unlock(&priv->queue_lock);
	releasewrite_mrsw(&priv->lock);
	ltfs_thread_join(priv->writer_thread);

	/* Push IP extents to libltfs and free remaining dentry_priv structures */
	if (! TAILQ_EMPTY(&priv->ext_queue)) {
		TAILQ_FOREACH_SAFE(dpr, &priv->ext_queue, ext_queue, aux)
			_unified_free_dentry_priv(dpr->dentry, priv);
	}

	/* Free data structures */
	ltfs_thread_cond_destroy(&priv->queue_cond);
	ltfs_thread_mutex_destroy(&priv->queue_lock);
	ltfs_thread_cond_destroy(&priv->cache_cond);
	ltfs_thread_mutex_destroy(&priv->cache_lock);
	destroy_mrsw(&priv->lock);
	cache_manager_destroy(priv->pool);

	if (priv->profiler) {
		fclose(priv->profiler);
		priv->profiler = NULL;
	}

	free(priv);

	/* Unified I/O scheduler deinitialized */
	ltfsmsg(LTFS_DEBUG, 13016D);
	return 0;
}

/**
 * Open a file.
 * We allocate the dentry_priv structure on the first write, so this is just
 * a thin wrapper for ltfs_fsraw_open().
 * @param path Path to open.
 * @param open_write true if opening the file for write.
 * @param dentry On success, contains a dentry pointer. Undefined on failure.
 * @param iosched_handle Handle to the I/O scheduler data.
 * @return 0 on success or a negative value on error.
 */
int unified_open(const char *path, bool open_write, struct dentry **dentry, void *iosched_handle)
{
	struct unified_data *priv = iosched_handle;
	int ret = 0;
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);

	ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_ENTER(REQ_IOS_OPEN));
	ret = ltfs_fsraw_open(path, open_write, dentry, ((struct unified_data *)iosched_handle)->vol);
	ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_EXIT(REQ_IOS_OPEN));
	return ret;
}

/**
 * Close a file.
 * This is equivalent to a flush followed by an ltfs_fsraw_close().
 * @param d File to close.
 * @param write_index true if this file's data should be flushed before closing.
 * @param iosched_handle Handle to the I/O scheduler data.
 * @return 0 on success or a negative value on error.
 */
int unified_close(struct dentry *d, bool flush, void *iosched_handle)
{
	int write_error, ret = 0;
	struct unified_data *priv = iosched_handle;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);
	ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_ENTER(REQ_IOS_CLOSE));

	acquireread_mrsw(&priv->lock);
	ltfs_mutex_lock(&d->iosched_lock);
	if (flush)
		ret = _unified_flush_unlocked(d, priv);
	write_error = _unified_get_write_error(d->iosched_priv);
	_unified_free_dentry_priv_conditional(d, 3, priv);
	ltfs_mutex_unlock(&d->iosched_lock);
	releaseread_mrsw(&priv->lock);

	/* No need to hold any scheduler locks when closing the file. All writes which were
	 * outstanding when the close request started have been issued. */
	ltfs_fsraw_close(d);
	ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_EXIT(REQ_IOS_CLOSE));
	return ret ? ret : write_error ? write_error : 0;
}

/**
 * Read from a file.
 * This checks the outstanding write queue for any overlapping blocks. It issues
 * ltfs_fsraw_read() requests for any parts of the requested block which are not
 * in the write queue.
 * @param d File to read.
 * @param buf Output buffer for data read from the file.
 * @param size Number of bytes to read.
 * @param offset Logical file offset to read from.
 * @param iosched_handle Handle to the I/O scheduler data.
 * @return Number of bytes read on success, or a negative value on error. An attempt to read
 *         past the end of the file will return a number smaller than size, possibly 0.
 */
ssize_t unified_read(struct dentry *d, char *buf, size_t size, off_t offset, void *iosched_handle)
{
	struct unified_data *priv = iosched_handle;
	struct dentry_priv *dpr;
	struct write_request *req;
	struct read_request *rreq, *rreq_aux;
	ssize_t ret = 0, nread;
	size_t to_read;
	bool past_eof = false, have_io_lock = false;
	char *cache_obj;
	TAILQ_HEAD(read_struct, read_request) requests;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(buf, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);

	ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_ENTER(REQ_IOS_READ));
	TAILQ_INIT(&requests);

	if (size == 0) {
		ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_EXIT(REQ_IOS_READ));
		return 0;
	}

	acquireread_mrsw(&priv->lock);
	ret = ltfs_get_volume_lock(false, priv->vol);
	if (ret < 0)
		goto out;
	releaseread_mrsw(&priv->vol->lock);

	ltfs_mutex_lock(&d->iosched_lock);
	dpr = d->iosched_priv;
	if (! dpr) {
		ltfs_mutex_unlock(&d->iosched_lock);
		ret = ltfs_fsraw_read(d, buf, size, offset, priv->vol);
		goto out;
	}

	/* If there are no outstanding requests, get data from libltfs */
	if (TAILQ_EMPTY(&dpr->requests)) {
		ltfs_mutex_lock(&dpr->io_lock);
		ltfs_mutex_unlock(&d->iosched_lock);
		ret = ltfs_fsraw_read(d, buf, size, offset, priv->vol);
		ltfs_mutex_unlock(&dpr->io_lock);
		goto out;
	}

	/* Check for cached write data, queueing up read requests for any holes in the write
	 * request queue. */
	TAILQ_FOREACH(req, &dpr->requests, list) {
		/* Need to get more bytes before looking at this request? */
		if ((uint64_t)offset < req->offset) {
			to_read = req->offset - offset;
			if (to_read > size)
				to_read = size;

			/* Queue up a tape read */
			rreq = malloc(sizeof(struct read_request));
			if (! rreq) {
				ltfsmsg(LTFS_ERR, 10001E, "unified_read: read request");
				ltfs_mutex_unlock(&d->iosched_lock);
				ret = -LTFS_NO_MEMORY;
				goto out;
			}
			rreq->offset = offset;
			rreq->buf = buf;
			rreq->count = to_read;
			TAILQ_INSERT_TAIL(&requests, rreq, list);
			buf += to_read;
			offset += to_read;
			ret += to_read;
			size -= to_read;

			/* Are we done? */
			if (size == 0)
				break;
		}

		/* Use some bytes from this request? */
		if ((uint64_t)offset < req->offset + req->count) {
			to_read = req->offset + req->count - offset;
			if (to_read > size)
				to_read = size;
			cache_obj = cache_manager_get_object_data(req->write_cache);
			memcpy(buf, &cache_obj[offset - req->offset], to_read);
			buf += to_read;
			offset += to_read;
			ret += to_read;
			size -= to_read;

			/* Are we done? */
			if (size == 0)
				break;
		}
	}

	/* Issue any queued reads down to libltfs */
	if (! TAILQ_EMPTY(&requests)) {
		ltfs_mutex_lock(&dpr->io_lock);
		ltfs_mutex_unlock(&d->iosched_lock);
		have_io_lock = true;

		TAILQ_FOREACH_SAFE(rreq, &requests, list, rreq_aux) {
			to_read = rreq->count;
			nread = 0;

			/* Read from tape */
			if (! past_eof) {
				nread = ltfs_fsraw_read(d, rreq->buf, to_read, rreq->offset, priv->vol);
				if (nread < 0) {
					ltfs_mutex_unlock(&dpr->io_lock);
					ret = nread;
					goto out;
				} else if ((size_t)nread < to_read)
					past_eof = true;
				if (to_read > (size_t)nread)
					to_read -= nread;
				else
					to_read = 0;
			}

			/* We know the requested section of the file sits before an existing outstanding
			 * write request. If libltfs didn't return rreq->count bytes, then that outstanding
			 * write is past libltfs' EOF. In that case, the file will eventually be truncated
			 * out, so fill any unused portion of this read request with zeros. */
			if (to_read > 0)
				memset(rreq->buf + nread, 0, to_read);

			free(rreq);
		}
	}

	/* The code above issues libltfs reads for parts of the file that are uncached but sit
	 * before or within the file offset range covered by the request list. Still need to
	 * handle the part of the read request that lies past the end of the request list. */
	if (size > 0) {
		if (! have_io_lock) {
			ltfs_mutex_lock(&dpr->io_lock);
			ltfs_mutex_unlock(&d->iosched_lock);
		}
		nread = ltfs_fsraw_read(d, buf, size, offset, priv->vol);
		if (nread > 0)
			ret += nread;
		else if (nread < 0)
			ret = nread;
		ltfs_mutex_unlock(&dpr->io_lock);
	} else if (have_io_lock)
		ltfs_mutex_unlock(&dpr->io_lock);
	else
		ltfs_mutex_unlock(&d->iosched_lock);

out:
	releaseread_mrsw(&priv->lock);

	ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_EXIT(REQ_IOS_READ));
	return ret;
}

/**
 * Write to a file.
 * This function inserts data in to the request list of a given dentry. It allocates a
 * dentry_priv structure if necessary. It allocates cache blocks as needed, signaling the
 * background writer thread if cache pressure occurs. It does not call ltfs_fsraw_write() directly.
 * @param d File to write.
 * @param buf Input buffer containing the data to write.
 * @param size Number of bytes to write.
 * @param offset Logical file offset where bytes should be written.
 * @param isupdatetime False if caller is Windows system.
 * @param iosched_handle Handle to the I/O scheduler data.
 * @return Number of bytes written on success, or a negative value on error. The number of
 *         bytes written on success always equals 'size'; any other nonnegative return value from
 *         this function is a bug. In the future, the scheduler write interface may change to
 *         return 0 on success.
 */
ssize_t unified_write(struct dentry *d, const char *buf, size_t size, off_t offset,
	bool isupdatetime, void *iosched_handle)
{
	ssize_t ret = 0;
	struct unified_data *priv = iosched_handle;
	struct dentry_priv *dpr;
	struct write_request *req, *aux, *prev_req;
	char *req_cache;
	size_t original_size = size;
	size_t copy_offset, copy_count;
	void *spare_cache = NULL;
	off_t last_offset;
	int did_merge = 0;
	bool checked_readonly = false;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(buf, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);

	ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_ENTER(REQ_IOS_WRITE));
	if (size == 0)
		return 0;

	acquireread_mrsw(&priv->lock);
	ret = ltfs_get_volume_lock(false, priv->vol);
	if (ret < 0) {
		releaseread_mrsw(&priv->lock);
		ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_EXIT(REQ_IOS_WRITE));
		return ret;
	}
	releaseread_mrsw(&priv->vol->lock);

write_start:
	ltfs_mutex_lock(&d->iosched_lock);

	/* Allocate a new iosched_priv structure if it doesn't exist */
	ret = _unified_get_dentry_priv(d, &dpr, priv);
	if (ret < 0) {
		/* Cannot write: failed to allocate scheduler private data (%d) */
		ltfsmsg(LTFS_ERR, 13010E, (int)ret);
		goto out;
	}

	/* Check if a write request previously queued resulted in a write error */
	ret = _unified_get_write_error(dpr);
	if (ret < 0) {
		/* Propagate the write error to the caller */
		ltfs_mutex_unlock(&d->iosched_lock);
		releaseread_mrsw(&priv->lock);
		ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_EXIT(REQ_IOS_WRITE));
		return ret;
	}

	/* Disallow writes if the medium is read-only */
	if (! checked_readonly) {
		ret = ltfs_get_tape_readonly(priv->vol);
		if (ret < 0) {
			ltfs_mutex_unlock(&d->iosched_lock);
			releaseread_mrsw(&priv->lock);
			ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_EXIT(REQ_IOS_WRITE));
			return ret;
		}
		checked_readonly = true;
	}

	/* Update data placement based on file size */
	{
		const int readonly = ltfs_get_partition_readonly(ltfs_ip_id(priv->vol), priv->vol);
		if (dpr->write_ip && ((offset + size) > index_criteria_get_max_filesize(priv->vol) ||
			                  readonly == -LTFS_NO_SPACE || readonly == -LTFS_LESS_SPACE))
			_unified_unset_write_ip(dpr, priv);
	}

do_append:

	if (TAILQ_EMPTY(&dpr->requests)) {
		req = NULL;
		last_offset = 0;
	} else {
		req = TAILQ_LAST(&dpr->requests, req_struct);
		last_offset = req->offset + req->count;
	}

	/* Special case: avoid traversing the request list if writing past the end.
	 * This makes sequential writes O(1). */
	if (offset >= last_offset) {
		/* Try to append data to an existing request buffer */
		if (req && req->count < priv->cache_size && offset == last_offset &&
			req->state != REQUEST_IP) {
			copy_count = _unified_update_request(buf, offset, size, dpr, req, priv);
			buf += copy_count;
			offset += copy_count;
			size -= copy_count;
		}

		/* Append new request(s) to the end of the queue */
		while (size > 0) {
			ret = _unified_insert_new_request(buf, offset, size, &spare_cache, false,
				NULL, d, priv);
			if (ret < 0)
				goto out;
			else if (ret == 0)
				goto write_start;

			buf += ret;
			offset += ret;
			size -= ret;
		}
		goto out;
	}

	/* Not a simple append; need to traverse the request list */
	prev_req = NULL;
	TAILQ_FOREACH_SAFE(req, &dpr->requests, list, aux) {
		/* Skip this request the new write belongs farther down the queue */
		if ((uint64_t)offset > req->offset + req->count)
			continue;

		/* Insert request(s) before the current one */
do_insert_before:
		while (size > 0 && (uint64_t)offset < req->offset) {
			ret = _unified_insert_new_request(buf, offset, size, &spare_cache, false, req, d, priv);
			if (ret < 0)
				goto out;
			else if (ret == 0)
				goto write_start;

			prev_req = TAILQ_PREV(req, req_struct, list);
			buf += ret;
			offset += ret;
			size -= ret;
		}

		/* Merge the current request into the previous one. Resolve any overlap between
		 * the requests using bytes from the previous request. */
		did_merge = _unified_merge_requests(prev_req, req, &spare_cache, dpr, priv);
		if (did_merge == 2)
			continue; /* req was freed */

		/* Handle overlaps between the new write and the current request. If the current request
		 * is targeted at the DP, update it with new bytes. If the current request is targeted
		 * at the IP, truncate or remove it. */
		if (size > 0) {
			req_cache = cache_manager_get_object_data(req->write_cache);

			if ((uint64_t)offset < req->offset) {
				/* Can this happen? */
				goto do_insert_before;
			}

			if (req->state != REQUEST_IP && ((uint64_t)offset < req->offset + req->count ||
				((uint64_t)offset == req->offset + req->count && req->count < priv->cache_size))) {
				/* Update this request with bytes from the new write */
				did_merge = true; /* Force another iteration, merge check might be needed */
				copy_count = _unified_update_request(buf, offset, size, dpr, req, priv);
				buf += copy_count;
				offset += copy_count;
				size -= copy_count;
			} else if (req->state == REQUEST_IP && (uint64_t)offset < req->offset + req->count) {
				/* Truncate, split or remove this request to avoid overlapping with the new write */
				if ((uint64_t)offset == req->offset && size >= req->count) { /* Remove */
					TAILQ_REMOVE(&dpr->requests, req, list);
					_unified_update_queue_membership(false, false, REQUEST_IP, dpr, priv);
					if (spare_cache)
						_unified_free_request(req, priv);
					else {
						spare_cache = req->write_cache;
						free(req);
					}
					continue;
				} else if ((uint64_t)offset == req->offset) {
					/* Truncate from the beginning */
					memmove(req_cache, req_cache + size, req->count - size);
					req->offset += size;
					req->count -= size;
					goto do_insert_before; /* now we have offset < req->offset */
				} else if ((uint64_t)offset + size >= req->offset + req->count) {
					/* Truncate from the end */
					req->count = offset - req->offset;
				} else {
					/* Split */
					copy_offset = (offset - req->offset) + size;
					ret = _unified_insert_new_request(req_cache + copy_offset,
						req->offset + copy_offset, req->count - copy_offset,
						&spare_cache, true, aux, d, priv);
					if (ret < 0)
						goto out;
					else if (ret == 0)
						goto write_start;
					req->count = offset - req->offset;
					req = TAILQ_NEXT(req, list);
					goto do_insert_before;
				}
			}
		}

		prev_req = req;

		if (size == 0 && ! did_merge)
			goto out;
	}

	if (size > 0)
		goto do_append;

out:
	if (ret >= 0) {
		int err = ltfs_get_volume_lock(false, priv->vol);
		/* It's undesirable to fail the write here, as we have no way to roll back the cache
		 * to its previous state. There's no harm in ignoring revalidation errors at this point. */
		if (err == 0) {
            if (isupdatetime) {
                acquirewrite_mrsw(&d->meta_lock);
                get_current_timespec(&d->modify_time);
                d->change_time = d->modify_time;
                releasewrite_mrsw(&d->meta_lock);
            }
			/* Don't set index dirty flag here. Will be set later by ltfs_fsraw_add_extent. */
			releaseread_mrsw(&priv->vol->lock);
		}
	}
	ltfs_mutex_unlock(&d->iosched_lock);
	if (spare_cache)
		_unified_cache_free(spare_cache, 0, priv);
	releaseread_mrsw(&priv->lock);
	ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_EXIT(REQ_IOS_WRITE));
	return (ret < 0) ? ret : (ssize_t)original_size;
}

/**
 * Forces pending operations to meet the tape.
 *
 * @param d dentry to flush or NULL to flush all queued operations.
 * @param closeflag true if flushing before close(), false if not. Ignored by this implementation.
 * @param iosched_handle the I/O scheduler handle.
 * @return 0 on success or a negative value on error.
 */
int unified_flush(struct dentry *d, bool closeflag, void *iosched_handle)
{
	int ret;
	struct unified_data *priv = iosched_handle;

	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);
	ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_ENTER(REQ_IOS_FLUSH));

	if (d) {
		acquirewrite_mrsw(&priv->lock);
		ltfs_mutex_lock(&d->iosched_lock);
		ret = _unified_flush_unlocked(d, priv);
		ltfs_mutex_unlock(&d->iosched_lock);
		releasewrite_mrsw(&priv->lock);
	} else
		ret = _unified_flush_all(priv);

	ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_EXIT(REQ_IOS_FLUSH));
	return ret;
}

/**
 * Truncate a file to the given length.
 * @param d Dentry to modify.
 * @param length Target file size.
 * @param iosched_handle Handle to the I/O scheduler private data.
 * @return 0 on success or a negative value on error.
 */
int unified_truncate(struct dentry *d, off_t length, void *iosched_handle)
{
	int ret;
	struct unified_data *priv = iosched_handle;
	struct dentry_priv *dpr;
	struct write_request *req, *aux;
	struct extent_info *ext, *ext_aux;
	uint64_t max_filesize;
	bool matches_name_criteria, deleted;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);
	ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_ENTER(REQ_IOS_TRUNCATE));

	/* Disallow truncate if the medium is read-only */
	ret = ltfs_get_tape_readonly(priv->vol);
	if (ret < 0) {
		ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_EXIT(REQ_IOS_TRUNCATE));
		return ret;
	}

	acquireread_mrsw(&priv->lock);
	ltfs_mutex_lock(&d->iosched_lock);

	dpr = d->iosched_priv;
	if (dpr) {
		if ((uint64_t)length < dpr->file_size) {
			if (! TAILQ_EMPTY(&dpr->requests)) {
				TAILQ_FOREACH_REVERSE_SAFE(req, &dpr->requests, req_struct, list, aux) {
					if (req->offset >= (uint64_t)length) {
						TAILQ_REMOVE(&dpr->requests, req, list);
						_unified_update_queue_membership(false, false, req->state, dpr, priv);
						_unified_free_request(req, priv);
					} else if (req->offset + req->count > (uint64_t)length)
						req->count = length - req->offset;
					else
						break;
				}
			}

			if (! TAILQ_EMPTY(&dpr->alt_extentlist)) {
				TAILQ_FOREACH_SAFE(ext, &dpr->alt_extentlist, list, ext_aux) {
					if (ext->fileoffset >= (uint64_t)length) {
						TAILQ_REMOVE(&dpr->alt_extentlist, ext, list);
						free(ext);
					} else if (ext->fileoffset + ext->bytecount > (uint64_t)length)
						ext->bytecount = (uint64_t)length - ext->fileoffset;
				}
			}
		}

		dpr->file_size = length;

		/* Recompute dpr->write_ip */
		max_filesize = index_criteria_get_max_filesize(priv->vol);
		acquireread_mrsw(&d->meta_lock);
		matches_name_criteria = d->matches_name_criteria;
		deleted = d->deleted;
		releaseread_mrsw(&d->meta_lock);

		/* Only reset write_ip if the new size is 0 (complete rewrite) to avoid interleaving
		 * DP and IP extents in a single file. */
		if (! dpr->write_ip && max_filesize > 0 && length == 0
			&& matches_name_criteria && ! deleted)
			_unified_set_write_ip(dpr, priv);
		else if (dpr->write_ip
			&& (dpr->file_size > max_filesize || ! matches_name_criteria || deleted))
			_unified_unset_write_ip(dpr, priv);

		/* Tell libltfs about the truncate request */
		ltfs_mutex_lock(&dpr->io_lock);
		ret = ltfs_fsraw_truncate(d, length, priv->vol);
		ltfs_mutex_unlock(&dpr->io_lock);
	}

	ltfs_mutex_unlock(&d->iosched_lock);
	releaseread_mrsw(&priv->lock);

	if (! dpr)
		ret = ltfs_fsraw_truncate(d, length, priv->vol);

	ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_EXIT(REQ_IOS_TRUNCATE));
	return ret;
}

/**
 * Get the file size, considering data stored in working buffers.
 *
 * @param d dentry to flush or NULL to flush all queued operations.
 * @param iosched_handle the I/O scheduler handle.
 * @return the file size.
 */
uint64_t unified_get_filesize(struct dentry *d, void *iosched_handle)
{
	struct unified_data *priv = iosched_handle;
	struct dentry_priv *dentry_priv;
	uint64_t size;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);
	ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_ENTER(REQ_IOS_GETFSIZE));

	/* Try to get the file size from the dentry_priv */
	acquireread_mrsw(&priv->lock);
	ltfs_mutex_lock(&d->iosched_lock);
	dentry_priv = (struct dentry_priv *) d->iosched_priv;
	if (dentry_priv)
		size = dentry_priv->file_size;
	ltfs_mutex_unlock(&d->iosched_lock);
	releaseread_mrsw(&priv->lock);

	/* If there was no dentry_priv, return file size as stored in the dentry structure */
	if (! dentry_priv) {
		acquireread_mrsw(&d->meta_lock);
		size = d->size;
		releaseread_mrsw(&d->meta_lock);
	}

	ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_EXIT(REQ_IOS_GETFSIZE));
	return size;
}

/**
 * Update the data placement policy for a given dentry.
 * If the dentry matches the name criteria and has the appropriate size, set its write_ip flag.
 * If its write_ip flag is set and it is the wrong size or doesn't match the name criteria,
 * unset its write_ip flag and clear out its IP requests and extents.
 * If the dentry is deleted, clear its write_ip flag.
 *
 * @param d dentry
 * @param iosched_handle the I/O scheduler handle.
 * @return 0 on success or a negative value on error.
 */
int unified_update_data_placement(struct dentry *d, void *iosched_handle)
{
	struct unified_data *priv = (struct unified_data *) iosched_handle;
	struct dentry_priv *dpr;
	uint64_t filesize, max_filesize;
	bool matches_name_criteria, deleted;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);
	ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_ENTER(REQ_IOS_UPDPLACE));

	acquireread_mrsw(&priv->lock);
	ltfs_mutex_lock(&d->iosched_lock);

	dpr = d->iosched_priv;
	if (! dpr)
		goto out;

	filesize = dpr->file_size;
	max_filesize = index_criteria_get_max_filesize(priv->vol);

	acquireread_mrsw(&d->meta_lock);
	matches_name_criteria = d->matches_name_criteria;
	deleted = d->deleted;
	releaseread_mrsw(&d->meta_lock);

	if (! dpr->write_ip && max_filesize > 0 && filesize <= max_filesize && matches_name_criteria
		&& ! deleted)
		_unified_set_write_ip(dpr, priv);
	else if (dpr->write_ip && (filesize > max_filesize || ! matches_name_criteria || deleted))
		_unified_unset_write_ip(dpr, priv);

out:
	ltfs_mutex_unlock(&d->iosched_lock);
	releaseread_mrsw(&priv->lock);

	ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_EXIT(REQ_IOS_UPDPLACE));
	return 0;
}

/**
 * Background writer thread.
 * Processes requests from the dp_queue, working_set and ip_queue. The latter is only
 * processed when the number of requests waiting in that queue exceeds the high watermark
 * specified for the index partition.
 * @param iosched_handle Handle to the I/O scheduler data.
 * @return NULL.
 */
ltfs_thread_return _unified_writer_thread(void *iosched_handle)
{
	struct unified_data *priv = (struct unified_data *) iosched_handle;

	while (true) {
		ltfs_thread_mutex_lock(&priv->queue_lock);
		ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_EXIT(REQ_IOS_IOSCHED));
		while (TAILQ_EMPTY(&priv->dp_queue) && priv->cache_requests == 0 && priv->writer_keepalive)
			ltfs_thread_cond_wait(&priv->queue_cond, &priv->queue_lock);

		ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_ENTER(REQ_IOS_IOSCHED));
		if (! priv->writer_keepalive) {
			ltfs_thread_mutex_unlock(&priv->queue_lock);
			_unified_flush_all(priv);
			_unified_process_queue(REQUEST_IP, priv);
			break;

		} else if (priv->cache_requests > 0) {
			uint32_t num_waiting = priv->cache_requests;
			uint32_t num_dp = priv->dp_request_count;
			uint32_t num_ip = priv->ip_request_count;
			ltfs_thread_mutex_unlock(&priv->queue_lock);

			if (num_dp > 2 * num_waiting)
				_unified_process_queue(REQUEST_DP, priv);
			else if (num_ip < (uint32_t)(IP_HIGH_WATERMARK * priv->cache_blocks))
				_unified_process_queue(REQUEST_PARTIAL, priv);
			else
				_unified_process_queue(REQUEST_IP, priv);

		} else {
			ltfs_thread_mutex_unlock(&priv->queue_lock);
			_unified_process_queue(REQUEST_DP, priv);
		}
	}

	ltfs_thread_exit();
	return LTFS_THREAD_RC_NULL;
}

void _unified_process_queue(enum request_state queue, struct unified_data *priv)
{
	if (! priv) {
		ltfsmsg(LTFS_ERR, 10005E, "priv", __FUNCTION__);
		return;
	}

	if (queue == REQUEST_IP)
		/* Process Index Partition queue */
		_unified_process_index_queue(priv);
	else
		/* Process Data Partition / Working Set queue */
		_unified_process_data_queue(queue, priv);
}

void _unified_process_index_queue(struct unified_data *priv)
{
	struct write_request *req, *req_aux;
	struct dentry_priv *dentry_priv, *dpr_aux;
	char partition_id;
	ssize_t ret;

	partition_id = ltfs_ip_id(priv->vol);

	acquirewrite_mrsw(&priv->lock);
	TAILQ_FOREACH_SAFE(dentry_priv, &priv->ip_queue, ip_queue, dpr_aux) {
		/* Remove dentry_priv from the IP queue, process its IP requests,
		 * then free it if the request list is empty. */
		_unified_update_queue_membership(false, true, REQUEST_IP, dentry_priv, priv);

		TAILQ_FOREACH_SAFE(req, &dentry_priv->requests, list, req_aux) {
			if (req->state == REQUEST_IP) {
				char *cache_obj = cache_manager_get_object_data(req->write_cache);
				struct extent_info *extent = calloc(1, sizeof(struct extent_info));
				if (! extent) {
					ltfsmsg(LTFS_ERR, 10001E, "_unified_process_index_queue: extent");
					_unified_handle_write_error(-ENOMEM, req, dentry_priv, priv);
					break;
				}

				ret = ltfs_fsraw_write_data(partition_id, cache_obj, req->count, 1,
					&extent->start.block, priv->vol);
				if (ret < 0) {
					/* Index partition writer: failed to write data to the tape (%d) */
					ltfsmsg(LTFS_WARN, 13013W, (int)ret);
					if (IS_WRITE_PERM(-ret)) {
						ret = tape_set_cart_volume_lock_status(priv->vol, PWE_MAM_IP);
					}
					_unified_handle_write_error(ret, req, dentry_priv, priv);
					break;
				} else {
					extent->start.partition = partition_id;
					extent->byteoffset = 0;
					extent->bytecount = req->count;
					extent->fileoffset = req->offset;
					_unified_update_alt_extentlist(extent, dentry_priv, priv);
				}

				TAILQ_REMOVE(&dentry_priv->requests, req, list);
				_unified_free_request(req, priv);
			}
		}

		_unified_free_dentry_priv_conditional(dentry_priv->dentry, 2, priv);
	}
	releasewrite_mrsw(&priv->lock);
}

void _unified_process_data_queue(enum request_state queue, struct unified_data *priv)
{
	struct workingset_struct *ws = &priv->working_set;
	struct writequeue_struct *dq = &priv->dp_queue;
	char partition_id = ltfs_dp_id(priv->vol);
	uint32_t count, i;
	ssize_t ret;

	acquireread_mrsw(&priv->lock);
	ltfs_thread_mutex_lock(&priv->queue_lock);
	count = queue == REQUEST_DP ? priv->dp_count : priv->dp_count + priv->ws_count;
	ltfs_thread_mutex_unlock(&priv->queue_lock);

	/*
	 * Process only the 'count' entries that are known to be in the queue.
	 * This is needed to guarantee a limited runtime.
	 */
	for (i=0; i<count; ++i) {
		struct dentry *dentry;
		struct dentry_priv *dentry_priv;
		struct req_struct local_req_list;
		struct write_request *req, *req_aux;

		ltfs_thread_mutex_lock(&priv->queue_lock);
		if (! TAILQ_EMPTY(dq))
			dentry_priv = TAILQ_FIRST(dq);
		else if (queue == REQUEST_PARTIAL && ! TAILQ_EMPTY(ws))
			dentry_priv = TAILQ_FIRST(ws);
		else {
			ltfs_thread_mutex_unlock(&priv->queue_lock);
			break;
		}
		dentry = dentry_priv->dentry;
		ltfs_thread_mutex_unlock(&priv->queue_lock);

		if (! dentry) {
			/* Invalid backpointer to the dentry in the dentry_priv structure */
			/* Note: this can only happen if there is a bug elsewhere. */
			ltfsmsg(LTFS_ERR, 13011E);
			continue;
		}

		ltfs_mutex_lock(&dentry->iosched_lock);
		dentry_priv = dentry->iosched_priv;
		if (! dentry_priv) {
			/* Someone else took care of this dentry */
			ltfs_mutex_unlock(&dentry->iosched_lock);
			continue;
		}

		/* Remove dentry_priv from the DP queue, also from the working set if requested */
		_unified_update_queue_membership(false, true, queue, dentry_priv, priv);
		if (queue == REQUEST_PARTIAL)
			_unified_update_queue_membership(false, true, REQUEST_DP, dentry_priv, priv);

		/* Move entries to be processed into a private list */
		TAILQ_INIT(&local_req_list);
		ltfs_mutex_lock(&dentry_priv->io_lock);
		TAILQ_FOREACH_SAFE(req, &dentry_priv->requests, list, req_aux) {
			if (req->state == REQUEST_IP) {
				_unified_merge_requests(TAILQ_PREV(req, req_struct, list), req, NULL,
					dentry_priv, priv);

			} else if (req->state == REQUEST_DP || queue == REQUEST_PARTIAL) {
				if (dentry_priv->write_ip) {
					char *cache_obj = cache_manager_get_object_data(req->write_cache);
					ret = ltfs_fsraw_write(dentry, cache_obj, req->count, req->offset,
						partition_id, false, priv->vol);
					if (ret < 0) {
						/* Data partition writer: failed to write data to the tape (%d) */
						ltfsmsg(LTFS_WARN, 13014W, (int)ret);
						(void)_unified_write_index_after_perm(ret, priv);
						_unified_handle_write_error(ret, req, dentry_priv, priv);
						break;
					} else {
						req->state = REQUEST_IP;
						_unified_update_queue_membership(true, false, REQUEST_IP,
							dentry_priv, priv);
						_unified_merge_requests(TAILQ_PREV(req, req_struct, list), req, NULL,
							dentry_priv, priv);
					}

				} else {
					TAILQ_REMOVE(&dentry_priv->requests, req, list);
					TAILQ_INSERT_TAIL(&local_req_list, req, list);
					if (queue != REQUEST_PARTIAL)
						ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_EVENT(REQ_IOS_DEQUEUE_DP));
				}
			}
		}

		ltfs_mutex_unlock(&dentry->iosched_lock);

		/* Send requests to tape */
		if (! TAILQ_EMPTY(&local_req_list)) {
			TAILQ_FOREACH_SAFE(req, &local_req_list, list, req_aux) {
				char *cache_obj = cache_manager_get_object_data(req->write_cache);
				ret = ltfs_fsraw_write(dentry, cache_obj, req->count, req->offset,
					partition_id, false, priv->vol);
				if (ret < 0) {
					/* Data partition writer: failed to write data to the tape (%d) */
					ltfsmsg(LTFS_WARN, 13014W, (int)ret);
					(void)_unified_write_index_after_perm(ret, priv);
					break;
				} else {
					TAILQ_REMOVE(&local_req_list, req, list);
					_unified_free_request(req, priv);
				}
			}

			/* If there are requests left, then a write error (ret) occurred */
			if (! TAILQ_EMPTY(&local_req_list)) {
				ltfs_mutex_unlock(&dentry_priv->io_lock);
				ltfs_mutex_lock(&dentry->iosched_lock);
				if (dentry->iosched_priv) {
					dentry_priv = dentry->iosched_priv;
					ltfs_mutex_lock(&dentry_priv->io_lock);
					_unified_handle_write_error(ret, req, dentry_priv, priv);
				} else
					dentry_priv = NULL;
				ltfs_mutex_unlock(&dentry->iosched_lock);

				TAILQ_FOREACH_SAFE(req, &local_req_list, list, req_aux) {
					TAILQ_REMOVE(&local_req_list, req, list);
					_unified_free_request(req, priv);
				}
			}
		}

		if (dentry_priv)
			ltfs_mutex_unlock(&dentry_priv->io_lock);
	}

	releaseread_mrsw(&priv->lock);
}

/**
 * Returns the dentry_priv structure for a dentry, allocating it if it does not exist.
 * Must be called with a read lock on priv->lock and with d->iosched_lock held.
 * @param d Dentry to retrieve scheduler data for. The caller must hold the d->iosched_lock mutex.
 * @param dentry_priv If alloc is false, contains the dentry_priv structure or NULL if the dentry
 *             has no dentry_priv. If alloc is true, contains the dentry_priv structure on success,
 *             but it is undefined if allocating a new dentry_priv fails.
 * @param priv Handle to the I/O scheduler data.
 * @return 0 on success or a negative value on error. This function always succeeds
 *         if alloc is false.
 */
int _unified_get_dentry_priv(struct dentry *d, struct dentry_priv **dentry_priv,
	struct unified_data *priv)
{
	int ret;
	size_t max_filesize;
	struct dentry_priv *dpr;

	if (d->iosched_priv) {
		*dentry_priv = d->iosched_priv;
		return 0;
	}

	dpr = calloc(1, sizeof(struct dentry_priv));
	if (! dpr) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return -LTFS_NO_MEMORY;
	}

	dpr->in_working_set = dpr->in_dp_queue = dpr->in_ip_queue = 0;
	dpr->dentry = d;
	TAILQ_INIT(&dpr->requests);
	TAILQ_INIT(&dpr->alt_extentlist);

	ret = ltfs_mutex_init(&dpr->io_lock);
	if (ret) {
		/* Failed to initialize mutex in scheduler private data (%d) */
		ltfsmsg(LTFS_ERR, 13009E, ret);
		free(dpr);
		return -LTFS_MUTEX_INIT;
	}
	ret = ltfs_mutex_init(&dpr->write_error_lock);
	if (ret) {
		/* Failed to initialize mutex in scheduler private data (%d) */
		ltfsmsg(LTFS_ERR, 13009E, ret);
		ltfs_mutex_destroy(&dpr->io_lock);
		free(dpr);
		return -LTFS_MUTEX_INIT;
	}

	acquireread_mrsw(&d->meta_lock);
	dpr->file_size = d->size;
	dpr->write_ip = d->matches_name_criteria;
	releaseread_mrsw(&d->meta_lock);
	max_filesize = index_criteria_get_max_filesize(priv->vol);
	if (max_filesize == 0 || dpr->file_size > max_filesize)
		dpr->write_ip = false;

	d->iosched_priv = dpr;
	ltfs_fsraw_get_dentry(d, priv->vol);
	*dentry_priv = dpr;
	return 0;
}

/**
 * Add an extent to the alternate extent list.
 * Adds the dentry_priv to the ext_queue if it isn't already there. This is needed on unmount
 * to figure out which dentries still have IP extents that need dispatching.
 * @param newext Extent to add to the list.
 * @param dpr Dentry_priv to modify.
 * @param priv I/O scheduler private data.
 */
void _unified_update_alt_extentlist(struct extent_info *newext, struct dentry_priv *dpr,
	struct unified_data *priv)
{
	bool newext_used = false, free_newext = false;
	uint64_t newext_fileoffset_end, entry_fileoffset_end, fileoffset_diff;
	uint64_t entry_blockcount, entry_byteoffset_end, entry_byteoffset_mod;
	uint64_t blocksize;
	struct extent_info *entry, *aux;

	if (TAILQ_EMPTY(&dpr->alt_extentlist)) {
		/* Add this dentry_priv to the alternate extent queue */
		ltfs_thread_mutex_lock(&priv->queue_lock);
		TAILQ_INSERT_TAIL(&priv->ext_queue, dpr, ext_queue);
		ltfs_thread_mutex_unlock(&priv->queue_lock);

		TAILQ_INSERT_TAIL(&dpr->alt_extentlist, newext, list);
		return;
	}

	blocksize = priv->vol->label->blocksize;

	newext_fileoffset_end = newext->fileoffset + newext->bytecount;
	TAILQ_FOREACH_SAFE(entry, &dpr->alt_extentlist, list, aux) {
		entry_fileoffset_end = entry->fileoffset + entry->bytecount;

		if (! newext_used && newext->fileoffset <= entry->fileoffset) {
			TAILQ_INSERT_BEFORE(entry, newext, list);
			newext_used = true;
		}

		if (entry_fileoffset_end < newext->fileoffset) {
			/* Will insert newext farther down the list */
			continue;

		} else if (entry_fileoffset_end == newext->fileoffset) {
			entry_byteoffset_end = entry->byteoffset + entry->bytecount;
			entry_blockcount = entry_byteoffset_end / blocksize;
			if (newext->byteoffset == 0 && /* NOTE: assuming all extents are in the IP */
				entry_byteoffset_end % blocksize == 0 &&
				entry->start.block + entry_blockcount == newext->start.block) {
				/* Append newext's bytes to this extent */
				entry->bytecount += newext->bytecount;
				newext_used = true;
				free_newext = true;
			}

		} else if (entry->fileoffset < newext->fileoffset) {
			if (entry_fileoffset_end <= newext_fileoffset_end) {
				/* Truncate entry from its end */
				entry->bytecount = newext->fileoffset - entry->fileoffset;

			} else {
				/* To achieve maximum compactness, entry should be split here and newext should
				 * be inserted between the two parts. Instead, to avoid allocating memory, just
				 * skip entry. newext will be inserted after entry, which maintains correctness
				 * despite the increased memory usage. */
				continue;
			}

		} else if (entry_fileoffset_end <= newext_fileoffset_end) {
			/* Delete entry */
			TAILQ_REMOVE(&dpr->alt_extentlist, entry, list);
			free(entry);

		} else if (entry->fileoffset < newext_fileoffset_end) {
			/* Truncate entry from its beginning */
			fileoffset_diff = newext_fileoffset_end - entry->fileoffset;
			entry_byteoffset_mod = fileoffset_diff + entry->byteoffset;
			entry->start.block += entry_byteoffset_mod / blocksize;
			entry->byteoffset = entry_byteoffset_mod % blocksize;
			entry->bytecount -= fileoffset_diff;
			entry->fileoffset += fileoffset_diff;

		} else {
			/* Entry lies past the end of newext. Finished! */
			break;
		}
	}

	if (! newext_used)
		TAILQ_INSERT_TAIL(&dpr->alt_extentlist, newext, list);

	if (free_newext)
		free(newext);
}

/**
 * Remove all entries from a dentry_priv's alternate extent list.
 * @param save True to push extents into libltfs, false to discard them.
 * @param dpr Dentry_priv to modify.
 * @param priv I/O scheduler private data.
 */
void _unified_clear_alt_extentlist(bool save, struct dentry_priv *dpr, struct unified_data *priv)
{
	int ret;
	struct extent_info *ext, *aux;

	if (! TAILQ_EMPTY(&dpr->alt_extentlist)) {
		if (save) {
			TAILQ_FOREACH_SAFE(ext, &dpr->alt_extentlist, list, aux) {
				TAILQ_REMOVE(&dpr->alt_extentlist, ext, list);
				ret = ltfs_fsraw_add_extent(dpr->dentry, ext, false, priv->vol);
				if (ret < 0)
					ltfsmsg(LTFS_WARN, 13021W, ret);
				free(ext);
			}
		} else {
			TAILQ_FOREACH_SAFE(ext, &dpr->alt_extentlist, list, aux) {
				TAILQ_REMOVE(&dpr->alt_extentlist, ext, list);
				free(ext);
			}
		}

		ltfs_thread_mutex_lock(&priv->queue_lock);
		TAILQ_REMOVE(&priv->ext_queue, dpr, ext_queue);
		ltfs_thread_mutex_unlock(&priv->queue_lock);
	}
}

/**
 * Update the queue membership of a given dentry_priv structure.
 * Requires the caller to have a lock held on dentry->iosched_lock.
 * Performs reference counting so that counts of write requests in each queue are known.
 * @param add True to add the dentry_priv to a queue membership, False to remove it from that queue.
 * @param all If 'add' is false and 'all' is true, set the counter for the given queue to 0
 *            and forcibly remove this dentry_priv from that queue. Ignored when 'add' is true.
 * @param queue Queue to add or remove the dentry_priv from.
 * @param dentry_priv The dentry_priv structure.
 * @param priv Handle to the I/O scheduler data.
 * @return 0 on success or a negative value on error.
 */
int _unified_update_queue_membership(bool add, bool all, enum request_state queue,
	struct dentry_priv *dentry_priv, struct unified_data *priv)
{
	int ret = 0;

	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dentry_priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dentry_priv->dentry, -LTFS_NULL_ARG);

	ltfs_thread_mutex_lock(&priv->queue_lock);
	switch (queue) {
		case REQUEST_PARTIAL:
			if (add) {
				if (! dentry_priv->in_working_set) {
					TAILQ_INSERT_TAIL(&priv->working_set, dentry_priv, working_set);
					++priv->ws_count;
				}
				++dentry_priv->in_working_set;
				++priv->ws_request_count;
			} else {
				if ((all && dentry_priv->in_working_set) || dentry_priv->in_working_set == 1) {
					TAILQ_REMOVE(&priv->working_set, dentry_priv, working_set);
					--priv->ws_count;
				}
				if (all) {
					priv->ws_request_count -= dentry_priv->in_working_set;
					dentry_priv->in_working_set = 0;
				} else if (dentry_priv->in_working_set) {
					--priv->ws_request_count;
					--dentry_priv->in_working_set;
				}
			}
			break;

		case REQUEST_DP:
			if (add) {
				if (! dentry_priv->in_dp_queue) {
					TAILQ_INSERT_TAIL(&priv->dp_queue, dentry_priv, dp_queue);
					++priv->dp_count;
					/* Tell background thread a write request is ready */
					ltfs_thread_cond_signal(&priv->queue_cond);
				}
				if (! dentry_priv->write_ip)
					++priv->dp_request_count;
				++dentry_priv->in_dp_queue;
				ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_EVENT(REQ_IOS_ENQUEUE_DP));
			} else {
				if ((all && dentry_priv->in_dp_queue) || dentry_priv->in_dp_queue == 1) {
					TAILQ_REMOVE(&priv->dp_queue, dentry_priv, dp_queue);
					--priv->dp_count;
				}
				if (all) {
					if (! dentry_priv->write_ip)
						priv->dp_request_count -= dentry_priv->in_dp_queue;
					dentry_priv->in_dp_queue = 0;
				} else if (dentry_priv->in_dp_queue) {
					if (! dentry_priv->write_ip)
						--priv->dp_request_count;
					--dentry_priv->in_dp_queue;
				}
			}
			break;

		case REQUEST_IP:
			if (add) {
				if (! dentry_priv->in_ip_queue) {
					TAILQ_INSERT_TAIL(&priv->ip_queue, dentry_priv, ip_queue);
					++priv->ip_count;
				}
				++dentry_priv->in_ip_queue;
				++priv->ip_request_count;
				ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_EVENT(REQ_IOS_ENQUEUE_IP));
			} else {
				if ((all && dentry_priv->in_ip_queue) || dentry_priv->in_ip_queue == 1) {
					TAILQ_REMOVE(&priv->ip_queue, dentry_priv, ip_queue);
					--priv->ip_count;
				}
				if (all) {
					priv->ip_request_count -= dentry_priv->in_ip_queue;
					dentry_priv->in_ip_queue = 0;
				} else if (dentry_priv->in_ip_queue) {
					--dentry_priv->in_ip_queue;
					--priv->ip_request_count;
				}
				ltfs_profiler_add_entry(priv->profiler, &priv->proflock, IOSCHED_REQ_EVENT(REQ_IOS_DEQUEUE_IP));
			}
			break;

		default:
			/* Invalid request_state received when updating the queue membership (%d) */
			ltfsmsg(LTFS_ERR, 13012E, queue);
			ret = -LTFS_BAD_ARG;
	};
	ltfs_thread_mutex_unlock(&priv->queue_lock);

	return ret;
}

/**
 * Free a write_request structure, including its write cache.
 * @param req Request structure to free.
 * @param priv Handle to I/O scheduler data.
 */
void _unified_free_request(struct write_request *req, struct unified_data *priv)
{
	if (req->write_cache)
		_unified_cache_free(req->write_cache, req->count, priv);
	free(req);
}

/**
 * Free a cache block and signal anyone waiting on cache pressure.
 * @param cache Cache manager block to free.
 * @param priv Handle to the I/O scheduler data.
 */
void _unified_cache_free(void *cache, size_t count, struct unified_data *priv)
{
	ltfs_thread_mutex_lock(&priv->cache_lock);
	cache_manager_free_object(cache, count);
	ltfs_thread_cond_signal(&priv->cache_cond);
	ltfs_thread_mutex_unlock(&priv->cache_lock);
}

/**
 * Allocate a cache block for the given dentry. This is a helper function for unified_write;
 * it may not be very useful elsewhere.
 * The caller must hold d->iosched_lock and a read lock on priv->lock.
 * This function tries to allocate a cache block without releasing any locks. If it fails (due to
 * cache pressure), it releases both locks, waits for the cache pressure to be relieved,
 * retakes priv->lock for read, and returns.
 * @param cache On exit, holds a newly allocated cache block.
 * @param d Dentry to allocate a cache block for.
 * @param priv Handle to I/O scheduler data.
 * @return 1 if dentry_priv lock was released due to cache pressure, 0 if not, or a negative value
 *         on error. Both locks (priv->lock and d->iosched_lock) are released on error.
 */
int _unified_cache_alloc(void **cache, struct dentry *d, struct unified_data *priv)
{
	ltfs_thread_mutex_lock(&priv->cache_lock);
	*cache = cache_manager_allocate_object(priv->pool);
	if (*cache) {
		ltfs_thread_mutex_unlock(&priv->cache_lock);
		return 0;
	}

	/* Cache pressure occurred. Release locks and wait for space to become free */
	ltfs_mutex_unlock(&d->iosched_lock);
	ltfs_thread_mutex_lock(&priv->queue_lock);
	ltfs_thread_cond_signal(&priv->queue_cond);
	++priv->cache_requests;
	ltfs_thread_mutex_unlock(&priv->queue_lock);
	releaseread_mrsw(&priv->lock);
	while (! (*cache)) {
		ltfs_thread_cond_wait(&priv->cache_cond, &priv->cache_lock);
		*cache = cache_manager_allocate_object(priv->pool);
	}
	ltfs_thread_mutex_unlock(&priv->cache_lock);

	acquireread_mrsw(&priv->lock);
	ltfs_thread_mutex_lock(&priv->queue_lock);
	--priv->cache_requests;
	ltfs_thread_mutex_unlock(&priv->queue_lock);
	return 1;
}

/**
 * Insert a new write request before 'req', or at the end of the request list if
 * 'req' is NULL.
 * Call this function with d->iosched_lock held and a read lock on priv->lock.
 * d->iosched_lock may be released during allocation of a new cache block, in which case
 * the function returns without actually inserting a new request into the queue.
 * This is a helper function for unified_write; it may not be useful elsewhere.
 * @param buf Data buffer to copy.
 * @param offset File offset of the buffer.
 * @param count Number of bytes in the buffer.
 * @param cache Address of a cache block. May point to NULL if no cache block is available when
 *              the function is called. On exit, it contains NULL (for a return value <= 0)
 *              or a pointer to a newly allocated cache block (for a return value of 1).
 * @param ip_state True to put the new request into state REQUEST_IP, false to put it in
 *                 REQUEST_DP or REQUEST_PARTIAL (depending on its size).
 * @param req An existing write request, or NULL to insert the new request at the end of the list.
 * @param d Dentry for which this write request is being issued.
 * @return Number of bytes copied on success, 0 if d->iosched_lock must be retaken,
 *         or a negative value on error. d->iosched_lock is released if the function returns 0;
 *         all locks are released on error.
 */
ssize_t _unified_insert_new_request(const char *buf, off_t offset, size_t count, void **cache,
	bool ip_state, struct write_request *req, struct dentry *d, struct unified_data *priv)
{
	int ret;
	struct dentry_priv *dpr = d->iosched_priv;
	struct write_request *new_req;
	size_t copy_count;

	if (! (*cache)) {
		ret = _unified_cache_alloc(cache, d, priv);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 13017E, (int)ret);
			return ret;
		}
		if (ret == 1) {
			/* d->iosched_lock was released due to cache pressure; need to start again */
			return 0;
		}
	}

	/* Copy data to the spare cache block */
	copy_count = count;
	if (copy_count > priv->cache_size)
		copy_count = priv->cache_size;
	memcpy(cache_manager_get_object_data(*cache), buf, copy_count);

	/* Store new write request */
	new_req = calloc(1, sizeof(struct write_request));
	if (! new_req) {
		ltfsmsg(LTFS_ERR, 13018E);
		_unified_cache_free(*cache, 0, priv);
		ltfs_mutex_unlock(&d->iosched_lock);
		releaseread_mrsw(&priv->lock);
		return -LTFS_NO_MEMORY;
	}
	new_req->offset = offset;
	new_req->count = copy_count;
	if (ip_state)
		new_req->state = REQUEST_IP;
	else
		new_req->state = (copy_count == priv->cache_size) ? REQUEST_DP : REQUEST_PARTIAL;
	new_req->write_cache = *cache;
	*cache = NULL;
	if (req)
		TAILQ_INSERT_BEFORE(req, new_req, list);
	else
		TAILQ_INSERT_TAIL(&dpr->requests, new_req, list);
	_unified_update_queue_membership(true, false, new_req->state, dpr, priv);

	/* Update file size */
	if (new_req->offset + new_req->count > dpr->file_size)
		dpr->file_size = new_req->offset + new_req->count;

	return (ssize_t)count;
}

/**
 * Update an existing request with bytes from a new buffer, changing the state if needed.
 * Must call with a read lock on priv->lock and a lock on dpr->dentry->iosched_lock.
 * This function must not be called to merge dirty (DP targeted) bytes into a REQUEST_IP
 * request.
 * @param buf New bytes to write.
 * @param offset File offset of buf. Must be between req->offset and
 *               req->offset + req->count, inclusive.
 * @param size Length of buf in bytes.
 * @param dpr dentry_priv structure being processed.
 * @param req Write request to modify.
 * @param priv Handle to I/O scheduler data.
 * @return Number of bytes written to req->write_cache from the beginning of buf.
 *         This function does not fail.
 */
size_t _unified_update_request(const char *buf, off_t offset, size_t size,
	struct dentry_priv *dpr, struct write_request *req, struct unified_data *priv)
{
	size_t copy_offset; /* Offset into req->write_cache */
	size_t copy_count;
	char *req_cache;
	struct write_request *w_req;

	if (size == 0)
		return 0;

	req_cache = cache_manager_get_object_data(req->write_cache);

	copy_offset = offset - req->offset;
	copy_count = (req->offset + priv->cache_size) - offset;
	if (copy_count > size)
		copy_count = size;

	memcpy(req_cache + copy_offset, buf, copy_count);
	if (copy_offset + copy_count > req->count)
		req->count = copy_offset + copy_count;

	if (req->state == REQUEST_PARTIAL && req->count == priv->cache_size) {
		TAILQ_FOREACH(w_req, &dpr->requests, list) {
			if (w_req->state == REQUEST_PARTIAL && w_req->offset <= (uint64_t)offset) {
				_unified_update_queue_membership(false, false, REQUEST_PARTIAL, dpr, priv);
				w_req->state = REQUEST_DP;
				_unified_update_queue_membership(true, false, REQUEST_DP, dpr, priv);
			}
		}
	}

	/* Update file size */
	if (req->offset + req->count > dpr->file_size)
		dpr->file_size = req->offset + req->count;

	return copy_count;
}

/**
 * Try to merge 'src' into 'dest'. Copies bytes from src into dest's buffer if dest has room,
 * taking the bytes from dest if there is any overlap. Then truncates or frees src, optionally
 * returning src's cache block to a spare area provided by the caller.
 * Bytes may only be copied from src to dest if they have the same target partition, but
 * truncation or removal of src is performed regardless of the request states.
 * @param dest Target request.
 * @param src Source request.
 * @param spare_cache If non-NULL, points to a spare cache block. If this function frees src
 *                    and *spare_cache is NULL, it stores src->write_cache in *spare_cache.
 * @param dpr Dentry_priv structure the requests belong to.
 * @param priv I/O scheduler private data.
 * @return 2 if src was freed, 1 if it was modified, 0 otherwise.
 */
int _unified_merge_requests(struct write_request *dest, struct write_request *src,
	void **spare_cache, struct dentry_priv *dpr, struct unified_data *priv)
{
	int ret = 0;
	char *src_cache;
	size_t copy_offset, copy_count;

	if (! dest || src->offset > dest->offset + dest->count)
		return 0;

	src_cache = cache_manager_get_object_data(src->write_cache);
	copy_offset = (dest->offset + dest->count) - src->offset;

	/* Append bytes to the previous request.
	 * Merging requests this way is only allowed if the two requests have the same
	 * target partition: otherwise some bytes would get written to the DP more than once. */
	if (dest->state != src->state && (dest->state == REQUEST_IP || src->state == REQUEST_IP))
		copy_count = 0;
	else if (dest->count < priv->cache_size && src->count > copy_offset)
		copy_count = _unified_update_request(src_cache + copy_offset,
			src->offset + copy_offset, src->count - copy_offset, dpr, dest, priv);
	else
		copy_count = 0;

	/* Truncate or remove the current request */
	copy_offset += copy_count;
	if (copy_offset > 0) {
		if (copy_offset < src->count) {
			ret = 1;
			memmove(src_cache, src_cache + copy_offset, src->count - copy_offset);
			src->offset += copy_offset;
			src->count -= copy_offset;
			if (src->state == REQUEST_DP) {
				_unified_update_queue_membership(false, false, src->state, dpr, priv);
				src->state = REQUEST_PARTIAL;
				_unified_update_queue_membership(true, false, src->state, dpr, priv);
			}
		} else {
			ret = 2;
			TAILQ_REMOVE(&dpr->requests, src, list);
			_unified_update_queue_membership(false, false, src->state, dpr, priv);
			if (! spare_cache || *spare_cache)
				_unified_free_request(src, priv);
			else {
				*spare_cache = src->write_cache;
				free(src);
			}
		}
	}

	return ret;
}

/**
 * Flush requests for a dentry.
 * The caller should hold (a) d->iosched_lock and a read lock on priv->lock, or
 * (b) a write lock on priv->lock.
 * @param d Dentry to flush.
 * @param priv I/O scheduler private data.
 * @return 0 on success or a negative value on error.
 */
int _unified_flush_unlocked(struct dentry *d, struct unified_data *priv)
{
	ssize_t ret = 0;
	struct dentry_priv *dpr;
	struct write_request *req, *aux;
	char *req_cache;
	char dp_id;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);

	dp_id = ltfs_dp_id(priv->vol);

	dpr = d->iosched_priv;
	if (! dpr)
		return 0;

	/* Check for previous write errors */
	ret = _unified_get_write_error(dpr);
	if (ret < 0)
		return ret;

	if (TAILQ_EMPTY(&dpr->requests))
		return 0;

	/* Remove dpr from the DP queue and working set */
	_unified_update_queue_membership(false, true, REQUEST_DP, dpr, priv);
	_unified_update_queue_membership(false, true, REQUEST_PARTIAL, dpr, priv);

	ltfs_mutex_lock(&dpr->io_lock);

	TAILQ_FOREACH_SAFE(req, &dpr->requests, list, aux) {
		if (req->state == REQUEST_IP)
			_unified_merge_requests(TAILQ_PREV(req, req_struct, list), req, NULL, dpr, priv);
		else {
			req_cache = cache_manager_get_object_data(req->write_cache);
			ret = ltfs_fsraw_write(d, req_cache, req->count, req->offset, dp_id, false, priv->vol);
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, 13019E, (int)ret);
				(void)_unified_write_index_after_perm(ret, priv);
				_unified_handle_write_error(ret, req, dpr, priv);
				break;
			} else if (dpr->write_ip) {
				req->state = REQUEST_IP;
				_unified_update_queue_membership(true, false, REQUEST_IP, dpr, priv);
				_unified_merge_requests(TAILQ_PREV(req, req_struct, list), req, NULL, dpr, priv);
			} else {
				TAILQ_REMOVE(&dpr->requests, req, list);
				_unified_free_request(req, priv);
			}
		}
	}
	ltfs_mutex_unlock(&dpr->io_lock);

	ret = _unified_get_write_error(dpr);
	return (ret < 0) ? ret : 0;
}

/**
 * Flush all dentries to the data partition.
 * If this function returns success, there are no REQUEST_DP or REQUEST_PARTIAL requests left
 * in the scheduler. There may still be REQUEST_IP requests lying around.
 * @param priv I/O scheduler instance to flush.
 * @return 0 on success or a negative value on error.
 */
int _unified_flush_all(struct unified_data *priv)
{
	int ret;
	struct dentry_priv *dpr, *aux;

	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);

	acquirewrite_mrsw(&priv->lock);

	if (! TAILQ_EMPTY(&priv->dp_queue)) {
		TAILQ_FOREACH_SAFE(dpr, &priv->dp_queue, dp_queue, aux) {
			ret = _unified_flush_unlocked(dpr->dentry, priv);
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, 13020E, dpr->dentry->platform_safe_name, ret);
				releasewrite_mrsw(&priv->lock);
				return ret;
			}
		}
	}

	if (! TAILQ_EMPTY(&priv->working_set)) {
		TAILQ_FOREACH_SAFE(dpr, &priv->working_set, working_set, aux) {
			ret = _unified_flush_unlocked(dpr->dentry, priv);
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, 13020E, dpr->dentry->platform_safe_name, ret);
				releasewrite_mrsw(&priv->lock);
				return ret;
			}
		}
	}

	releasewrite_mrsw(&priv->lock);
	return 0;
}

/**
 * Free a dentry_priv structure if it has no open handles, outstanding requests or
 * queued IP extents.
 * The caller is assumed to have a handle on the dentry, so "no open handles" means
 * d->numhandles == 2 normally, numhandles == 1 if d has been unlinked or if IP processing
 * just finished and there are no open handles.
 * The caller should also hold appropriate locks, which usually means d->iosched_lock.
 * @param d Dentry to free priv structure for.
 * @param target_handles Only free the dentry_priv if the dentry has this many handles or fewer.
 * @param priv I/O scheduler private data.
 */
void _unified_free_dentry_priv_conditional(struct dentry *d, uint32_t target_handles,
	struct unified_data *priv)
{
	uint32_t numhandles;
	struct dentry_priv *dpr;

	acquireread_mrsw(&d->meta_lock);
	numhandles = d->numhandles;
	releaseread_mrsw(&d->meta_lock);

	dpr = d->iosched_priv;
	if (dpr && numhandles <= target_handles && TAILQ_EMPTY(&dpr->requests) &&
		TAILQ_EMPTY(&dpr->alt_extentlist)) {
		/* Take I/O lock first. The background thread could be processing this dentry */
		ltfs_mutex_lock(&dpr->io_lock);
		ltfs_mutex_unlock(&dpr->io_lock);

		ltfs_mutex_destroy(&dpr->write_error_lock);
		ltfs_mutex_destroy(&dpr->io_lock);
		free(dpr);
		d->iosched_priv = NULL;
		ltfs_fsraw_put_dentry(d, priv->vol);
	}
}

/**
 * Free a dentry_priv structure unconditionally, dispatching its alt_extentlist to libltfs
 * if write_ip is enabled.
 * This is called on unmount to ensure that IP extents hit the tape. It emits a warning if the
 * dentry has any outstanding write requests.
 * @param d Dentry to free priv structure for.
 * @param priv I/O scheduler private data.
 */
void _unified_free_dentry_priv(struct dentry *d, struct unified_data *priv)
{
	struct dentry_priv *dpr = d->iosched_priv;

	if (! dpr)
		return;

	if (! TAILQ_EMPTY(&dpr->requests))
		ltfsmsg(LTFS_WARN, 13022W);

	/* Wait for background thread to finish flushing requests */
	ltfs_mutex_lock(&dpr->io_lock);
	ltfs_mutex_unlock(&dpr->io_lock);

	/* Sent alt_extentlist to libltfs */
	if (dpr->write_ip && ! TAILQ_EMPTY(&dpr->alt_extentlist))
		_unified_clear_alt_extentlist(true, dpr, priv);

	ltfs_mutex_destroy(&dpr->write_error_lock);
	ltfs_mutex_destroy(&dpr->io_lock);
	free(dpr);
	d->iosched_priv = NULL;
	ltfs_fsraw_put_dentry(d, priv->vol);
}

/**
 * Set the write_ip flag for a dentry_priv structure.
 * This also requires updating the global DP request counter so that the background writer thread
 * knows this dentry_priv's requests can be freed immediately after writing.
 * The caller should hold an appropriate lock (usually dpr->d->iosched_lock).
 * @param dpr dentry_priv structure to modify.
 * @param priv I/O scheduler data.
 */
void _unified_set_write_ip(struct dentry_priv *dpr, struct unified_data *priv)
{
	dpr->write_ip = true;

	/* Hide DP requests from the global request counter, as they can't be freed */
	if (dpr->in_dp_queue) {
		ltfs_thread_mutex_lock(&priv->queue_lock);
		priv->dp_request_count -= dpr->in_dp_queue;
		ltfs_thread_mutex_unlock(&priv->queue_lock);
	}
}

/**
 * Unset the write_ip flag for a dentry_priv structure. This also requires freeing all
 * REQUEST_IP requests, removing the dentry_priv from the ip_queue (and decrementing the
 * reference count), and clearing the dentry_priv's alt_extentlist.
 * The caller should hold an appropriate lock (usually dpr->d->iosched_lock).
 * @param dpr dentry_priv structure to modify.
 * @param priv I/O scheduler data.
 */
void _unified_unset_write_ip(struct dentry_priv *dpr, struct unified_data *priv)
{
	struct write_request *req, *req_aux;

	dpr->write_ip = false;

	/* Remove dentry_priv from the ip_queue */
	if (dpr->in_ip_queue) {
		TAILQ_FOREACH_SAFE(req, &dpr->requests, list, req_aux) {
			if (req->state == REQUEST_IP) {
				TAILQ_REMOVE(&dpr->requests, req, list);
				_unified_free_request(req, priv);
			}
		}

		/* Decrement reference count: it was incremented when this dentry_priv was added
		 * to the ip_queue. */
		_unified_update_queue_membership(false, true, REQUEST_IP, dpr, priv);
	}

	/* write_ip hides DP requests from the global request counter. Unhide them now */
	if (dpr->in_dp_queue) {
		ltfs_thread_mutex_lock(&priv->queue_lock);
		priv->dp_request_count += dpr->in_dp_queue;
		ltfs_thread_mutex_unlock(&priv->queue_lock);
	}

	/* Clear the alt_extentlist */
	if (! TAILQ_EMPTY(&dpr->alt_extentlist))
		_unified_clear_alt_extentlist(false, dpr, priv);
}

/**
 * Write error handler. When a write error occurs, it is necessary to perform write error
 * propagation and then do something sensible with the request whose write failed.
 * Currently the scheduler assumes the write is fatal, so it is most sensible to clear the
 * affected dentry_priv's request list. This prevents the user from seeing cached data that
 * cannot reach the medium.
 * The caller should hold appropriate locks, usually dpr->dentry->iosched_lock and dpr->io_lock.
 * TODO: there are a few error conditions that really are transient: ENOMEM from libltfs and
 *       maybe some types of device/driver errors. For these, it is desirable to keep the failed
 *       request around, at least for a few retries.
 * @param write_req Return code from ltfs_fsraw_write() or ltfs_fsraw_write_data().
 * @param req Request that could not be written.
 * @param dpr dentry_priv to which the request belongs.
 * @param priv I/O scheduler data.
 * @return true if the error was fatal, false otherwise.
 */
void _unified_handle_write_error(ssize_t write_ret, struct write_request *failed_req,
	struct dentry_priv *dpr, struct unified_data *priv)
{
	struct write_request *req, *aux;
	bool clear_dp = false, clear_ip = false;

	/* Propagate the write error to the caller, UNLESS we were writing to the IP and encountered
	 * an out of space error. That is not a hard failure, as the data is known to be on the DP
	 * at that point. Other IP errors are worth reporting because they may affect LTFS' ability
	 * to make the volume consistent. */
	if (! (failed_req->state == REQUEST_IP && (write_ret == -LTFS_NO_SPACE || write_ret == -LTFS_LESS_SPACE))) {
		ltfs_mutex_lock(&dpr->write_error_lock);
		if (dpr->write_error == 0)
			dpr->write_error = write_ret;
		ltfs_mutex_unlock(&dpr->write_error_lock);
	}

	/* Clear requests for the partition that experienced the error. Also clear requests
	 * for the other partition if the file system is in read-only mode or if the other
	 * partition is known to be out of space. */
	if (failed_req->state == REQUEST_IP) {
		clear_ip = true;
		if ((write_ret != -LTFS_NO_SPACE && write_ret != -LTFS_LESS_SPACE) ||
			ltfs_get_partition_readonly(ltfs_dp_id(priv->vol), priv->vol) < 0) {
			clear_dp = true;
		}
	} else {
		clear_dp = true;
		if ((write_ret != -LTFS_NO_SPACE && write_ret != -LTFS_LESS_SPACE) ||
			ltfs_get_partition_readonly(ltfs_ip_id(priv->vol), priv->vol) < 0) {
			clear_ip = true;
		}
	}

	/* Recompute file size, starting with what libltfs thinks the file size is */
	acquireread_mrsw(&dpr->dentry->meta_lock);
	dpr->file_size = dpr->dentry->size;
	releaseread_mrsw(&dpr->dentry->meta_lock);

	/* Remove requests from the selected partitions */
	if (! TAILQ_EMPTY(&dpr->requests)) {
		if (clear_dp) {
			_unified_update_queue_membership(false, true, REQUEST_DP, dpr, priv);
			_unified_update_queue_membership(false, true, REQUEST_PARTIAL, dpr, priv);
		}
		if (clear_ip)
			_unified_update_queue_membership(false, true, REQUEST_IP, dpr, priv);
		TAILQ_FOREACH_SAFE(req, &dpr->requests, list, aux) {
			if ((req->state == REQUEST_IP && clear_ip) || (req->state != REQUEST_IP && clear_dp)) {
				TAILQ_REMOVE(&dpr->requests, req, list);
				_unified_free_request(req, priv);
			} else if (req->offset + req->count > dpr->file_size)
				dpr->file_size = req->offset + req->count;
		}
	}
}

/**
 * Return the contents of the write_error code of the dentry_priv structure.
 * The write_error flag is reset after being read.
 * The caller is expected to have (a) a write lock on priv->lock or (b) a read lock on
 * priv->lock and a lock on dentry->iosched_lock.
 * @param dpr dentry_priv structure to access.
 * @return The write_error code.
 */
int _unified_get_write_error(struct dentry_priv *dpr)
{
	int ret = 0;

	if (dpr) {
		ltfs_mutex_lock(&dpr->write_error_lock);
		ret = dpr->write_error;
		dpr->write_error = 0;
		ltfs_mutex_unlock(&dpr->write_error_lock);
	}

	return ret;
}

int _unified_write_index_after_perm(int write_ret, struct unified_data *priv)
{
	int ret = 0;
	struct tc_position err_pos;
	unsigned long blocksize;

	if (!IS_WRITE_PERM(-write_ret)) {
		/* Nothing to do for non-medium error */
		return ret;
	}

	ltfsmsg(LTFS_INFO, 13024I, write_ret);
	ret = tape_set_cart_volume_lock_status(priv->vol, PWE_MAM_DP);
	if (ret < 0)
		ltfsmsg(LTFS_ERR, 13026E, "update MAM", ret);

	blocksize = ltfs_get_blocksize(priv->vol);
	ret = tape_get_physical_block_position(priv->vol->device, &err_pos);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 13026E, "get error pos", ret);
		return ret;
	}

	ltfsmsg(LTFS_INFO, 13025I, (int)err_pos.block, (int)blocksize);

	ret = ltfs_fsraw_cleanup_extent(priv->vol->index->root, err_pos, blocksize, priv->vol);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 13026E, "extent cleanup", ret);
		return ret;
	}

	ret = ltfs_write_index(ltfs_ip_id(priv->vol), SYNC_WRITE_PERM, priv->vol);

	return ret;
}

/**
 * Enable profiler function
 * @param work_dir work directory to store profiler data
 * @paran enable enable or disable profiler function of this backend
 * @param iosched_handle Handle to the I/O scheduler data.
 * @return 0 on success or a negative value on error
 */
int unified_set_profiler(char *work_dir, bool enable, void *iosched_handle)
{
	int rc = 0;
	char *path;
	FILE *p;
	struct timer_info timerinfo;
	struct unified_data *priv = iosched_handle;

	if (enable) {
		if (priv->profiler)
			return 0;

		if(!work_dir)
			return -LTFS_BAD_ARG;

		rc = asprintf(&path, "%s/%s%s%s", work_dir, IOSCHED_PROFILER_BASE,
					  priv->vol->label->vol_uuid, PROFILER_EXTENSION);
		if (rc < 0) {
			ltfsmsg(LTFS_ERR, 10001E, __FILE__);
			return -LTFS_NO_MEMORY;
		}

		p = fopen(path, PROFILER_FILE_MODE);

		free(path);

		if (! p)
			rc = -LTFS_FILE_ERR;
		else {
			get_timer_info(&timerinfo);
			fwrite((void*)&timerinfo, sizeof(timerinfo), 1, p);
			priv->profiler = p;
			rc = 0;
		}
	} else {
		if (priv->profiler) {
			fclose(priv->profiler);
			priv->profiler = NULL;
		}
	}

	return rc;
}

struct iosched_ops unified_ops = {
	.init         = unified_init,
	.destroy      = unified_destroy,
	.open         = unified_open,
	.close        = unified_close,
	.read         = unified_read,
	.write        = unified_write,
	.flush        = unified_flush,
	.truncate     = unified_truncate,
	.get_filesize = unified_get_filesize,
	.update_data_placement = unified_update_data_placement,
	.set_profiler = unified_set_profiler,
};

struct iosched_ops *iosched_get_ops(void)
{
	return &unified_ops;
}

#ifndef mingw_PLATFORM
extern char iosched_unified_dat[];
#endif

const char *iosched_get_message_bundle_name(void **message_data)
{
#ifndef mingw_PLATFORM
	*message_data = iosched_unified_dat;
#else
	*message_data = NULL;
#endif
	return "iosched_unified";
}
