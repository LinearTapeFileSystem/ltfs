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

#ifndef __ltfslogging_h
#define __ltfslogging_h

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include "crossbuild/compatibility.h"


enum ltfs_log_levels {
	LTFS_NONE  = -1, /* Don't print any log (special use for mkltfs/ltfsck) */
	LTFS_ERR    = 0,  /* Fatal error or operation failed unexpectedly */
	LTFS_WARN   = 1,  /* Unexpected condition, but the program can continue */
	LTFS_INFO   = 2,  /* Helpful message */
	LTFS_DEBUG  = 3,  /* Diagnostic messages (Level 0: Base Level) */
	LTFS_DEBUG1 = 4,  /* Diagnostic messages (Level 1) */
	LTFS_DEBUG2 = 5,  /* Diagnostic messages (Level 2) */
	LTFS_DEBUG3 = 6,  /* Diagnostic messages (Level 3) */
	LTFS_TRACE  = 7,  /* Full call tracing */
};

extern int ltfs_log_level;
extern int ltfs_syslog_level;
extern bool ltfs_print_thread_id;

/* Wrapper for ltfsmsg_internal. It only invokes the message print function if the requested
 * log level is not too verbose. */
#ifdef MSG_CHECK
#include "ltfsmsg.h"
#define ltfsmsg(level, id, ...)					\
	do {										\
		printf(LTFS ## id, ##__VA_ARGS__);		\
	} while(0)
#else
#define ltfsmsg(level, id, ...) \
	do { \
		if (level <= ltfs_log_level) \
			ltfsmsg_internal(true, level, NULL, #id, ##__VA_ARGS__);	\
	} while (0)

#define ltfsmsgplain(level, id, ...) \
	do { \
		if (level <= ltfs_log_level) \
			ltfsmsg_internal(true, level, NULL, id, ##__VA_ARGS__);	\
	} while (0)
#endif

/* CAUTION: ltfsmsg_buffer takes message ID as a text literal */
/* Wrapper for ltfsmsg_internal. It only invokes the message print function if the requested
 * log level is not too verbose. */
#define ltfsmsg_buffer(level, id, buffer, ...)	\
	do { \
		*buffer = NULL; \
		if (level <= ltfs_log_level) \
			ltfsmsg_internal(true, level, buffer, id, ##__VA_ARGS__);	\
	} while (0)

/* Wrapper for ltfsmsg_internal that prints a message without the LTFSnnnnn prefix. It
 * always invokes the message print function, regardless of the message level. */
#ifdef MSG_CHECK
#define ltfsresult(id, ...)						\
	do {										\
		printf(LTFS ## id, ##__VA_ARGS__);		\
	} while(0)
#else
#define ltfsresult(id, ...)						\
	do {																\
		ltfsmsg_internal(false, LTFS_TRACE + 1, NULL, #id, ##__VA_ARGS__); \
	} while (0)
#endif

/* Shortcut for asserting that a function argument is not NULL. It generates an error-level
 * message if the given argument is NULL. Functions for which a NULL argument is a warning
 * should not use this macro. */
#define CHECK_ARG_NULL(var, ret) \
	do { \
		if (! (var)) { \
			ltfsmsg(LTFS_ERR, 10005E, #var, __FUNCTION__); \
			return ret; \
		} \
	} while (0)

/**
 * Initialize the logging and error reporting functions.
 * @param log_level Logging level (generally one of LTFS_ERROR...LTFS_TRACE).
 * @param use_syslog Send error/warning/info messages to syslog? This function does not call
 *                   openlog(); the calling application may do so if it wants to.
 * @param print_thread_id Print thread ID to the message.
 * @return 0 on success or a negative value on error.
 */
int ltfsprintf_init(int log_level, bool use_syslog, bool print_thread_id);

/**
 * Tear down the logging and error reporting framework.
 */
void ltfsprintf_finish();

/**
 * Update ltfs_log_level
 */
int ltfsprintf_set_log_level(int log_level);

/**
 * Load messages for a plugin from the specified resource name.
 * @param bundle_name Message bundle name.
 * @param bundle_data Message bundle data structure.
 * @param messages On success, contains a handle to the loaded message bundles. That handle
 *                 should be passed to @ltfsprintf_unload_plugin later.
 * @return 0 on success or a negative value on error.
 */
int ltfsprintf_load_plugin(const char *bundle_name, void *bundle_data, void **messages);

/**
 * Stop using messages from the given plugin message bundle.
 * @param handle Message bundle handle, as returned by @ltfsprintf_load_plugin.
 */
void ltfsprintf_unload_plugin(void *handle);

/**
 * Print a message in the system locale. Any extra arguments are substituted into the
 * format string. The current logging level is ignored, so the ltfsmsg macro
 * (which calls this function) should be used instead.
 * The generated output goes to stderr. If syslog is enabled, messages of severity LTFS_INFO
 * through LTFS_ERR go to syslog as well. LTFS_DEBUG and LTFS_TRACE level messages always go
 * only to stderr.
 * @param print_id Print the message prefix LTFSnnnnn ?
 * @param level Log level of this message, must be one of the ltfs_log_levels (LTFS_ERROR, etc.).
 * @param id Unique ID of this error.
 * @return 0 if a message was printed or a negative value on error.
 */
int ltfsmsg_internal(bool print_id, int level, char **msg_out, const char *id, ...);

#ifdef __cplusplus
}
#endif

#endif /* __ltfslogging_h */
