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
*/

/*************************************************************************************
 ** FILE NAME:       ltfstrace.c
 **
 ** DESCRIPTION:     Routines for LTFS trace
 **
 ** AUTHORS:         Atsushi Abe
 **                  IBM Tokyo Lab., Japan
 **                  piste@jp.ibm.com
 **
 *************************************************************************************
 */

#include "libltfs/ltfstrace.h"
#include "libltfs/queue.h"

/*************************************************************************************
 * TRACE FILE STRUCTURE
 *  +==========================+
 *  |H      Trace Header       |
 *  +==========================+
 *  |H   Request Trace Header  |
 *  +--------------------------+------------------------------------------------------
 *  |                          | Request Trace
 *  |    Request Trace (Body)  | - All threads access to one trace structure
 *  |                          | - Store request entry and exit
 *  +==========================+======================================================
 *  |H  Function Trace Header  |
 *  +--------------------------+------------------------------------------------------
 *  |                          | FS (filesystem) Function Trace
 *  |   FS Function Trace #1   | - Create a trace structure on each thread
 *  |                  (Body)  | - Use each trace area with round robin strategy
 *  +--------------------------+ - Store function trace
 *  |           ...            |
 *  +--------------------------+
 *  |                          |
 *  |   FS Function Trace #n   |
 *  |                  (Body)  |
 *  +--------------------------+------------------------------------------------------
 *  |                          | Admin Function Trace
 *  |  Admin Function Trace #1 | - Create a trace structure on each thread (each request)
 *  |                  (Body)  | - Use each trace area with round robin strategy
 *  +--------------------------+ - Trace structure is freed with LRU
 *  |           ...            | - Store function trace
 *  +--------------------------+
 *  |                          |
 *  |  Admin Function Trace #n |
 *  |                  (Body)  |
 *  +--------------------------+------------------------------------------------------
 *  |                          | Completed Admin Function Trace
 *  |   Admin Fn Comp Trace #1 | - Create a trace structure on each thread (each request)
 *  |                  (Body)  | - Use each trace area with round robin strategy
 *  +--------------------------+ - Trace structure is freed with LRU
 *  |           ...            | - Store function trace
 *  +--------------------------+
 *  |                          |
 *  |   Admin Fn Comp Trace #n |
 *  |                  (Body)  |
 *  +==========================+------------------------------------------------------
 **************************************************************************************/

/*
 * Definition for LTFS trace header information
 */
#define LTFS_TRACE_SIGNATURE "LTFS_TRC"
#pragma pack(push, 1)
struct trace_header {
	char signature[8];               /**< Signature for LTFS trace */
	uint32_t header_size;            /**< Size of trace header */
	uint32_t req_header_offset;      /**< Request trace header offset */
	uint32_t fn_header_offset;       /**< Function trace header offset */
	unsigned short endian_signature; /**< Endian signagure : 0x1234 or 0x3412 */
	struct timer_info timerinfo;     /**< Timer info to reconstruct time stamp in post process */
	uint32_t trace_size;             /**< Whole size of trace (all headers and bodies) */
	uint32_t crc;                    /**< CRC (reserved for future use) */
};
#pragma pack(pop)

/*
 * Definitions for LTFS Request header information
 */
#pragma pack(push, 1)
struct request_header {
	uint32_t header_size;            /**< Size of request header */
	uint32_t num_of_req_trace;       /**< Number of request trace descriptrs (always 1) */
	struct request_trace_descriptor {
		uint32_t  size_of_entry; /**< Size of entry */
		uint32_t  num_of_entry;  /**< Number of entry */
	} req_t_desc;                    /**< Request header descriptor */
	uint32_t crc;                    /**< CRC (reserved for future use) */
};
#pragma pack(pop)

/*
 * Definitions for LTFS function trace header
 */
#pragma pack(push, 1)
struct function_trace_header {
	uint32_t header_size;		/**< Size of function trace header */
	uint32_t num_of_fn_trace;	/**< Number of function trace */
	struct function_trace_descriptor {
		uint32_t type;		/**< Function trace type (admin or filesystem) */
		uint32_t size_of_entry;	/**< Size of function trace entry */
		uint32_t num_of_entry;	/**< Number of function trace entries */
	} *req_t_desc;
	uint32_t crc;                    /**< CRC (reserved for future use) */
};
#pragma pack(pop)

/*
 * Definitions for LTFS function trace data
 */
