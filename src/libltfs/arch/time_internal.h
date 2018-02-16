/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2018 IBM Corp. All rights reserved.
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
** FILE NAME:       arch/time_internal.h
**
** DESCRIPTION:     Prototypes for platform-specific time functions.
**
** AUTHOR:          Michael A. Richmond
**                  IBM Almaden Research Center
**                  mar@almaden.ibm.com
**
*************************************************************************************
*/

#ifndef time_internal_h_
#define time_internal_h_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef mingw_PLATFORM
#include "libltfs/arch/win/win_util.h"
#else
#include <time.h>
typedef int64_t	ltfs_time_t;
struct ltfs_timespec {
	ltfs_time_t	tv_sec;
	long		tv_nsec;
};
#endif

#define TIMER_TYPE_LINUX   (0x0000000000000000)
#define TIMER_TYPE_OSX     (0x0000000000000001)
#define TIMER_TYPE_WINDOWS (0x0000000000000002)

#pragma pack(1)
struct timer_info {
	uint64_t type;
	uint64_t base;
};
#pragma pack(0)

#define LTFS_TIME_T_MAX (253402300799) /* 9999/12/31 23:59:59 UTC */
#define LTFS_TIME_T_MIN (-62167219200) /* 0000/01/01 00:00:00 UTC */
#define LTFS_TIME_OUT_OF_RANGE    (1)  /* Time stamp is out of range */
#define LTFS_NSEC_MAX   (999999999)    /* MAX of nano sec */
#define LTFS_NSEC_MIN   (0)            /* MIN of nano sec */

#define timer_sub(a, b, result) \
  do { \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec; \
    (result)->tv_nsec = (a)->tv_nsec - (b)->tv_nsec; \
    if ((result)->tv_nsec < 0) { \
      --(result)->tv_sec; \
      (result)->tv_nsec += 1000000000; \
    } \
  } while (0)

ltfs_time_t ltfs_timegm(struct tm *t);
struct tm *ltfs_gmtime(const ltfs_time_t *timep, struct tm *result);
struct timespec timespec_from_ltfs_timespec(const struct ltfs_timespec *pSrc);
struct ltfs_timespec ltfs_timespec_from_timespec(const struct timespec *pSrc);

#ifdef __APPLE__
int get_osx_current_timespec(struct ltfs_timespec* now);
#endif

#ifdef __APPLE__
int get_timer_info(struct timer_info *ti);
#define _get_current_timespec(timespec) get_osx_current_timespec(timespec)
#define get_localtime(time) localtime(time)
#elif defined(mingw_PLATFORM)
int get_timer_info(struct timer_info *ti);
#define _get_current_timespec(timespec) get_win32_current_timespec(timespec)
#define get_localtime(time) get_win32_localtime(time)
#define get_gmtime(time) get_win32_gmtime(time)
#else
int get_unix_current_timespec(struct ltfs_timespec* now);
struct tm *get_unix_localtime(const ltfs_time_t *timep);
#define _get_current_timespec(timespec) get_unix_current_timespec(timespec)
#define get_localtime(time) get_unix_localtime(time);
#endif

static inline int normalize_ltfs_time(struct ltfs_timespec* t)
{
	int ret = LTFS_TIME_OUT_OF_RANGE;

	if (t->tv_sec > (ltfs_time_t)LTFS_TIME_T_MAX) {
		t->tv_sec = (ltfs_time_t)LTFS_TIME_T_MAX;
		t->tv_nsec = LTFS_NSEC_MAX;
	} else if (t->tv_sec < (ltfs_time_t)LTFS_TIME_T_MIN) {
		t->tv_sec = (ltfs_time_t)LTFS_TIME_T_MIN;
		t->tv_nsec = LTFS_NSEC_MIN;
	} else
		ret = 0;

	return ret;
}

static inline int get_current_timespec(struct ltfs_timespec* now)
{
	int ret;

	ret = _get_current_timespec(now);
	if (! ret)
		ret = normalize_ltfs_time(now);

	return ret;
}

#ifndef gmtime_r
#define gmtime_r win_gmtime_r
#endif /* gmtime_r */

/*
 *  Time stamp functions
 */
#ifdef __APPLE__
typedef uint64_t _time_stamp_t;

extern void __get_time(_time_stamp_t* t);
extern int get_timer_info(struct timer_info *ti);

inline static uint64_t get_time_stamp(_time_stamp_t* start)
{
	_time_stamp_t now;

	__get_time(&now);

	return (uint64_t)(now - *start);
}

#elif defined(mingw_PLATFORM)
#else /* Linux */
typedef struct timespec _time_stamp_t;

inline static void __get_time(_time_stamp_t* t)
{
	clock_gettime(CLOCK_MONOTONIC, t);
}

inline static int get_timer_info(struct timer_info *ti)
{
	ti->type = TIMER_TYPE_LINUX;
	ti->base = 0LL;

	return 0;
}

inline static void __diff_time(_time_stamp_t* result, _time_stamp_t* end, _time_stamp_t* start)
{
	result->tv_sec = end->tv_sec - start->tv_sec;
	if (end->tv_nsec < start->tv_nsec) {
		result->tv_sec--;
		result->tv_nsec = 1000000000 - start->tv_nsec + end->tv_nsec ;
	} else
		result->tv_nsec = end->tv_nsec - start->tv_nsec;
}

inline static uint64_t get_time_stamp(_time_stamp_t* start)
{
	_time_stamp_t now, s;
	uint64_t ret;

	__get_time(&now);
	__diff_time(&s, &now, start);
	ret = ((s.tv_sec & 0xFFFFFFFF) << 32) | (s.tv_nsec & 0xFFFFFFFF);

	return ret;
}

#endif

#ifdef __cplusplus
};
#endif

#endif /* time_internal_h_ */
