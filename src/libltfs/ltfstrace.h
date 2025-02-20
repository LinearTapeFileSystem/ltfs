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
 ** FILE NAME:       ltfstrace.h
 **
 ** DESCRIPTION:     Definitions for LTFS trace
 **
 ** AUTHORS:         Atsushi Abe
 **                  IBM Tokyo Lab., Japan
 **                  piste@jp.ibm.com
 **
 *************************************************************************************
 */

#ifndef __LTFSTRACE_H__
#define __LTFSTRACE_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef mingw_PLATFORM
#include "arch/win/win_util.h"
#include <fcntl.h>
#define PROFILER_FILE_MODE "wb+"
#else
#define PROFILER_FILE_MODE "w+"
#include <sys/wait.h>
#include <sys/fcntl.h>
#endif

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <stdint.h>

#include "libltfs/ltfs.h"
#include "libltfs/ltfslogging.h"
#include "libltfs/ltfs_thread.h"
#include "libltfs/ltfs_locking.h"
#include "libltfs/ltfs_error.h"
#include "libltfs/uthash.h"
#include "libltfs/arch/time_internal.h"

typedef enum {
	FILESYSTEM = 0,		/* Filesystem function trace */
	ADMIN = 1,		/* Admin function trace */
	ADMIN_COMPLETED = 2	/* Completed Admin function trace */
} FUNCTION_TRACE_TYPE;

/*
 *  Definitions for LTFS trace
 */
int  ltfs_trace_init(void);
int ltfs_trace_get_offset(char** val);
void ltfs_trace_destroy(void);
int  ltfs_trace_dump(char *fname, const char *work_dir);
int ltfs_get_trace_status(char **val);
int ltfs_set_trace_status(char *mode);
int ltfs_dump(char *fname, const char *work_dir);
int ltfs_fn_trace_start(FUNCTION_TRACE_TYPE, uint32_t);
void ltfs_admin_function_trace_completed(uint32_t);

/*
 *  Definitions for LTFS request trace
 */

/*
 *  Reqest number definitions for LTFS request trace
 *
 *  Request value -> 0xASSSTTTT
 *    A: Status (enter, exit or others)
 *    S: Source (FUSE, admin channel or others)
 *    T: Request Type (defined by each sources)
 */
#define REQ_NUMBER(status, source, type) _REQ_NUMBER(status, source, type)
#define _REQ_NUMBER(status, source, type) (uint32_t)(0x##status##source##type)

#define REQ_STATUS_MASK (0xF0000000)
#define REQ_SOURCE_MASK (0x0FFF0000)
#define REQ_TYPE_MASK   (0x0000FFFF)

/* Value of request trace */
#define REQ_STAT_ENTER 0
#define REQ_STAT_EVENT 1
#define REQ_STAT_EXIT  8

/*  Request sources
 *    000:       FUSE
 *    001 - 00F: RESERVED
 *    010:       ADMINCHANNEL
 *    111:       IO Scheduler
 *    222:       Tape Backend
 *    333:       Changer Backend
 *    334 - FFF: RESERVED
 */
#define REQ_FUSE 000
#define REQ_ADM  010
#define REQ_IOS  111
#define REQ_DRV  222
#define REQ_CHG  333

/*
 *  Definitions for LTFS function trace
 */

/*
 *  Definitions for LTFS trace file
 */
int ltfs_dump_trace(char* name);

/*
 *  Definitions for LTFS profiler
 */
/* !!!!! Following line is needed because ltfstrace.h is included from ltfs.h */
struct ltfs_volume;
int ltfs_request_profiler_start(const char *work_dir);
int ltfs_request_profiler_stop(void);

#define PROF_REQ       (0x0000000000000001)
#define PROF_IOSCHED   (0x0000000000000002)
#define PROF_DRIVER    (0x0000000000000004)
#define PROF_CHANGER   (0x0000000000000008)

#define REQ_PROFILER_FILE        "prof_request.dat"
#define IOSCHED_PROFILER_BASE    "prof_iosched_"
#define DRIVER_PROFILER_BASE     "prof_driver_"
#define PROFILER_EXTENSION       ".dat"