#pragma pack(push, 1)
struct function_entry {
	uint64_t time;
	uint64_t function;
	uint64_t info1;
	uint64_t info2;
};
#pragma pack(pop)

#define FN_TRACE_ENTRY_SIZE (sizeof(struct function_entry))

/*
 * "Filesystem" Function Trace Data structure
 */
#define FS_FN_TRACE_SIZE       (1 * 1024 * 1024) /* 1MB */
#define FS_FN_TRACE_ENTRIES    (FS_FN_TRACE_SIZE / FN_TRACE_ENTRY_SIZE)

struct filesystem_function_trace {
	MultiReaderSingleWriter        trace_lock;      /**< Lock for trace data */
	uint32_t                       max_index;
	uint32_t                       cur_index;
	struct function_entry          entries[FS_FN_TRACE_ENTRIES];
};

struct filesystem_trace_list {
	uint32_t                          tid;
	struct filesystem_function_trace *fn_entry;
	UT_hash_handle                    hh;
};

/*
 * "Admin" Function Trace Data structure
 */
#define ADMIN_FN_TRACE_ENTRIES	256
#define ADMIN_FN_TRACE_SIZE	(ADMIN_FN_TRACE_ENTRIES * FN_TRACE_ENTRY_SIZE)
struct admin_function_trace {
	MultiReaderSingleWriter        trace_lock;      /**< Lock for trace data */
	uint32_t                       max_index;
	uint32_t                       cur_index;
	struct function_entry          entries[ADMIN_FN_TRACE_ENTRIES];
};

struct admin_trace_list {
	uint32_t                       tid;
	struct admin_function_trace    *fn_entry;
	UT_hash_handle                 hh;
};

/*
 * Definitions for Tail Q of Admin function trace
 */
#define MAX_ADMIN_COMP_NUM 512
struct admin_completed_function_trace {
	TAILQ_ENTRY(admin_completed_function_trace) list;
	uint32_t                       tid;
	struct admin_function_trace    *fn_entry;
	MultiReaderSingleWriter        trace_lock;
};

/*
 *  Definitions for LTFS Profiler
 */

struct trace_header           *trc_header    = NULL;
struct request_header         *req_header    = NULL;
struct function_trace_header  *fn_trc_header = NULL;

struct request_trace          *req_trace     = NULL;
struct filesystem_trace_list  *fs_tr_list    = NULL;
struct admin_trace_list       *admin_tr_list = NULL;
TAILQ_HEAD(admin_completed, admin_completed_function_trace);
struct admin_completed        *acomp         = NULL;

_time_stamp_t                 start_offset;
struct ltfs_timespec          start;
struct timer_info             timerinfo;
bool                          trace_enable   = true;

static int ltfs_request_trace_init(void)
{
	int ret = 0;

	req_trace = calloc(1, sizeof(struct request_trace));
	if (!req_trace) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return -LTFS_NO_MEMORY;
	}

	ret = ltfs_mutex_init(&req_trace->req_trace_lock);
	if (ret) {
		ltfsmsg(LTFS_ERR, 10002E, ret);
		free(req_trace);
		return -LTFS_MUTEX_INIT;
	}

	ret = ltfs_mutex_init(&req_trace->req_profiler_lock);
	if (ret) {
		ltfsmsg(LTFS_ERR, 10002E, ret);
		free(req_trace);
		return -LTFS_MUTEX_INIT;
	}

	req_trace->max_index = REQ_TRACE_ENTRIES - 1;

	return 0;
}

static void ltfs_request_trace_destroy(void)
{
	if (req_trace) {
		ltfs_mutex_destroy(&req_trace->req_trace_lock);
		ltfs_mutex_destroy(&req_trace->req_profiler_lock);
		free(req_trace);
		req_trace = NULL;
	}
}

static int ltfs_fn_trace_init(void)
{
	acomp = (struct admin_completed *) calloc (1, sizeof(struct admin_completed));
	TAILQ_INIT(acomp);
	return 0;
}

