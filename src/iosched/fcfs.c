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
** FILE NAME:       iosched/fcfs.c
**
** DESCRIPTION:     Implements the First-Come, First-Served I/O scheduler example.
**
** AUTHOR:          Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
*************************************************************************************
*/

#include "libltfs/ltfs.h"
#include "libltfs/ltfs_fsops_raw.h"
#include "ltfs_copyright.h"
#include "libltfs/iosched_ops.h"

volatile char *copyright = LTFS_COPYRIGHT_0"\n"LTFS_COPYRIGHT_1"\n"LTFS_COPYRIGHT_2"\n" \
	LTFS_COPYRIGHT_3"\n"LTFS_COPYRIGHT_4"\n"LTFS_COPYRIGHT_5"\n";

struct fcfs_data {
	ltfs_mutex_t sched_lock;    /**< Serializes read and write access */
	struct ltfs_volume *vol;    /**< A reference to the LTFS volume structure */
};

/**
 * Initialize the FCFS I/O scheduler.
 * @param vol LTFS volume
 * @return a pointer to the private data on success or NULL on error.
 */
void *fcfs_init(struct ltfs_volume *vol)
{
	int ret;
	struct fcfs_data *priv = calloc(1, sizeof(struct fcfs_data));
	if (! priv) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return NULL;
	}
	ret = ltfs_mutex_init(&priv->sched_lock);
	if (ret) {
		ltfsmsg(LTFS_ERR, 10002E, ret);
		free(priv);
		return NULL;
	}
	priv->vol = vol;
	ltfsmsg(LTFS_INFO, 13019I);
	return priv;
}

/**
 * Destroy the FCFS I/O scheduler.
 * @param iosched_handle the I/O scheduler handle
 * @return 0 on success or a negative value on error.
 */
int fcfs_destroy(void *iosched_handle)
{
	struct fcfs_data *priv = (struct fcfs_data *) iosched_handle;
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);

	ltfs_mutex_destroy(&priv->sched_lock);
	free(priv);
	ltfsmsg(LTFS_INFO, 13020I);
	return 0;
}

/**
 * Open a file and create the I/O scheduler private data for a dentry
 * @param path the file to open
 * @param open_write true if opening the file for write.
 * @param dentry on success, points to the dentry.
 * @param iosched_handle the I/O scheduler handle
 * @return 0 on success or a negative value on error.
 */
int fcfs_open(const char *path, bool open_write, struct dentry **dentry, void *iosched_handle)
{
	struct fcfs_data *priv = (struct fcfs_data *) iosched_handle;

	CHECK_ARG_NULL(path, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dentry, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);

	return ltfs_fsraw_open(path, open_write, dentry, priv->vol);
}

/**
 * Close a dentry and destroy the I/O scheduler private data from a dentry if appropriate.
 * @param d dentry
 * @param flush true to force a flush before closing.
 * @param iosched_handle the I/O scheduler handle
 * @return 0 on success or a negative value on error.
 */
int fcfs_close(struct dentry *d, bool flush, void *iosched_handle)
{
	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);

	return ltfs_fsraw_close(d);
}

/**
 * Read contents from tape.
 * The caller must NOT have a lock held on the dentry @d.
 *
 * @param d dentry to read from
 * @param buf output data buffer
 * @param size output data buffer size
 * @param offset offset relative to the beginning of file to start reading from
 * @param iosched_handle the I/O scheduler handle
 * @return the number of bytes read or a negative value on error
 */
ssize_t fcfs_read(struct dentry *d, char *buf, size_t size, off_t offset, void *iosched_handle)
{
	struct fcfs_data *priv = (struct fcfs_data *) iosched_handle;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(buf, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);

	return ltfs_fsraw_read(d, buf, size, offset, priv->vol);
}

/**
 * Enqueue a write request to the tape.
 * The caller must NOT have a lock held on the dentry @d.
 *
 * @param d dentry to write to
 * @param buf input data buffer
 * @param size input data length
 * @param offset offset relative to the beginning of file to start writing to
 * @param iosched_handle the I/O scheduler handle
 * @return the number of bytes enqueued for writing or a negative value on error
 */
ssize_t fcfs_write(struct dentry *d, const char *buf, size_t size, off_t offset,
				   bool isupdatetime, void *iosched_handle)
{
	struct fcfs_data *priv = (struct fcfs_data *) iosched_handle;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(buf, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);

	return ltfs_fsraw_write(d, buf, size, offset, ltfs_dp_id(priv->vol), true, priv->vol);
}

/**
 * Forces all pending operations to meet the tape.
 *
 * @param d dentry to flush or NULL to flush all queued operations.
 * @param closeflag true if flushing before close(), false if not.
 * @param iosched_handle the I/O scheduler handle.
 * @return 0 on success or a negative value on error.
 */
int fcfs_flush(struct dentry *d, bool closeflag, void *iosched_handle)
{
	(void) closeflag;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);

	return 0;
}

int fcfs_truncate(struct dentry *d, off_t length, void *iosched_handle)
{
	struct fcfs_data *priv = (struct fcfs_data *) iosched_handle;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);

	return ltfs_fsraw_truncate(d, length, priv->vol);
}

/**
 * Get the file size, considering data stored in working buffers.
 *
 * @param d dentry to flush or NULL to flush all queued operations.
 * @param iosched_handle the I/O scheduler handle.
 * @return the file size.
 */
uint64_t fcfs_get_filesize(struct dentry *d, void *iosched_handle)
{
	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);

	return d->size;
}

/**
 * Update the data placement policy of data for a given dentry.
 *
 * @param d dentry
 * @param iosched_handle the I/O scheduler handle.
 * @return 0 on success or a negative value on error.
 */
int fcfs_update_data_placement(struct dentry *d, void *iosched_handle)
{
	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(iosched_handle, -LTFS_NULL_ARG);

	return 0;
}

/**
 * Enable profiler function
 * @param work_dir work directory to store profiler data
 * @paran enable enable or disable profiler function of this backend
 * @param iosched_handle Handle to the I/O scheduler data.
 * @return 0 on success or a negative value on error
 */
int fcfs_set_profiler(char *work_dir, bool enable, void *iosched_handle)
{
	/* Do nothing */
	return 0;
}

struct iosched_ops fcfs_ops = {
	.init         = fcfs_init,
	.destroy      = fcfs_destroy,
	.open         = fcfs_open,
	.close        = fcfs_close,
	.read         = fcfs_read,
	.write        = fcfs_write,
	.flush        = fcfs_flush,
	.truncate     = fcfs_truncate,
	.get_filesize = fcfs_get_filesize,
	.update_data_placement = fcfs_update_data_placement,
	.set_profiler = fcfs_set_profiler,
};

struct iosched_ops *iosched_get_ops(void)
{
	return &fcfs_ops;
}

#ifndef mingw_PLATFORM
extern char iosched_fcfs_dat[];
#endif

const char *iosched_get_message_bundle_name(void **message_data)
{
#ifndef mingw_PLATFORM
	*message_data = iosched_fcfs_dat;
#else
	*message_data = NULL;
#endif
	return "iosched_fcfs";
}
