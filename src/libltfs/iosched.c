/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2017 IBM Corp.
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
** FILE NAME:       iosched.c
**
** DESCRIPTION:     Implements the interface with the pluggable I/O schedulers.
**
** AUTHOR:          Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
** AUTHORS:         Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/
#include "ltfs_fuse.h"
#include "ltfs.h"
#include "iosched.h"

struct iosched_priv {
	void *dlopen_handle;           /**< Handle returned from dlopen */
	struct libltfs_plugin *plugin; /**< Reference to the plugin */
	struct iosched_ops *ops;       /**< I/O scheduler operations */
	void *backend_handle;          /**< Backend private data */
};

/**
 * Initialize the I/O scheduler.
 * @param plugin The plugin to take scheduler operations from.
 * @param vol LTFS volume
 * @return on success, 0 is returned and the I/O scheduler handle is stored in the ltfs_volume
 * structure. On failure a negative value is returned.
 */
int iosched_init(struct libltfs_plugin *plugin, struct ltfs_volume *vol)
{
	unsigned int i;
	struct iosched_priv *priv;

	CHECK_ARG_NULL(plugin, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);

	priv = calloc(1, sizeof(struct iosched_priv));
	if (! priv) {
		ltfsmsg(LTFS_ERR, 10001E, "iosched_init: private data");
		return -LTFS_NO_MEMORY;
	}

	priv->plugin = plugin;
	priv->ops = plugin->ops;

	/* Verify that backend implements all required operations */
	for (i=0; i<sizeof(struct iosched_ops)/sizeof(void *); ++i) {
		if (((void **)(priv->ops))[i] == NULL) {
			ltfsmsg(LTFS_ERR, 13003E);
			free(priv);
			return -LTFS_PLUGIN_INCOMPLETE;
		}
	}

	priv->backend_handle = priv->ops->init(vol);
	if (! priv->backend_handle) {
		free(priv);
		return -1;
	}

	vol->iosched_handle = priv;
	return 0;
}

/**
 * Destroy the I/O scheduler.
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
int iosched_destroy(struct ltfs_volume *vol)
{
	struct iosched_priv *priv = (struct iosched_priv *) vol ? vol->iosched_handle : NULL;
	int ret;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->destroy, -LTFS_NULL_ARG);

	ret = priv->ops->destroy(priv->backend_handle);
	vol->iosched_handle = NULL;
	free(priv);

	return ret;
}

/**
 * Open a file and create the I/O scheduler private data for a dentry
 * @param path the file to open
 * @param flags open flags
 * @param dentry on success, points to the dentry.
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
int iosched_open(const char *path, bool open_write, struct dentry **dentry, struct ltfs_volume *vol)
{
	struct iosched_priv *priv = (struct iosched_priv *) vol ? vol->iosched_handle : NULL;
	int ret;

	CHECK_ARG_NULL(path, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dentry, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->open, -LTFS_NULL_ARG);

	ret = priv->ops->open(path, open_write, dentry, priv->backend_handle);
	return ret;
}

/**
 * Close a dentry and destroy the I/O scheduler private data from a dentry if appropriate.
 * @param d dentry
 * @param flush true to force a flush before closing.
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
int iosched_close(struct dentry *d, bool flush, struct ltfs_volume *vol)
{
	struct iosched_priv *priv = (struct iosched_priv *) vol ? vol->iosched_handle : NULL;
	int ret;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->close, -LTFS_NULL_ARG);

	ret = priv->ops->close(d, flush, priv->backend_handle);
	return ret;
}

/**
 * Checks if the I/O scheduler has been initialized for the given volume
 * @param vol LTFS volume
 * @return true to indicate that the I/O scheduler has been initialized or false if not
 */
bool iosched_initialized(struct ltfs_volume *vol)
{
	CHECK_ARG_NULL(vol, false);
	return vol->iosched_handle;
}

