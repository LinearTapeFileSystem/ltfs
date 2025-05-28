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
** FILE NAME:       arch/time_internal.c
**
** DESCRIPTION:     Implements platform-specific time functions.
**
** AUTHOR:          Michael A. Richmond
**                  IBM Almaden Research Center
**                  mar@almaden.ibm.com
**
*************************************************************************************
*/

#ifndef time_internal_c_
#define time_internal_c_

#include <string.h>
#include <time.h>
#include <limits.h>
#include "libltfs/ltfslogging.h"
#include "libltfs/arch/time_internal.h"

// TODO: Fix definition of SIZEOF_TIME_T in cmake
#if defined(__APPLE__) || defined(__CMAKE_BUILD)
/*
 * In OSX environment time_t is always 64-bit width.
 * It is specified by compile option of Makefile.osx because autoconf architecture is
 * not used under OSX.
 */
#define SIZEOF_TIME_T (8)
#elif !defined(__CMAKE_BUILD)
#include "config.h"
#endif

#if ! ((SIZEOF_TIME_T == 4) || (SIZEOF_TIME_T == 8))
#error time_t width is not 4 or 8
#endif

ltfs_time_t ltfs_timegm(struct tm *t)
{
	int tmp;
	int64_t rel;
	ltfs_time_t ret;

	tmp = (t->tm_mon - 13) / 12;
	rel = 86400LL * ((1461 * (t->tm_year + 6700 + tmp)) / 4
	                 + (367 * (t->tm_mon - 1 - 12 * tmp)) / 12
	                 - (3 * ((t->tm_year + 6800 + tmp) / 100)) / 4
	                 + t->tm_mday - 2472663)
	      + 3600 * t->tm_hour + 60 * t->tm_min + t->tm_sec;

	if (sizeof(time_t) == 4) {
		if (rel > LONG_MAX)
			ltfsmsg(LTFS_WARN, 17172W, t->tm_year + 1900, t->tm_mon + 1, t->tm_mday
					, t->tm_hour, t->tm_min, t->tm_sec);
		if (rel < LONG_MIN)
			ltfsmsg(LTFS_WARN, 17173W, t->tm_year + 1900, t->tm_mon + 1, t->tm_mday
					, t->tm_hour, t->tm_min, t->tm_sec);
	}

	ret = rel;
	return ret;
}

#ifdef __APPLE__
#include <errno.h>
#include <libkern/OSReturn.h>
#include <mach/mach.h>
#include <mach/clock.h>
#include <mach/mach_time.h>

void __get_time(_time_stamp_t* t)
{
	*t =  mach_absolute_time();
}

int get_timer_info(struct timer_info *ti)
{
	mach_timebase_info_data_t timebase;

	(void) mach_timebase_info(&timebase);

	ti->type = TIMER_TYPE_OSX;
	ti->base = ((uint64_t)timebase.denom << 32) + timebase.numer;

	return 0;
}

int get_osx_current_timespec(struct ltfs_timespec* now) {
	int ret = -1;

	kern_return_t kernel_return;
	clock_serv_t  clock;
	mach_timespec_t time;

	kernel_return = host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &clock);
	if (KERN_SUCCESS == kernel_return) {
		kernel_return = clock_get_time(clock, &time);
		if (KERN_SUCCESS == kernel_return) {
			now->tv_sec  = time.tv_sec;
			now->tv_nsec = time.tv_nsec;
			ret = 0;
		}
	  }
	if (ret != 0) {
		errno = EINVAL;
		ret = -1;
	}
	if (ret < 0)
		ltfsmsg(LTFS_ERR, 11110E, ret);
	return ret;
}
#elif defined(mingw_PLATFORM)
#else
int get_unix_current_timespec(struct ltfs_timespec* now)
{
	struct timespec ts;
	int ret = clock_gettime(CLOCK_REALTIME, &ts);
	now->tv_sec = ts.tv_sec;
	now->tv_nsec = ts.tv_nsec;
	return ret;
}
struct tm *get_unix_localtime(const ltfs_time_t *timep)
{
	time_t t = *timep;
	return localtime(&t);
}
#endif

int ltfs_get_days_of_year(int64_t nYear)
{
	int nDays = ((nYear % 400) == 0 || (((nYear % 100) != 0) && ((nYear % 4) == 0))) ?
		366 : 365;
	return nDays;
}

int ltfs_get_mday_from_yday(int64_t nYear, int nYday, int* pnMonth)
{
	int i = 0;
	int nMday = nYday;
	int nMonth = -1;
	int anDaysOfMonth[] = {
		31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
	};
	anDaysOfMonth[1] = (ltfs_get_days_of_year(nYear) == 365) ? 28 : 29;
	for (i = 0; i < 12; i++) {
		if (nMday < anDaysOfMonth[i]) {
			nMonth = i;
			break;
		}
		nMday -= anDaysOfMonth[i];
	}
	if (pnMonth) {
		*pnMonth = nMonth;
	}
	if (nMday < 0 || 12 <= i) {
		nMday = -2;
	}
	// Note: like struct tm, nYday and nMonth are 0 origin but
	//       nMday is 1 origin.
	nMday++;
	return nMday;
}

