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
** FILE NAME:       ltfs_locking_new.h
**
** DESCRIPTION:     New LTFS locking method and Multi-reader single-writer locking
**                  implementation supporting the system that inhibit to unlock
**                  a mutex from different thread (like FreeBSD)
**
** AUTHORS:         Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/

#ifndef __LTFS_LOCKING_NEW_H__
#define __LTFS_LOCKING_NEW_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef mingw_PLATFORM
	#error "New locking system is required on Windows"
#else /* ! mingw_PLATFORM */

#include <pthread.h>
#include <execinfo.h> /* For backtrace() */
#include <unistd.h>

/* Use struct for checking wrong usage of ltfs_mutex and ltfs_thread_mutex by compliter */
typedef struct {
	pthread_mutex_t lock;
} ltfs_mutex_t;

typedef pthread_mutexattr_t ltfs_mutexattr_t;

enum {
	LTFS_THREAD_PROCESS_SHARED = PTHREAD_PROCESS_SHARED,
	LTFS_THREAD_PROCESS_PRIVATE = PTHREAD_PROCESS_PRIVATE
};

static inline void backtrace_info(void)
{
	void *address[50];
	char **funcs = NULL;
	size_t back_num, i;

	back_num = backtrace(address, 50);
	funcs = backtrace_symbols( address, back_num);

	for(i = 0; i < back_num; ++i)  {
		if (funcs && funcs[i])
			ltfsmsg(LTFS_INFO, 17193I, (int)i, address[i], funcs[i]);
		else
			ltfsmsg(LTFS_INFO, 17194I, (int)i, address[i]);
	}

	if (funcs) free(funcs);

	return;
}

static inline int ltfs_mutex_init(ltfs_mutex_t *mutex)
{
	return pthread_mutex_init(&mutex->lock, NULL);
}

static inline int ltfs_mutex_init_attr(ltfs_mutex_t *mutex, ltfs_mutexattr_t *attr)
{
	return pthread_mutex_init(&mutex->lock, attr);
}

static inline int ltfs_mutex_destroy(ltfs_mutex_t *mutex)
{
	return pthread_mutex_destroy(&mutex->lock);
}

static inline int ltfs_mutex_lock(ltfs_mutex_t *mutex)
{
	return pthread_mutex_lock(&mutex->lock);
}

static inline int ltfs_mutex_unlock(ltfs_mutex_t *mutex)
{
	return pthread_mutex_unlock(&mutex->lock);
}

static inline int ltfs_mutex_trylock(ltfs_mutex_t *mutex)
{
	return pthread_mutex_trylock(&mutex->lock);
}

static inline int ltfs_mutexattr_init(ltfs_mutexattr_t *attr)
{
	return pthread_mutexattr_init(attr);
}

static inline int ltfs_mutexattr_destroy(ltfs_mutexattr_t *attr)
{
	return pthread_mutexattr_destroy(attr);
}

static inline int ltfs_mutexattr_setpshared(ltfs_mutexattr_t *attr,
											int pshared)
{
#ifdef __NetBSD__
	abort(); /* Not implemented */
#else
	return pthread_mutexattr_setpshared(attr, pshared);
#endif
}

typedef struct MultiReaderSingleWriter {
	ltfs_mutex_t     exclusive_mutex;
	pthread_rwlock_t rw_lock;
	uint32_t         writer; //if there is a write lock acquired
	uint32_t         long_lock;
} MultiReaderSingleWriter;

static inline int
init_mrsw(MultiReaderSingleWriter *mrsw)
{
	int ret;

	mrsw->writer = 0;
	mrsw->long_lock = 0;
	ret = ltfs_mutex_init(&mrsw->exclusive_mutex);
	if (ret)
		return -ret;

	ret = pthread_rwlock_init(&mrsw->rw_lock, NULL);
	if (ret) {
		ltfs_mutex_destroy(&mrsw->exclusive_mutex);
		return -ret;
	}

	return 0;
}

static inline void
destroy_mrsw(MultiReaderSingleWriter *mrsw)
{
	pthread_rwlock_destroy(&mrsw->rw_lock);
	ltfs_mutex_destroy(&mrsw->exclusive_mutex);
}