int ltfs_fn_trace_start(FUNCTION_TRACE_TYPE type, uint32_t tid)
{
	if (trace_enable == false)
		return 0;

	if (type == FILESYSTEM) {
		struct filesystem_trace_list *item = NULL;
		struct filesystem_function_trace *tr_data = NULL;
		item = (struct filesystem_trace_list *) calloc(1, sizeof(struct filesystem_trace_list));
		if (!item) {
			ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}
		item->tid = tid;

		tr_data = (struct filesystem_function_trace *) calloc(1, sizeof(struct filesystem_function_trace));
		if (!tr_data) {
			ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}
		tr_data->max_index = FS_FN_TRACE_ENTRIES - 1;
		tr_data->cur_index = 0;
		item->fn_entry = tr_data;
		HASH_ADD_INT(fs_tr_list, tid, item);

	} else if (type == ADMIN) {
		struct admin_trace_list *item = NULL;
		struct admin_function_trace *tr_data = NULL;
		item = (struct admin_trace_list *) calloc(1, sizeof(struct admin_trace_list));
		if (!item) {
			ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}
		item->tid = tid;

		tr_data = (struct admin_function_trace *) calloc(1, sizeof(struct admin_function_trace));
		if (!tr_data) {
			ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}
		tr_data->max_index = ADMIN_FN_TRACE_ENTRIES - 1;
		tr_data->cur_index = 0;
		item->fn_entry = tr_data;
		HASH_ADD_INT(admin_tr_list, tid, item);
	}
	return 0;
}

void ltfs_admin_function_trace_completed(uint32_t tid)
{
	struct admin_trace_list *item;
	struct admin_completed_function_trace *tailq_item;
	uint32_t num_of_comp_adm = 0;

	if (trace_enable == false)
		return;

	HASH_FIND_INT(admin_tr_list, &tid, item);
	if (item != NULL) {
		TAILQ_FOREACH (tailq_item, acomp, list)
			num_of_comp_adm++;

		if (num_of_comp_adm > MAX_ADMIN_COMP_NUM) {
			/* Remove first tailq entry */
			tailq_item = TAILQ_FIRST(acomp);
			TAILQ_REMOVE(acomp, tailq_item, list);
			free(tailq_item->fn_entry);
			free(tailq_item);
		}
		tailq_item = (struct admin_completed_function_trace *) calloc(1, sizeof(struct admin_completed_function_trace));

		acquirewrite_mrsw(&tailq_item->trace_lock);
		struct admin_function_trace *ptr = NULL;
		ptr = (struct admin_function_trace *) calloc(1, sizeof(struct admin_function_trace));
		ptr->cur_index = item->fn_entry->cur_index;
		for (unsigned int j=0; j<ptr->cur_index; j++) {
			ptr->entries[j].time = item->fn_entry->entries[j].time;
			ptr->entries[j].function = item->fn_entry->entries[j].function;
			ptr->entries[j].info1 = item->fn_entry->entries[j].info1;
			ptr->entries[j].info2 = item->fn_entry->entries[j].info2;
		}
		tailq_item->fn_entry = ptr;
		tailq_item->tid = tid;
		TAILQ_INSERT_TAIL(acomp, tailq_item, list);
		releasewrite_mrsw(&tailq_item->trace_lock);

		HASH_DEL(admin_tr_list, item);
		free(item->fn_entry);
		free(item);
	}
}

static void ltfs_function_trace_destroy(void)
{
	if (fs_tr_list) {
		struct filesystem_trace_list *fsitem, *tmp;
		HASH_ITER(hh, fs_tr_list, fsitem, tmp) {
			destroy_mrsw(&fsitem->fn_entry->trace_lock);
			free(fsitem->fn_entry);
			free(fsitem);
		}
		fs_tr_list = NULL;
	}
	if (admin_tr_list) {
		struct admin_trace_list *aditem, *tmp;
		HASH_ITER(hh, admin_tr_list, aditem, tmp) {
			destroy_mrsw(&aditem->fn_entry->trace_lock);
			free(aditem->fn_entry);
			free(aditem);
		}
		admin_tr_list = NULL;
	}
	if (acomp) {
		struct admin_completed_function_trace *tailq_item, *tmp;
		TAILQ_FOREACH_SAFE(tailq_item, acomp, list, tmp) {
			destroy_mrsw(&tailq_item->trace_lock);
			free(tailq_item->fn_entry);
			free(tailq_item);
		}
		free(acomp);
		acomp = NULL;
	}
}