/**
 * Read from tape through the I/O scheduler.
 * @param d dentry to read from
 * @param buf output data buffer
 * @param size output data buffer size
 * @param offset offset relative to the beginning of file to start reading from
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
ssize_t iosched_read(struct dentry *d, char *buf, size_t size, off_t offset, struct ltfs_volume *vol)
{
	ssize_t ret;
	struct iosched_priv *priv = (struct iosched_priv *) vol ? vol->iosched_handle : NULL;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->read, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);

	ret = priv->ops->read(d, buf, size, offset, priv->backend_handle);
	return ret;
}

/**
 * Write to tape through the I/O scheduler.
 * The caller must have a read
 * @param d dentry to write to
 * @param buf input data buffer
 * @param size input data length
 * @param offset offset relative to the beginning of file to start writing to
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
ssize_t iosched_write(struct dentry *d, const char *buf, size_t size, off_t offset,
	bool isupdatetime, struct ltfs_volume *vol)
{
	ssize_t ret;
	struct iosched_priv *priv = (struct iosched_priv *) vol ? vol->iosched_handle : NULL;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->write, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);

	ret = priv->ops->write(d, buf, size, offset, isupdatetime, priv->backend_handle);
	if (ret > 0 && (size_t) ret > size)
		ret = size;

	return ret;
}

/**
 * Flushes all pending operations to the tape.
 * @param d dentry to flush or NULL to flush all queued operations.
 * @param closeflag true if flushing before close(), false if not.
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
int iosched_flush(struct dentry *d, bool closeflag, struct ltfs_volume *vol)
{
	int ret;
	struct iosched_priv *priv = (struct iosched_priv *) vol ? vol->iosched_handle : NULL;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->flush, -LTFS_NULL_ARG);

	ret = priv->ops->flush(d, closeflag, priv->backend_handle);
	return ret;
}

/**
 * Change the length of a file. This may either shorten or lengthen the file.
 * @param d Dentry to truncate.
 * @param length Desired file size.
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
int iosched_truncate(struct dentry *d, off_t length, struct ltfs_volume *vol)
{
	int ret;
	struct iosched_priv *priv = (struct iosched_priv *) vol ? vol->iosched_handle : NULL;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->truncate, -LTFS_NULL_ARG);

	ret = priv->ops->truncate(d, length, priv->backend_handle);
	return ret;
}

/**
 * Ask the I/O scheduler what's the current size of the file represented
 * by the dentry @d. The returned value takes into account dirty buffers
 * which didn't reach the tape yet.
 *
 * @param d dentry to flush or NULL to flush all queued operations.
 * @param vol LTFS volume
 * @return the file size.
 */
uint64_t iosched_get_filesize(struct dentry *d, struct ltfs_volume *vol)
{
	uint64_t ret;
	struct iosched_priv *priv = (struct iosched_priv *) vol ? vol->iosched_handle : NULL;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->get_filesize, -LTFS_NULL_ARG);

	ret = priv->ops->get_filesize(d, priv->backend_handle);
	return ret;
}

/**
 * Update the data placement policy of data for a given dentry.
 *
 * @param d dentry
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
int iosched_update_data_placement(struct dentry *d, struct ltfs_volume *vol)
{
	int ret;
	struct iosched_priv *priv = (struct iosched_priv *) vol ? vol->iosched_handle : NULL;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->update_data_placement, -LTFS_NULL_ARG);

	ret = priv->ops->update_data_placement(d, priv->backend_handle);
	return ret;
}

/**
 * Enable profiler function
 * @param work_dir work directory to store profiler data
 * @param enable enable or disable profiler function of this backend
 * @param vol LTFS volume
 * @return 0 on succe	ss or a negative value on error
 */
int iosched_set_profiler(char* work_dir, bool enable, struct ltfs_volume *vol)
{
	int ret = 0;
	struct iosched_priv *priv = (struct iosched_priv *) vol ? vol->iosched_handle : NULL;

	CHECK_ARG_NULL(work_dir, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);

	if (priv) {
		CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
		CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
		CHECK_ARG_NULL(priv->ops->set_profiler, -LTFS_NULL_ARG);

		ret = priv->ops->set_profiler(work_dir, enable, priv->backend_handle);
	}

	return ret;
}