static inline bool
try_acquirewrite_mrsw(MultiReaderSingleWriter *mrsw)
{
	int err;
	err = ltfs_mutex_trylock(&mrsw->exclusive_mutex);
	if (err)
		return false;

	err = pthread_rwlock_trywrlock(&mrsw->rw_lock);
	if (err) {
		ltfs_mutex_unlock(&mrsw->exclusive_mutex);
		return false;
	}
	mrsw->writer=1;
	return true;
}

static inline void
acquirewrite_mrsw(MultiReaderSingleWriter *mrsw)
{
	ltfs_mutex_lock(&mrsw->exclusive_mutex);
	pthread_rwlock_wrlock(&mrsw->rw_lock);
	mrsw->writer=1;
	mrsw->long_lock=0;
}

static inline void
acquirewrite_mrsw_long(MultiReaderSingleWriter *mrsw)
{
	ltfs_mutex_lock(&mrsw->exclusive_mutex);
	pthread_rwlock_wrlock(&mrsw->rw_lock);
	mrsw->writer=1;
	mrsw->long_lock=1;
}

static inline void
releasewrite_mrsw(MultiReaderSingleWriter *mrsw)
{
	mrsw->writer=0;
	mrsw->long_lock=0;
	pthread_rwlock_unlock(&mrsw->rw_lock);
	ltfs_mutex_unlock(&mrsw->exclusive_mutex);
}

static inline void
acquireread_mrsw(MultiReaderSingleWriter *mrsw)
{
	ltfs_mutex_lock(&mrsw->exclusive_mutex);
	mrsw->long_lock=0;
	ltfs_mutex_unlock(&mrsw->exclusive_mutex);

	pthread_rwlock_rdlock(&mrsw->rw_lock);
}

static inline int
acquireread_mrsw_short(MultiReaderSingleWriter *mrsw)
{
	int ret = 0;
	//struct timespec tv = {0, 100000000};

	if (mrsw->long_lock)
		return -1;

	while(1) {
		ret = ltfs_mutex_trylock(&mrsw->exclusive_mutex);
		if (! ret)
			break;
		if (mrsw->long_lock)
			return -1;
		/*
		 * Wait 1 sec to avoid busy loop.
		 * This kind of busy loop consumes CPU resource
		 */
		sleep(1);
	}
	ltfs_mutex_unlock(&mrsw->exclusive_mutex);

	pthread_rwlock_rdlock(&mrsw->rw_lock);

	return 0;
}

static inline void
releaseread_mrsw(MultiReaderSingleWriter *mrsw)
{
	pthread_rwlock_unlock(&mrsw->rw_lock);
}

static inline void
release_mrsw(MultiReaderSingleWriter *mrsw)
{
	if(mrsw->writer) {
		//If there is a writer, and we hold a lock, we must be the writer.
		releasewrite_mrsw(mrsw);
	} else {
		releaseread_mrsw(mrsw);
	}
}

//downgrades a write lock to a read lock
static inline void
writetoread_mrsw(MultiReaderSingleWriter *mrsw)
{
	// This thread owns write protection meaning that
	// there are no threads that own read protection.

	// This means:
	// 1) read_count is currently 0 AND all threads requesting read and write protection
	//    are blocked on write_exclusive_mutex
	// 2) There may threads requesting read protection that got through the write_exclusive_mutex gate
	//    before this thread successfully got write protection.
	//    At most, one of this set of threads has gotten through the read_count_mutex gate, and may
	//    have already bumped read_count to 1, or is in the process of bumping read_count to 1. This
	//    thread may or may not be blocked on reading_mutex, but is following that code path.

	// Unset the writer flag before allowing any readers in.
	mrsw->writer = 0;
	mrsw->long_lock = 0;

	// Replace acquired write lock to a read lock
	pthread_rwlock_unlock(&mrsw->rw_lock);
	pthread_rwlock_rdlock(&mrsw->rw_lock);

	//Release the write_exclusive_mutex, to allow others to write protect, and additional readers in.
	ltfs_mutex_unlock(&mrsw->exclusive_mutex);
}
#endif /* mingw_PLATFORM */

#ifdef __cplusplus
}
#endif

#endif /* __LTFS_LOCKING_NEW_H__ */