void ltfs_function_trace(uint64_t func, uint64_t info1, uint64_t info2)
{
	struct admin_trace_list *item;
	uint32_t tid;
	uint64_t time;

	if (trace_enable == false)
		return;

	time = get_time_stamp(&start_offset);
	tid = ltfs_get_thread_id();
	HASH_FIND_INT(admin_tr_list, &tid, item);
	if (item != NULL) {
		acquirewrite_mrsw(&item->fn_entry->trace_lock);
		item->fn_entry->entries[item->fn_entry->cur_index].time = time;
		item->fn_entry->entries[item->fn_entry->cur_index].function = func;
		item->fn_entry->entries[item->fn_entry->cur_index].info1 = info1;
		item->fn_entry->entries[item->fn_entry->cur_index].info2 = info2;
		if (item->fn_entry->cur_index >= item->fn_entry->max_index)
			item->fn_entry->cur_index = 0;
		else
			item->fn_entry->cur_index++;
		releasewrite_mrsw(&item->fn_entry->trace_lock);
	} else {
		struct filesystem_trace_list *item;
		HASH_FIND_INT(fs_tr_list, &tid, item);
		if (item != NULL) {
			acquirewrite_mrsw(&item->fn_entry->trace_lock);
			item->fn_entry->entries[item->fn_entry->cur_index].time = time;
			item->fn_entry->entries[item->fn_entry->cur_index].function = func;
			item->fn_entry->entries[item->fn_entry->cur_index].info1 = info1;
			item->fn_entry->entries[item->fn_entry->cur_index].info2 = info2;

			if (item->fn_entry->cur_index >= item->fn_entry->max_index)
				item->fn_entry->cur_index = 0;
			else
				item->fn_entry->cur_index++;

			releasewrite_mrsw(&item->fn_entry->trace_lock);
		} else {
			ltfs_fn_trace_start(FILESYSTEM, tid);
		}
	}
}

int ltfs_request_profiler_start(const char *work_dir)
{
	int ret;
	char *path;

	if (req_trace->profiler)
		return 0;

	if(!work_dir)
		return -LTFS_BAD_ARG;

	ret = asprintf(&path, "%s/%s", work_dir, REQ_PROFILER_FILE);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 10001E, __FILE__);
		return -LTFS_NO_MEMORY;
	}

	req_trace->profiler = fopen(path, PROFILER_FILE_MODE);

	free(path);

	if (! req_trace->profiler)
		ret = -LTFS_FILE_ERR;
	else {
		fwrite((void*)&timerinfo, sizeof(timerinfo), 1, req_trace->profiler);
		ret = 0;
	}

	return ret;
}

int ltfs_request_profiler_stop(void)
{
	if (req_trace->profiler) {
		fclose(req_trace->profiler);
		req_trace->profiler = NULL;
	}

	return 0;
}

int ltfs_header_init(void)
{
	/* Trace header */
	trc_header = calloc(1, sizeof(struct trace_header));
	if (!trc_header) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return -LTFS_NO_MEMORY;
	}
	strncpy(trc_header->signature, LTFS_TRACE_SIGNATURE, strlen(LTFS_TRACE_SIGNATURE));
	trc_header->header_size = sizeof(struct trace_header);
	trc_header->req_header_offset = sizeof(struct trace_header);
	trc_header->fn_header_offset = sizeof(struct trace_header) + sizeof(struct request_header) + REQ_TRACE_SIZE;
	trc_header->endian_signature = 0x1234;
	trc_header->timerinfo.type = timerinfo.type;
	trc_header->timerinfo.base = timerinfo.base;
	trc_header->crc = 0xFACEFEED;

	/* Request trace header */
	req_header = calloc(1, sizeof(struct request_header));
	if (!trc_header) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return -LTFS_NO_MEMORY;
	}
	req_header->header_size = sizeof(struct request_header);
	req_header->num_of_req_trace = 1;
	req_header->crc = 0xCAFEBABE;

	/* Function trace header */
	fn_trc_header = calloc(1, sizeof(struct function_trace_header));
	if (!fn_trc_header) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return -LTFS_NO_MEMORY;
	}
	fn_trc_header->crc = 0xDEADBEEF;
	return 0;
}

int ltfs_trace_init(void)
{
	int ret = 0;

	if (trace_enable == false)
		return ret;

	/* Store launch time */
	get_current_timespec(&start);
	__get_time(&start_offset);

	/* Get timer info (architecture dependent) */
	get_timer_info(&timerinfo);

	/* Initialize trace header */
	ret = ltfs_header_init();

	/* Initalize trace structures */
	ret = ltfs_request_trace_init();

	/* Initialize function trace structures */
	ret = ltfs_fn_trace_init();

	return ret;
}