#define IOSCHED_REQ_ENTER(r)   REQ_NUMBER(REQ_STAT_ENTER, REQ_IOS, r)
#define IOSCHED_REQ_EXIT(r)    REQ_NUMBER(REQ_STAT_EXIT,  REQ_IOS, r)
#define IOSCHED_REQ_EVENT(r)   REQ_NUMBER(REQ_STAT_EVENT,  REQ_IOS, r)

#ifndef TAPEBEND_REQ_ENTER
#define TAPEBEND_REQ_ENTER(r)    REQ_NUMBER(REQ_STAT_ENTER, REQ_DRV, r)
#define TAPEBEND_REQ_EXIT(r)     REQ_NUMBER(REQ_STAT_EXIT,  REQ_DRV, r)
#endif

#define CHANGER_REQ_ENTER(r)    REQ_NUMBER(REQ_STAT_ENTER, REQ_CHG, r)
#define CHANGER_REQ_EXIT(r)     REQ_NUMBER(REQ_STAT_EXIT,  REQ_CHG, r)

/*
 *  Definitions for LTFS request trace
 */
#pragma pack(push, 1)
struct profiler_entry {
	uint64_t time;
	uint32_t req_num;
	uint32_t tid;
};
#pragma pack(pop)

#define PROF_ENTRY_SIZE      (sizeof(struct profiler_entry))
#define REQ_PROF_ENTRY_SIZE  PROF_ENTRY_SIZE /* Don't record information fields in profler data */

#pragma pack(push, 1)
struct request_entry {
	uint64_t time;
	uint32_t req_num;
	uint32_t tid;
	uint64_t info1;
	uint64_t info2;
};
#pragma pack(pop)

#define REQ_TRACE_ENTRY_SIZE (sizeof(struct request_entry))
#define REQ_TRACE_SIZE       (4 * 1024 * 1024) /* 4MB */
#define REQ_TRACE_ENTRIES    (REQ_TRACE_SIZE / REQ_TRACE_ENTRY_SIZE)

struct request_trace {
	ltfs_mutex_t         req_trace_lock;
	ltfs_mutex_t         req_profiler_lock;
	uint32_t             max_index;
	uint32_t             cur_index;
	FILE*                profiler;
	struct request_entry entries[REQ_TRACE_ENTRIES];
};

extern bool                 trace_enable;
extern struct request_trace *req_trace;
extern _time_stamp_t        start_offset;

static inline void ltfs_request_trace(uint32_t req_num, uint64_t info1, uint64_t info2)
{
	if (!trace_enable)
		return;
	return;
	__try {
		if (req_trace) {
			uint32_t index;

			ltfs_mutex_lock(&req_trace->req_trace_lock);

			if (req_trace->cur_index >= req_trace->max_index) {
				index = req_trace->cur_index;
				req_trace->cur_index = 0;
			}
			else
				index = req_trace->cur_index++;

			ltfs_mutex_unlock(&req_trace->req_trace_lock);

			req_trace->entries[index].time = get_time_stamp(&start_offset);
			req_trace->entries[index].tid = ltfs_get_thread_id();
			req_trace->entries[index].req_num = req_num;
			req_trace->entries[index].info1 = info1;
			req_trace->entries[index].info2 = info2;

			if (req_trace->profiler) {
				ltfs_mutex_lock(&req_trace->req_profiler_lock);
				fwrite((void*)&req_trace->entries[index], REQ_PROF_ENTRY_SIZE, 1, req_trace->profiler);
				ltfs_mutex_unlock(&req_trace->req_profiler_lock);
			}
		}
	}__except (EXCEPTION_EXECUTE_HANDLER) {
		fprintf(stderr, "An exception occurred!\n");
	}
}

static inline void ltfs_profiler_add_entry(FILE* file, ltfs_mutex_t *mutex, uint32_t req_num)
{
	struct profiler_entry entry;

	if (file) {
		entry.time = get_time_stamp(&start_offset);
		entry.tid = ltfs_get_thread_id();
		entry.req_num = req_num;
		if (mutex)
			ltfs_mutex_lock(mutex);
		fwrite((void*)&entry, PROF_ENTRY_SIZE, 1, file);
		if (mutex)
			ltfs_mutex_unlock(mutex);
	}
}

#ifdef __cplusplus
}
#endif

#endif /* __LTFSTRACE_H__ */
