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
** FILE NAME:       ltfs_thread.h
**
** DESCRIPTION:     LTFS thread operation imprementation
**
** AUTHORS:         Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/

#ifndef __LTFS_THREAD_H__
#define __LTFS_THREAD_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef mingw_PLATFORM
#include "arch/win/win_thread.h"
#else

#include <pthread.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <unistd.h>

typedef pthread_t          ltfs_thread_t;

/* Use struct for checking wrong usage of ltfs_mutex and ltfs_thread_mutex by compliter */
typedef struct {
	pthread_mutex_t thread_lock;
} ltfs_thread_mutex_t;

typedef pthread_attr_t     ltfs_thread_attr_t;
typedef pthread_cond_t     ltfs_thread_cond_t;
typedef pthread_condattr_t ltfs_thread_condattr_t;
typedef void               *ltfs_thread_return;
typedef void               *ltfs_thread_return_detached;

#define LTFS_THREAD_RC_NULL    (NULL)

enum {
	LTFS_THREAD_CREATE_DETACHED = PTHREAD_CREATE_DETACHED,
	LTFS_THREAD_CREATE_JOINABLE = PTHREAD_CREATE_JOINABLE
};

static inline int ltfs_thread_mutex_init(ltfs_thread_mutex_t *mutex)
{
	return pthread_mutex_init(&mutex->thread_lock, NULL);
}

static inline int ltfs_thread_mutex_destroy(ltfs_thread_mutex_t *mutex)
{
	return pthread_mutex_destroy(&mutex->thread_lock);
}

static inline int ltfs_thread_mutex_lock(ltfs_thread_mutex_t *mutex)
{
	return pthread_mutex_lock(&mutex->thread_lock);
}

static inline int ltfs_thread_mutex_unlock(ltfs_thread_mutex_t *mutex)
{
	return pthread_mutex_unlock(&mutex->thread_lock);
}

static inline int ltfs_thread_mutex_trylock(ltfs_thread_mutex_t *mutex)
{
	return pthread_mutex_trylock(&mutex->thread_lock);
}

static inline int ltfs_thread_attr_init(ltfs_thread_attr_t *attr)
{
	return pthread_attr_init(attr);
}

static inline int ltfs_thread_attr_destroy(ltfs_thread_attr_t *attr)
{
	return pthread_attr_destroy(attr);
}

static inline int ltfs_thread_attr_setdetachstate(ltfs_thread_attr_t *attr, int detachstate)
{
	return pthread_attr_setdetachstate(attr, detachstate);
}

static inline int ltfs_thread_cond_broadcast(ltfs_thread_cond_t *cond)
{
	return pthread_cond_broadcast(cond);
}

static inline int ltfs_thread_cond_signal(ltfs_thread_cond_t *cond)
{
	return pthread_cond_signal(cond);
}

static inline int ltfs_thread_cond_destroy(ltfs_thread_cond_t *cond)
{
	return pthread_cond_destroy(cond);
}

static inline int ltfs_thread_cond_init(ltfs_thread_cond_t *restrict cond)
{
	return pthread_cond_init(cond, NULL);
}

static inline int ltfs_thread_cond_timedwait(ltfs_thread_cond_t *restrict cond,
											 ltfs_thread_mutex_t *restrict mutex,
											 const int sec)
{
	struct timeval now;
	struct timespec timeout;

	gettimeofday(&now, NULL);
	timeout.tv_sec = now.tv_sec + sec;
	timeout.tv_nsec = 0;

	return pthread_cond_timedwait(cond, &mutex->thread_lock, &timeout);
}

static inline int ltfs_thread_cond_wait(ltfs_thread_cond_t *restrict cond,
										ltfs_thread_mutex_t *restrict mutex)
{
	return pthread_cond_wait(cond, &mutex->thread_lock);
}

static inline int ltfs_thread_create(ltfs_thread_t *thread,
									 ltfs_thread_return (*start_routine) (void *),
									 void *arg)
{
	return pthread_create(thread, NULL, start_routine, arg);
}

static inline int ltfs_thread_create_detached(ltfs_thread_t *thread,
											  const ltfs_thread_attr_t *attr,
											  ltfs_thread_return_detached (*start_routine) (void *),
											  void *arg)
{
	return pthread_create(thread, attr, start_routine, arg);
}

static inline void ltfs_thread_exit(void)
{
	return pthread_exit(NULL);
}

static inline void ltfs_thread_exit_detached(void)
{
	return pthread_exit(NULL);
}

static inline int ltfs_thread_join(ltfs_thread_t thread)
{
	return pthread_join(thread, NULL);
}

static inline ltfs_thread_t ltfs_thread_self(void)
{
	return pthread_self();
}

static inline int ltfs_thread_yield(void)
{
#ifdef __APPLE__
	return sched_yield();
#else
	return pthread_yield();
#endif
}

#ifdef __APPLE__
extern uint32_t ltfs_get_thread_id(void);
#else
static inline uint32_t ltfs_get_thread_id(void)
{
	uint32_t tid;

	tid = (uint32_t)syscall(SYS_gettid);

	return tid;
}
#endif

#endif

#ifdef __cplusplus
}
#endif

#endif /* __LTFS_THREAD_H__ */