int ltfs_trace_get_offset(char** val)
{
#ifdef __APPLE__
	return asprintf(val, "%llu", start_offset);
#elif defined(mingw_PLATFORM)
	return asprintf(val, "%llu", start_offset);
#else
	return asprintf(val, "%lu.%09lu", start_offset.tv_sec, start_offset.tv_nsec);
#endif
}

void ltfs_trace_destroy(void)
{
	/* Destroy trace structures */
	ltfs_request_trace_destroy();

	/* Destroy function trace structures */
	ltfs_function_trace_destroy();

	free(trc_header);
	trc_header    = NULL;

	free(req_header);
	req_header    = NULL;

	free(fn_trc_header);
	fn_trc_header = NULL;
}

int ltfs_dump(char *fname, const char *work_dir)
{
#ifndef mingw_PLATFORM
	int ret = 0, num_args = 0, status;
	char *path, *pid;
	pid_t fork_pid;
	const unsigned int max_arguments = 32;
	const char *args[max_arguments];

	if(!work_dir)
		return -LTFS_BAD_ARG;

	ret = asprintf(&path, "%s/%s", work_dir, fname);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 10001E, __FILE__);
		return -LTFS_NO_MEMORY;
	}

	ret = asprintf(&pid, "%ld", (long)getpid());
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 10001E, __FILE__);
		return -LTFS_NO_MEMORY;
	}

	fork_pid = fork();
	if (fork_pid < 0) {
		ltfsmsg(LTFS_ERR, 17233E);
	} else  if (fork_pid == 0) {
		args[num_args++] = "/usr/bin/gcore";
		args[num_args++] = "-o";
		args[num_args++] = path;
		args[num_args++] = pid;
		args[num_args++] = NULL;

		execv(args[0], (char **) args);
		exit(errno);
	} else {
		waitpid(fork_pid, &status, 0);
		ret = WEXITSTATUS(status);
	}
#endif
	return 0;
}

