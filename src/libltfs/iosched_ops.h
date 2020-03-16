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
** FILE NAME:       iosched_ops.h
**
** DESCRIPTION:     Defines operations that must be supported by the I/O schedulers.
**
** AUTHOR:          Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
*************************************************************************************
*/
#ifndef __iosched_ops_h
#define __iosched_ops_h

#include "ltfs.h"

/**
 * iosched_ops structure.
 * Defines operations that must be supported by the I/O schedulers.
 */
struct iosched_ops {
	void    *(*init)(struct ltfs_volume *vol);
	int      (*destroy)(void *iosched_handle);
	int      (*open)(const char *path, bool open_write, struct dentry **dentry,
					 void *iosched_handle);
	int      (*close)(struct dentry *d, bool flush, void *iosched_handle);
	ssize_t  (*read)(struct dentry *d, char *buf, size_t size, off_t offset, void *iosched_handle);
	ssize_t  (*write)(struct dentry *d, const char *buf, size_t size, off_t offset,
					  bool isupdatetime, void *iosched_handle);
	int      (*flush)(struct dentry *d, bool closeflag, void *iosched_handle);
	int      (*truncate)(struct dentry *d, off_t length, void *iosched_handle);
	uint64_t (*get_filesize)(struct dentry *d, void *iosched_handle);
	int      (*update_data_placement)(struct dentry *d, void *iosched_handle);

	/**
	 * Enable profiler function
	 * @param work_dir work directory to store profiler data
	 * @paran enable enable or disable profiler function of this backend
	 * @param iosched_handle Handle to the I/O scheduler data.
	 * @return 0 on success or a negative value on error
	 */
	int   (*set_profiler)(char *work_dir, bool enable, void *iosched_handle);
};

struct iosched_ops *iosched_get_ops(void);
const char *iosched_get_message_bundle_name(void **message_data);

/**
 * Request type definisions for LTFS request profile
 */

#define REQ_IOS_OPEN        0000	/**< open */
#define REQ_IOS_CLOSE       0001	/**< close */
#define REQ_IOS_READ        0002	/**< read */
#define REQ_IOS_WRITE       0003	/**< write */
#define REQ_IOS_FLUSH       0004	/**< flush */
#define REQ_IOS_TRUNCATE    0005	/**< truncate */
#define REQ_IOS_GETFSIZE    0006	/**< get_filesize */
#define REQ_IOS_UPDPLACE    0007	/**< update_data_placement */
#define REQ_IOS_IOSCHED     0008	/**< (io_scheduler ... _unified_writer_thread) */
#define REQ_IOS_ENQUEUE_IP  0009	/**< Enqueue data block to IP */
#define REQ_IOS_DEQUEUE_IP  000A	/**< Dequeue data block to IP */
#define REQ_IOS_ENQUEUE_DP  000B	/**< Enqueue data block to DP */
#define REQ_IOS_DEQUEUE_DP  000C	/**< Dequeue data block to DP */

#endif /* __iosched_ops_h */