struct tm *ltfs_gmtime(const ltfs_time_t *timep, struct tm *result)
{
	int n;
	int64_t nSrcTime  = *timep;
	int64_t nYears = 0;
	int64_t nYday = 0;
	int nDaysOfYear = 0;

	// cyclic periods in terms leap years
	int64_t nDays4Y   =   1461; //   4 years = (365 days ) *  4 + 1
	int64_t nDays100Y =  36524; // 100 years = (  4 years) * 25 - 1
	int64_t nDays400Y = 146097; // 400 years = (100 years) *  4 + 1
	int64_t n400Y, n100Y, n4Y, n1Y;

	memset(result, 0, sizeof(struct tm));

	n = nSrcTime % 60;	// n = seconds
	nSrcTime /= 60;	// nSrcTime is in minutes
	if (0 <= n) {
		result->tm_sec  = n;
	}
	else {
		result->tm_sec  = n + 60;
		nSrcTime--;
	}

	n = nSrcTime % 60;	// n = minutes
	nSrcTime /= 60;	// nSrcTime is in hours
	if (0 <= n) {
		result->tm_min  = n;
	}
	else {
		result->tm_min  = n + 60;
		nSrcTime--;
	}

	n = nSrcTime % 24;	// n = hours
	nSrcTime /= 24;	// nSrcTime is in days
	if (0 <= n) {
		result->tm_hour  = n;
	}
	else {
		result->tm_hour  = n + 24;
		nSrcTime--;
	}

	// now nSrcTime is number of days since 1970-01-01
	// 2000-03-01 00:00:00 may be a good point to be set as the
	// reference point since it is a boundary of periods of the 400 years
	//         0 = time_t of 1970-01-01 00:00:00
	// 951868800 = time_t of 2000-03-01 00:00:00
	//     11017 = 951868800 / 60 / 60 / 24
	// number of days since 1970-01-01 to 2000-03-01 is 11017
	nSrcTime -= 11017;
	// now nSrcTime is number of days since 2000-03-01
	// wday of 2000-03-01 is 3
	result->tm_wday = (nSrcTime + 3) % 7;
	if (result->tm_wday < 0) {
		result->tm_wday += 7;
	}
	// calculate years since 2000-03-01
	n400Y = nSrcTime / nDays400Y;
	nYears += n400Y * 400;
	nSrcTime %= nDays400Y;
	n100Y = nSrcTime / nDays100Y;
	nYears += n100Y * 100;
	nSrcTime %= nDays100Y;
	if (n100Y == 4) {
		nSrcTime--;
	}
	n4Y = nSrcTime / nDays4Y;
	nYears += n4Y * 4;
	nSrcTime %= nDays4Y;
	if (n100Y < 0 && n4Y == 0) {
		nSrcTime++;
	}
	n1Y = nSrcTime / 365;
	nYears += n1Y;
	nSrcTime %= 365;
	if (n1Y == 4) {
		nSrcTime--;
	}
	if (n1Y < 0) {
		if (!(n100Y < 0 && n4Y == 0)) {
			nSrcTime++;
		}
	}
	// now nSrcTime is offset from March 1st.
	nDaysOfYear = ltfs_get_days_of_year(nYears + 2000);
	nYday = nSrcTime + 31 + 28 + (nDaysOfYear - 365);
	if (nDaysOfYear <= nYday) {
		nYears++;
		nYday -= nDaysOfYear;
	}
	else if (nYday < 0) {
		nDaysOfYear = ltfs_get_days_of_year((--nYears) + 2000);
		nYday += nDaysOfYear;
	}
	result->tm_yday = nYday;
	result->tm_mday = ltfs_get_mday_from_yday((nYears + 2000), nYday, &(result->tm_mon));
	result->tm_year = nYears + 2000 - 1900;
	result->tm_isdst = -1;
	return result;
}

struct timespec timespec_from_ltfs_timespec(const struct ltfs_timespec *pSrc)
{
	struct timespec ts;

#ifdef __APPLE__
	/*
	 * In OSX environment time_t is always 64-bit width.
	 * To use the same Makefile.osx for the limitation of build and test resources,
	 * SDE 1.3 assumes that the value of sizeof(time_t) is 4.
	 */
	if (pSrc->tv_sec > 0x7FFFFFFFLL)
		ts.tv_sec = 0x7FFFFFFFLL;
	else if (pSrc->tv_sec < -0x80000000LL)
		ts.tv_sec = -0x80000000LL;
	else
		ts.tv_sec = pSrc->tv_sec;
#else
	if (sizeof(time_t) == 4) {
		if (pSrc->tv_sec > LONG_MAX)
			ts.tv_sec = LONG_MAX;
		else if (pSrc->tv_sec < LONG_MIN)
			ts.tv_sec = LONG_MIN;
		else
			ts.tv_sec = pSrc->tv_sec;
	} else
		ts.tv_sec = pSrc->tv_sec;
#endif

	ts.tv_nsec = pSrc->tv_nsec;
	return ts;
}

struct ltfs_timespec ltfs_timespec_from_timespec(const struct timespec *pSrc)
{
	struct ltfs_timespec ts;
	ts.tv_sec = pSrc->tv_sec;
	ts.tv_nsec = pSrc->tv_nsec;
	return ts;
}

#endif /* time_internal_h_ */
