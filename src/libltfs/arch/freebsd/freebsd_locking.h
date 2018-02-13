/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2018 IBM Corp. All rights reserved.
**  Copyright (c) 2014-2018 Spectra Logic Corporation. All rights reserved.
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
** FILE NAME:       freebsd_locking.h
**
** DESCRIPTION:     LTFS locking method imprementation
**                  Multi-reader single-writer lock implementation for FreeBSD.
**
** AUTHORS:         Mark A. Smith
**                  IBM Almaden Research Center
**                  mark1smi@us.ibm.com
**
**                  Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
**                  Reid Linnemann
**                  Spectra Logic Corporation
**                  reidl@spectralogic.com
*************************************************************************************
*/
#ifndef __FREEBSD_LOCKING_H__
#define __FREEBSD_LOCKING_H__

typedef pthread_rwlock_t MultiReaderSingleWriter;

static inline int
init_mrsw(MultiReaderSingleWriter *mrsw)
{
	return (pthread_rwlock_init(mrsw, NULL));
}

static inline void
destroy_mrsw(MultiReaderSingleWriter *mrsw)
{
	pthread_rwlock_destroy(mrsw);
}

static inline bool
try_acquirewrite_mrsw(MultiReaderSingleWriter *mrsw)
{
	int err;

	err = pthread_rwlock_trywrlock(mrsw);
	if (err)
		return false;
	else
		return true;
}

static inline void
acquirewrite_mrsw(MultiReaderSingleWriter *mrsw)
{
	pthread_rwlock_wrlock(mrsw);
}

static inline void
acquirewrite_mrsw_long(MultiReaderSingleWriter *mrsw)
{
	/* XXX KDM long lock? */
	pthread_rwlock_wrlock(mrsw);
}

static inline void
releasewrite_mrsw(MultiReaderSingleWriter *mrsw)
{
	pthread_rwlock_unlock(mrsw);
}

static inline void
acquireread_mrsw(MultiReaderSingleWriter *mrsw)
{
	pthread_rwlock_rdlock(mrsw);
}

static inline int
acquireread_mrsw_short(MultiReaderSingleWriter *mrsw)
{
	int ret = 0;

	ret = pthread_rwlock_rdlock(mrsw);
	if (ret != 0)
		return -1;
	else
		return 0;
}

static inline void
releaseread_mrsw(MultiReaderSingleWriter *mrsw)
{
	pthread_rwlock_unlock(mrsw);
}

static inline void
release_mrsw(MultiReaderSingleWriter *mrsw)
{
	pthread_rwlock_unlock(mrsw);
}

//downgrades a write lock to a read lock
static inline void
writetoread_mrsw(MultiReaderSingleWriter *mrsw)
{
	/*
	 * The original intent of this function was to downgrade from write lock
	 * to read lock with higher priority than incoming write
	 * locks. pthread_rwlock doesn't provide this semantic,so we just
	 * release the lock and reacquire a reader lock on it. This demotion is
	 * only used by _ltfs_fsraw_write_data_unlocked() to release the write
	 * lock before returning. If there are pending writers on the volume
	 * lock at this point, they could prevent
	 * _ltfs_fsraw_write_data_unlocked)_ from returning immediately.
	 */
	pthread_rwlock_unlock(mrsw);
	pthread_rwlock_rdlock(mrsw);
}

#endif /* __FREEBSD_LOCKING_H__ */
