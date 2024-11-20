/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2022 IBM Corp. All rights reserved.
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
** FILE NAME:       periodic_sync.c
**
** DESCRIPTION:     Implements the periodic sync feature
**
** AUTHOR:          Atsushi Abe
**                  IBM Yamato, Japan
**                  PISTE@jp.ibm.com
**
*************************************************************************************
*/

#include "ltfs.h"
#include "ltfs_fsops.h"

#ifdef mingw_PLATFORM
int gettimeofday(struct timeval* tv, struct timezone* tz) {
	LARGE_INTEGER freq, count;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&count);

	tv->tv_sec = count.QuadPart / freq.QuadPart;
	tv->tv_usec = (count.QuadPart % freq.QuadPart) * 1000000 / freq.QuadPart;

	return 0; // success
}
#endif

/**
 * Periodic sync scheduler private data structure.
 */
struct periodic_sync_data {
	ltfs_thread_cond_t   periodic_sync_thread_cond;  /**< Used to wake up the periodic sync thread */
	ltfs_thread_mutex_t  periodic_sync_thread_mutex; /**< Used to handle the periodic sync thread */
	ltfs_thread_t        periodic_sync_thread_id;    /**< Thread id of the periodic sync thread */
	bool             keepalive;                  /**< Used to terminate the background thread */
	int              period_sec;                 /**< Period between sync (sec) */
	struct ltfs_volume *vol;                     /**< A reference to the LTFS volume structure */
};

/**
 * Main routine for periodic sync.
 * @param data Periodic sync private data
 * @return NULL.
 */
#define FUSE_REQ_ENTER(r)   REQ_NUMBER(REQ_STAT_ENTER, REQ_FUSE, r)
#define FUSE_REQ_EXIT(r)    REQ_NUMBER(REQ_STAT_EXIT,  REQ_FUSE, r)

#define REQ_SYNC        fffe

ltfs_thread_return periodic_sync_thread(void* data)
{
	struct periodic_sync_data *priv = (struct periodic_sync_data *) data;
	struct timeval now;
	int ret;

	ltfs_thread_mutex_lock(&priv->periodic_sync_thread_mutex);
	while (priv->keepalive && gettimeofday(&now, NULL) == 0) {
		ltfs_thread_cond_timedwait(&priv->periodic_sync_thread_cond,
								   &priv->periodic_sync_thread_mutex,
								   priv->period_sec);
		if (! priv->keepalive)
			break;

		if (priv->vol->mount_type == MOUNT_ROLLBACK ||
			priv->vol->mount_type == MOUNT_ROLLBACK_META) {
			/* Never call sync on R/O mount */
			continue;
		}

		ltfs_request_trace(FUSE_REQ_ENTER(REQ_SYNC), 0, 0);

		ltfsmsg(LTFS_DEBUG, 17067D, "Sync-by-Time");
		ret = ltfs_fsops_flush(NULL, false, priv->vol);
		if (ret < 0) {
			/* Failed to flush file data */
			ltfsmsg(LTFS_WARN, 17063W, __FUNCTION__);
		}

		ltfs_set_commit_message_reason(SYNC_PERIODIC, priv->vol);

		ret = ltfs_sync_index(SYNC_PERIODIC, true, LTFS_INDEX_AUTO, priv->vol);
		if (ret < 0) {
			ltfsmsg(LTFS_INFO, 11030I, ret);
			priv->keepalive = false;
		}

		ltfs_request_trace(FUSE_REQ_EXIT(REQ_SYNC), ret, 0);
	}
	ltfs_thread_mutex_unlock(&priv->periodic_sync_thread_mutex);

	ltfsmsg(LTFS_DEBUG, 17064D, "Sync-by-Time");
	ltfs_thread_exit();

	return LTFS_THREAD_RC_NULL;
}

/**
 * Verifies if the periodic sync thread is currently running.
 * @param vol LTFS volume
 * @return true if the thread is running, false if not.
 */
bool periodic_sync_thread_initialized(struct ltfs_volume *vol)
{
	struct periodic_sync_data *priv = vol ? vol->periodic_sync_handle : NULL;
	bool initialized = false;

	if (priv) {
		ltfs_thread_mutex_lock(&priv->periodic_sync_thread_mutex);
		initialized = priv->keepalive;
		ltfs_thread_mutex_unlock(&priv->periodic_sync_thread_mutex);
	}

	return initialized;
}

/**
 * Initialize the periodic sync thread.
 * @param sec timer in which the syncing will be performed
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
int periodic_sync_thread_init(int sec, struct ltfs_volume *vol)
{
	int ret;
	struct periodic_sync_data *priv;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);

	priv = calloc(1, sizeof(struct periodic_sync_data));
	if (! priv) {
		ltfsmsg(LTFS_ERR, 10001E, "periodic_sync_thread_init: periodic sync data");
		return -LTFS_NO_MEMORY;
	}

	priv->vol = vol;
	priv->keepalive = true;
	priv->period_sec = sec;

	ret = ltfs_thread_cond_init(&priv->periodic_sync_thread_cond);
	if (ret) {
		ltfsmsg(LTFS_ERR, 10003E, ret);
		free(priv);
		return -ret;
	}
	ret = ltfs_thread_mutex_init(&priv->periodic_sync_thread_mutex);
	if (ret) {
		ltfsmsg(LTFS_ERR, 10002E, ret);
		ltfs_thread_cond_destroy(&priv->periodic_sync_thread_cond);
		free(priv);
		return -ret;
	}
	ret = ltfs_thread_create(&priv->periodic_sync_thread_id, periodic_sync_thread, priv);
	if (ret < 0) {
		/* Failed to spawn the periodic sync thread (%d) */
		ltfsmsg(LTFS_ERR, 17099E, ret);
		ltfs_thread_mutex_destroy(&priv->periodic_sync_thread_mutex);
		ltfs_thread_cond_destroy(&priv->periodic_sync_thread_cond);
		free(priv);
		return -ret;
	}

	ltfsmsg(LTFS_DEBUG, 17065D);
	vol->periodic_sync_handle = priv;

	return 0;
}

/**
 * Destroy the periodic sync thread.
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
int periodic_sync_thread_destroy(struct ltfs_volume *vol)
{
	struct periodic_sync_data *priv = vol ? vol->periodic_sync_handle : NULL;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);

	ltfs_thread_mutex_lock(&priv->periodic_sync_thread_mutex);
	priv->keepalive = false;
	ltfs_thread_cond_signal(&priv->periodic_sync_thread_cond);
	ltfs_thread_mutex_unlock(&priv->periodic_sync_thread_mutex);

	ltfs_thread_join(priv->periodic_sync_thread_id);
	ltfs_thread_cond_destroy(&priv->periodic_sync_thread_cond);
	ltfs_thread_mutex_destroy(&priv->periodic_sync_thread_mutex);
	free(priv);

	vol->periodic_sync_handle = NULL;

	ltfsmsg(LTFS_DEBUG, 17066D);
	return 0;
}