int ltfs_trace_dump(char *fname, const char *work_dir)
{
	int ret = 0, fd;
	char *path;

	if(trace_enable == false)
		return 0;

	if(!work_dir)
		return -LTFS_BAD_ARG;

	ret = asprintf(&path, "%s/%s", work_dir, fname);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 10001E, __FILE__);
		return -LTFS_NO_MEMORY;
	}

	/* Open file */
	fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0666);
	if(fd < 0)
		return -errno;

	free(path);

	if (req_trace)
	{
		uint32_t num_of_fn_trace = 0, num_of_fs_fn_trace = 0, num_of_adm_fn_trace = 0, n = 0;
		struct admin_completed_function_trace *tailq_item;
		struct filesystem_trace_list *fsitem;
		struct admin_trace_list *admitem;

		/* Calculate the number of function traces */
		num_of_fs_fn_trace += HASH_COUNT(fs_tr_list);
		num_of_adm_fn_trace += HASH_COUNT(admin_tr_list);
		TAILQ_FOREACH (tailq_item, acomp, list)
			num_of_adm_fn_trace++;
		num_of_fn_trace = num_of_fs_fn_trace + num_of_adm_fn_trace;

		fn_trc_header->num_of_fn_trace = num_of_fn_trace;
		fn_trc_header->header_size = 8 + 12 * num_of_fn_trace;

		fn_trc_header->req_t_desc =
			(struct function_trace_descriptor *) calloc(num_of_fn_trace, sizeof(struct function_trace_descriptor));
		if (!fn_trc_header->req_t_desc) {
			ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}

		for (fsitem=fs_tr_list; fsitem != NULL; fsitem=fsitem->hh.next) {
			fn_trc_header->req_t_desc[n].type = FILESYSTEM;
			fn_trc_header->req_t_desc[n].size_of_entry = FS_FN_TRACE_SIZE;
			acquireread_mrsw(&fsitem->fn_entry->trace_lock);
			fn_trc_header->req_t_desc[n++].num_of_entry = fsitem->fn_entry->cur_index;
			releaseread_mrsw(&fsitem->fn_entry->trace_lock);
		}
		for (admitem=admin_tr_list; admitem != NULL; admitem=admitem->hh.next) {
			fn_trc_header->req_t_desc[n].type = ADMIN;
			fn_trc_header->req_t_desc[n].size_of_entry = ADMIN_FN_TRACE_SIZE;
			acquireread_mrsw(&admitem->fn_entry->trace_lock);
			fn_trc_header->req_t_desc[n++].num_of_entry = admitem->fn_entry->cur_index;
			releaseread_mrsw(&admitem->fn_entry->trace_lock);
		}
		TAILQ_FOREACH (tailq_item, acomp, list) {
			fn_trc_header->req_t_desc[n].type = ADMIN_COMPLETED;
			fn_trc_header->req_t_desc[n].size_of_entry = ADMIN_FN_TRACE_SIZE;
			acquireread_mrsw(&tailq_item->fn_entry->trace_lock);
			fn_trc_header->req_t_desc[n++].num_of_entry = tailq_item->fn_entry->cur_index;
			releaseread_mrsw(&tailq_item->fn_entry->trace_lock);
		}

		/* Set header information */
		req_header->req_t_desc.num_of_entry = req_trace->cur_index;
		req_header->req_t_desc.size_of_entry = REQ_TRACE_SIZE;
		trc_header->trace_size =
			req_header->req_t_desc.size_of_entry +		/* Request trace */
			(num_of_fs_fn_trace * FS_FN_TRACE_SIZE) +	/* Function trace (filesystem) */
			(num_of_adm_fn_trace * ADMIN_FN_TRACE_SIZE) +	/* Function trace (admin) */
			trc_header->header_size + req_header->header_size + fn_trc_header->header_size;

		/* Write headers */
		(void)write(fd, trc_header, sizeof(struct trace_header));
		(void)write(fd, req_header, sizeof(struct request_header));

		/* Write request trace data */
		ltfs_mutex_lock(&req_trace->req_trace_lock);
		(void)write(fd, req_trace->entries, REQ_TRACE_SIZE);
		ltfs_mutex_unlock(&req_trace->req_trace_lock);

		/* Write function trace header */
		(void)write(fd, &fn_trc_header->header_size, sizeof(uint32_t));
		(void)write(fd, &fn_trc_header->num_of_fn_trace, sizeof(uint32_t));
		for (unsigned int i=0; i<n; i++)
			(void)write(fd, &fn_trc_header->req_t_desc[i], sizeof(struct function_trace_descriptor));
		(void)write(fd, &fn_trc_header->crc, sizeof(uint32_t));
		free(fn_trc_header->req_t_desc);
		fn_trc_header->req_t_desc = NULL;

		/* Write function trace data */
		for (fsitem=fs_tr_list; fsitem != NULL; fsitem=fsitem->hh.next) {
			acquireread_mrsw(&fsitem->fn_entry->trace_lock);
			(void)write(fd, fsitem->fn_entry->entries, FS_FN_TRACE_SIZE);
			releaseread_mrsw(&fsitem->fn_entry->trace_lock);
		}
		for (admitem=admin_tr_list; admitem != NULL; admitem=admitem->hh.next) {
			acquireread_mrsw(&admitem->fn_entry->trace_lock);
			(void)write(fd, admitem->fn_entry->entries, ADMIN_FN_TRACE_SIZE);
			releaseread_mrsw(&admitem->fn_entry->trace_lock);
		}
		TAILQ_FOREACH (tailq_item, acomp, list) {
			acquireread_mrsw(&tailq_item->fn_entry->trace_lock);
			(void)write(fd, tailq_item->fn_entry->entries, ADMIN_FN_TRACE_SIZE);
			releaseread_mrsw(&tailq_item->fn_entry->trace_lock);
		}
	}
	close(fd);

	return 0;
}

int ltfs_get_trace_status(char **val)
{
	int ret = 0;
	char *trstat = NULL;

	ret = asprintf(&trstat, "%s", (trace_enable == true) ? "on" : "off" );
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 10001E, __FILE__);
		return -LTFS_NO_MEMORY;
	}
	*val = SAFE_STRDUP(trstat);
	if (! (*val)) {
		ltfsmsg(LTFS_ERR, 10001E, __FILE__);
		return -LTFS_NO_MEMORY;
	}
	free(trstat);
	return 0;
}

int ltfs_set_trace_status(char *mode)
{
	int ret = 0;

	if (! strcmp(mode, "on")) {
		trace_enable = true;
		ltfs_trace_init();
	} else {
		if (trace_enable == true)
			ltfs_trace_destroy();
		trace_enable = false;
	}
	return ret;
}
