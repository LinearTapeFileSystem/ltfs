/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2022 IBM Corp. All rights reserved.
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
** FILE NAME:       ltfs_fuse.c
**
** DESCRIPTION:     Implements the interface of LTFS with FUSE.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
**                  Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
**                  Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/

#include <limits.h> /* for ULONG_MAX */

#include "ltfs_fuse.h"
#include "libltfs/ltfs_fsops.h"
#include "libltfs/iosched.h"
#include "libltfs/pathname.h"
#include "libltfs/xattr.h"
#include "libltfs/periodic_sync.h"
#include "libltfs/arch/time_internal.h"
#include "libltfs/arch/errormap.h"
#include "libltfs/kmi.h"

#ifdef mingw_PLATFORM
#include "arch/win/win_util.h"
#endif

#if (__WORDSIZE == 64 || ULONG_MAX == 0xffffffffffffffffUL)
#define FILEHANDLE_TO_STRUCT(fh) ((struct ltfs_file_handle *)(uint64_t)(fh))
#define STRUCT_TO_FILEHANDLE(de) ((uint64_t)(de))
#else
#define FILEHANDLE_TO_STRUCT(fh) ((struct ltfs_file_handle *)(uint32_t)(fh))
#define STRUCT_TO_FILEHANDLE(de) ((uint64_t)(uint32_t)(de))
#endif

#ifdef mingw_PLATFORM
static struct fuse_context *context;
#define fuse_get_context() context
#endif

#define FUSE_REQ_ENTER(r)   REQ_NUMBER(REQ_STAT_ENTER, REQ_FUSE, r)
#define FUSE_REQ_EXIT(r)    REQ_NUMBER(REQ_STAT_EXIT,  REQ_FUSE, r)

struct ltfs_file_handle *_new_ltfs_file_handle(struct file_info *fi)
{
	int ret;
	struct ltfs_file_handle *file = calloc(1, sizeof(struct ltfs_file_handle));
	if (! file) {
		ltfsmsg(LTFS_ERR, 10001E, "file structure");
		return NULL;
	}
	ret = ltfs_mutex_init(&file->lock);
	if (ret) {
		ltfsmsg(LTFS_ERR, 10002E, ret);
		free(file);
		return NULL;
	}
	file->file_info = fi;
	file->dirty = false;
	return file;
}

void _free_ltfs_file_handle(struct ltfs_file_handle *file)
{
	if (file) {
		ltfs_mutex_destroy(&file->lock);
		free(file);
	}
}

static struct file_info *_new_file_info(const char *path)
{
	int ret;
	struct file_info *fi = calloc(1, sizeof(struct file_info));
	if (! fi) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return NULL;
	}
	ret = ltfs_mutex_init(&fi->lock);
	if (ret) {
		ltfsmsg(LTFS_ERR, 10002E, ret);
		free(fi);
		return NULL;
	}
	if (path) {
		fi->path = SAFE_STRDUP(path);
		if (! fi->path) {
			ltfsmsg(LTFS_ERR, 10001E, "_new_file_info: path");
			ltfs_mutex_destroy(&fi->lock);
			free(fi);
			return NULL;
		}
	}
	fi->open_count = 1;
	return fi;
}

static void _free_file_info(struct file_info *fi)
{
	if (fi) {
		if (fi->path)
			free(fi->path);
		ltfs_mutex_destroy(&fi->lock);
		free(fi);
	}
}

/**
 * Retrieve file handle information for a dentry.
 * If no handle information exists, it is allocated and saved.
 * The open_file structure returned from this function should be released later using
 * _file_close().
 * @param path Path used to open this file. May be NULL.
 * @param d File handle to get information for. If NULL, a dummy handle information structure
 *          is returned.
 * @param spare A preallocated open_file structure. If present, it will be used instead of
 *              allocating memory. May be NULL.
 * @param priv LTFS private data.
 * @return File handle information, or NULL if memory allocation failed or if 'priv' is NULL.
 */
static struct file_info *_file_open(const char *path, void *d, struct file_info *spare,
	struct ltfs_fuse_data *priv)
{
	struct file_info *fi = NULL;
	CHECK_ARG_NULL(priv, NULL);
	ltfs_mutex_lock(&priv->file_table_lock);
	if (priv->file_table)
		HASH_FIND_PTR(priv->file_table, &d, fi);
	if (! fi) {
		fi = spare ? spare : _new_file_info(path);
		if (! fi) {
			ltfs_mutex_unlock(&priv->file_table_lock);
			return NULL;
		}
		fi->dentry_handle = d;
		HASH_ADD_PTR(priv->file_table, dentry_handle, fi);
	} else {
		ltfs_mutex_lock(&fi->lock);
		fi->open_count++;
		ltfs_mutex_unlock(&fi->lock);
	}
	ltfs_mutex_unlock(&priv->file_table_lock);
	return fi;
}

/**
 * Release a file_info structure obtained using _file_open().
 * The file_info structure is freed if there are no references left.
 */
static void _file_close(struct file_info *fi, struct ltfs_fuse_data *priv)
{
	bool do_free = false;
	if (fi && priv) {
		ltfs_mutex_lock(&priv->file_table_lock);
		ltfs_mutex_lock(&fi->lock);
		fi->open_count--;
		if (fi->open_count == 0) {
			HASH_DEL(priv->file_table, fi);
			do_free = true;
		}
		ltfs_mutex_unlock(&fi->lock);
		ltfs_mutex_unlock(&priv->file_table_lock);
		if (do_free)
			_free_file_info(fi);
	}
}

const char *_dentry_name(const char *path, struct file_info *fi)
{
	if (path)
		return path;
	else if (fi->path)
		return fi->path;
	else
		return "(unnamed)";
}

static void _ltfs_fuse_attr_to_stat(struct stat *stbuf, struct dentry_attr *attr,
	struct ltfs_fuse_data *priv)
{
	stbuf->st_dev = LTFS_SUPER_MAGIC;
	stbuf->st_ino = attr->uid;
	if (attr->isslink) {
		stbuf->st_mode = S_IFLNK | 0777;
	} else {
		stbuf->st_mode = ((attr->isdir ? S_IFDIR : S_IFREG) | (attr->readonly ? 0555 : 0777)) &
			(attr->isdir ? priv->dir_mode : priv->file_mode);
	}
	stbuf->st_nlink = attr->nlink;
	stbuf->st_rdev = 0; /* no special files on LTFS volumes */
	if (priv->perm_override) {
		stbuf->st_uid = priv->mount_uid;
		stbuf->st_gid = priv->mount_gid;
	} else {
		stbuf->st_uid = fuse_get_context()->uid;
		stbuf->st_gid = fuse_get_context()->gid;
	}
	stbuf->st_size = attr->size;
	stbuf->st_blksize = attr->blocksize;
	stbuf->st_blocks = (attr->alloc_size + 511) / 512; /* this field is in 512-byte units */

#ifdef __APPLE__
	stbuf->st_atimespec = timespec_from_ltfs_timespec(&attr->access_time);
	stbuf->st_mtimespec = timespec_from_ltfs_timespec(&attr->modify_time);
	stbuf->st_ctimespec = timespec_from_ltfs_timespec(&attr->change_time);
	#ifdef _DARWIN_USE_64_BIT_INODE
	stbuf->st_birthtimespec = timespec_from_ltfs_timespec(&attr->create_time);
	#endif
#else
	stbuf->st_atim = timespec_from_ltfs_timespec(&attr->access_time);
	stbuf->st_mtim = timespec_from_ltfs_timespec(&attr->modify_time);
	stbuf->st_ctim = timespec_from_ltfs_timespec(&attr->change_time);
#endif
}

int ltfs_fuse_fgetattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_file_handle *file = FILEHANDLE_TO_STRUCT(fi->fh);
	struct dentry_attr attr;
	int ret;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_FGETATTR), (uint64_t)fi, 0);

	ltfsmsg(LTFS_DEBUG3, 14030D, _dentry_name(path, file->file_info));

	ret = ltfs_fsops_getattr(file->file_info->dentry_handle, &attr, priv->data);

	if (ret == 0)
		_ltfs_fuse_attr_to_stat(stbuf, &attr, priv);

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_FGETATTR), ret,
					   ((struct dentry *)(file->file_info->dentry_handle))->uid);

	return errormap_fuse_error(ret);
}

int ltfs_fuse_getattr(const char *path, struct stat *stbuf)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct dentry_attr attr;
	ltfs_file_id id;
	int ret;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_GETATTR), 0, 0);

	ltfsmsg(LTFS_DEBUG3, 14031D, path);

	ret = ltfs_fsops_getattr_path(path, &attr, &id, priv->data);

	if (ret == 0)
		_ltfs_fuse_attr_to_stat(stbuf, &attr, priv);

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_GETATTR), ret, id.uid);

	return errormap_fuse_error(ret);
}


int ltfs_fuse_access(const char *path, int mode)
{
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_ACCESS), 0, 0);
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_ACCESS), 0, 0);
	return 0;
}

int ltfs_fuse_statfs(const char *path, struct statvfs *buf)
{
#ifndef mingw_PLATFORM
	int ret;
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct statvfs *stats = &priv->fs_stats;
	struct device_capacity blockstat;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_STATFS), 0, 0);

	memset(&blockstat, 0, sizeof(blockstat));

	ret = ltfs_capacity_data(&blockstat, priv->data);
	if (ret < 0) {
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_STATFS), ret, 0);
		return errormap_fuse_error(ret);
	}

	stats->f_blocks = blockstat.total_dp;           /* Total tape capacity */
	stats->f_bfree = blockstat.remaining_dp;        /* Remaining tape capacity */
	stats->f_bavail = stats->f_bfree;               /* Blocks available for normal user (ignored) */

#ifdef __APPLE__
	stats->f_files = UINT_MAX;
	stats->f_ffree = UINT_MAX - ltfs_get_file_count(priv->data);
	memcpy(buf, stats, sizeof(struct statvfs));

	/* With MacFUSE, we use an f_frsize not equal to the file system block size.
	 * Need to adjust the block counts so they're in units of the reported f_frsize. */
	double scale = ltfs_get_blocksize(priv->data) / (double)stats->f_frsize;
	buf->f_blocks *= scale;
	buf->f_bfree  *= scale;
	buf->f_bavail *= scale;
#else
	stats->f_files = UINT64_MAX;
	stats->f_ffree = UINT64_MAX - ltfs_get_file_count(priv->data);
	memcpy(buf, stats, sizeof(struct statvfs));
#endif /* __APPLE__ */

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_STATFS), 0, 0);

#endif /* mingw_PLATFORM */

	return 0;
}

int ltfs_fuse_open(const char *path, struct fuse_file_info *fi)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_file_handle *file;
	struct file_info *file_info;
	void *dentry_handle;
	int ret;
	bool open_write;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_OPEN), (uint64_t)fi->flags, 0);

	if ((fi->flags & O_WRONLY) == O_WRONLY)
		ltfsmsg(LTFS_DEBUG, 14032D, path, "write-only");
	else if ((fi->flags & O_RDWR) == O_RDWR)
		ltfsmsg(LTFS_DEBUG, 14032D, path, "read-write");
	else /* read-only */
		ltfsmsg(LTFS_DEBUG, 14032D, path, "read-only");
	open_write = (((fi->flags & O_WRONLY) == O_WRONLY) || ((fi->flags & O_RDWR) == O_RDWR));

	/* Open the file */
	ret = ltfs_fsops_open(path, open_write, true, (struct dentry **)&dentry_handle, priv->data);
	if (ret < 0) {
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_OPEN), ret, 0);
		return errormap_fuse_error(ret);
	}

	/* Get file information and create a file handle */
	file_info = _file_open(path, dentry_handle, NULL, priv);
	if (file_info)
		file = _new_ltfs_file_handle(file_info);
	if (! file_info || ! file) {
		if (file_info)
			_file_close(file_info, priv);
		ltfs_fsops_close(dentry_handle, false, open_write, true, priv->data);
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_OPEN), -ENOMEM, 0);
		return -ENOMEM;
	}

	fi->fh = STRUCT_TO_FILEHANDLE(file);

#ifdef __APPLE__
    /* Comment from MacFUSE author about direct_io on OSX:
     * direct_io is a rather abnormal mode of operation from Mac OS X's
     * standpoint. Unless your file system requires this mode, I wouldn't
     * recommend using this option.
     */
    fi->direct_io  = 0;
    fi->keep_cache = 0;
#else
#if FUSE_VERSION <= 27
	/* for FUSE <= 2.7, set direct_io when opening for write */
	if (((fi->flags & O_WRONLY) == O_WRONLY) || ((fi->flags & O_RDWR) == O_RDWR))
		fi->direct_io = 1;
	fi->keep_cache = 0;
#else
	/* cannot set keep cache if any process has the file open with direct_io set! so only
	 * set it on newer FUSE versions, where we don't use direct_io. */
	fi->direct_io = 0;
	fi->keep_cache = 1;
#endif
#endif

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_OPEN), 0,
					   ((struct dentry *)(file->file_info->dentry_handle))->uid);

	return 0;
}

int ltfs_fuse_release(const char *path, struct fuse_file_info *fi)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_file_handle *file = FILEHANDLE_TO_STRUCT(fi->fh);
	bool dirty, write_index, open_write;
	uint64_t uid;
	int ret;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_RELEASE), 0, 0);

	ltfsmsg(LTFS_DEBUG, 14035D, _dentry_name(path, file->file_info));

	uid = ((struct dentry *)(file->file_info->dentry_handle))->uid;

	/* Should this file's buffers be flushed? */
	ltfs_mutex_lock(&file->lock);
	dirty = file->dirty;
	ltfs_mutex_unlock(&file->lock);

	/* Should an index be written? */
	ltfs_mutex_lock(&file->file_info->lock);
	write_index = (priv->sync_type == LTFS_SYNC_CLOSE) ? file->file_info->write_index : false;
	ltfs_mutex_unlock(&file->file_info->lock);

	open_write = (((fi->flags & O_WRONLY) == O_WRONLY) || ((fi->flags & O_RDWR) == O_RDWR));
	ret = ltfs_fsops_close(file->file_info->dentry_handle, dirty, open_write, true, priv->data);
	if (write_index) {
		ltfs_set_commit_message_reason(SYNC_CLOSE, priv->data);
		ltfs_sync_index(SYNC_CLOSE, true, LTFS_INDEX_AUTO, priv->data);
	}

	_file_close(file->file_info, priv);
	_free_ltfs_file_handle(file);

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_RELEASE), ret, uid);

	return errormap_fuse_error(ret);
}

int ltfs_fuse_opendir(const char *path, struct fuse_file_info *fi)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_file_handle *file;
	struct file_info *file_info;
	void *dentry_handle;
	int ret;
	bool open_write;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_OPENDIR), (uint64_t)fi->flags, 0);

	ltfsmsg(LTFS_DEBUG, 14033D, path);

	open_write = (((fi->flags & O_WRONLY) == O_WRONLY) || ((fi->flags & O_RDWR) == O_RDWR));

	/* Open the file */
	ret = ltfs_fsops_open(path, open_write, false, (struct dentry **)&dentry_handle,
						  priv->data);
	if (ret < 0) {
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_OPENDIR), ret, 0);
		return errormap_fuse_error(ret);
	}

	/* Get file information and create a file handle */
	file_info = _file_open(path, dentry_handle, NULL, priv);
	if (file_info)
		file = _new_ltfs_file_handle(file_info);
	if (! file_info || ! file) {
		if (file_info)
			_file_close(file_info, priv);
		ltfs_fsops_close(dentry_handle, false, false, false, priv->data);
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_OPENDIR), -ENOMEM, 0);
		return -ENOMEM;
	}

	fi->fh = STRUCT_TO_FILEHANDLE(file);

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_OPENDIR), 0,
					   ((struct dentry *)(file->file_info->dentry_handle))->uid);

	return 0;
}

int ltfs_fuse_releasedir(const char *path, struct fuse_file_info *fi)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_file_handle *file = FILEHANDLE_TO_STRUCT(fi->fh);
	uint64_t uid;
	int ret;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_RELEASEDIR), 0, 0);

	ltfsmsg(LTFS_DEBUG, 14034D, _dentry_name(path, file->file_info));

	uid = ((struct dentry *)(file->file_info->dentry_handle))->uid;

	ret = ltfs_fsops_close(file->file_info->dentry_handle, false, false, false, priv->data);

	_file_close(file->file_info, priv);
	_free_ltfs_file_handle(file);
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_RELEASEDIR), ret, uid);

	return errormap_fuse_error(ret);
}

/* TODO: treat this like a regular fsync? */
int ltfs_fuse_fsyncdir(const char *path, int flags, struct fuse_file_info *fi)
{
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_FSYNCDIR), 0, 0);
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_FSYNCDIR), 0, 0);
	return 0;
}

static int _ltfs_fuse_do_flush(struct ltfs_file_handle *file, struct ltfs_fuse_data *priv,
	const char *caller)
{
	bool dirty;
	int ret = 0;

	ltfs_mutex_lock(&file->lock);
	dirty = file->dirty;
	ltfs_mutex_unlock(&file->lock);

	if (dirty) {
		ret = ltfs_fsops_flush(file->file_info->dentry_handle, false, priv->data);
		if (ret < 0)
			ltfsmsg(LTFS_ERR, 14022E, caller);
		else {
			ltfs_mutex_lock(&file->lock);
			file->dirty = false;
			ltfs_mutex_unlock(&file->lock);
		}
	}

	return errormap_fuse_error(ret);
}

int ltfs_fuse_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_file_handle *file = FILEHANDLE_TO_STRUCT(fi->fh);
	uint64_t uid;
	int ret;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_FSYNC), (uint64_t)isdatasync, 0);

	ltfsmsg(LTFS_DEBUG, 14036D, _dentry_name(path, file->file_info));
	uid = ((struct dentry *)(file->file_info->dentry_handle))->uid;
	ret = _ltfs_fuse_do_flush(file, priv, __FUNCTION__);

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_FSYNC), ret, uid);

	return errormap_fuse_error(ret);
}

int ltfs_fuse_flush(const char *path, struct fuse_file_info *fi)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_file_handle *file = FILEHANDLE_TO_STRUCT(fi->fh);
	uint64_t uid;
	int ret;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_FLUSH), 0, 0);

	ltfsmsg(LTFS_DEBUG, 14037D, _dentry_name(path, file->file_info));
	uid = ((struct dentry *)(file->file_info->dentry_handle))->uid;
	ret = _ltfs_fuse_do_flush(file, priv, __FUNCTION__);

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_FLUSH), ret, uid);

	return errormap_fuse_error(ret);
}

int ltfs_fuse_utimens(const char *path, const struct timespec ts[2])
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_timespec tsTmp[2];
	ltfs_file_id id;
	int ret;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_UTIMENS), 0, 0);

	tsTmp[0] = ltfs_timespec_from_timespec(&ts[0]);
	tsTmp[1] = ltfs_timespec_from_timespec(&ts[1]);

	ltfsmsg(LTFS_DEBUG, 14038D, path);
	ret = ltfs_fsops_utimens_path(path, tsTmp, &id, priv->data);

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_UTIMENS), ret, id.uid);

	if (ret)
		ltfsmsg(LTFS_ERR, 10020E, "utimens", path, 0, 0);

	return errormap_fuse_error(ret);
}

/**
 * Change the mode of a file or directory. Since LTFS does not support full Unix permissions,
 * this function just sets or clears the read-only flag.
 */
int ltfs_fuse_chmod(const char *path, mode_t mode)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	ltfs_file_id id;
	int ret;
	bool new_readonly = (mode & 0222) ? false : true;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_CHMOD), (uint64_t)mode, 0);

	ltfsmsg(LTFS_DEBUG, 14039D, path);
	ret = ltfs_fsops_set_readonly_path(path, new_readonly, &id, priv->data);

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_CHMOD), ret, id.uid);

	if (ret)
		ltfsmsg(LTFS_ERR, 10020E, "chmod", path, mode, 0);

	return errormap_fuse_error(ret);
}

/**
 * Set ownership of a file or directory. Succeeds, but has no effect: user/group are
 * controlled by mount-time options uid and gid.
 */
int ltfs_fuse_chown(const char *path, uid_t user, gid_t group)
{
	ltfs_request_trace(FUSE_REQ_ENTER(REQ_CHOWN), ((uint64_t)user << 32) + group, 0);
	ltfs_request_trace(FUSE_REQ_EXIT(REQ_CHOWN), 0, 0);
	return 0;
}

int ltfs_fuse_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_file_handle *file;
	struct file_info *file_info, *new_file_info;
	void *dentry_handle; /* might be a dentry or a dentry_proxy */
	bool readonly;
	bool overwrite;
	int ret;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_CREATE), (uint64_t)fi->flags, 0);

	ltfsmsg(LTFS_DEBUG, 14040D, path);

	readonly = ! (mode & priv->file_mode & 0222);

	/* Allocate file handle and information */
	file = _new_ltfs_file_handle(NULL);
	if (! file) {
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_CREATE), -ENOMEM, 0);
		return -ENOMEM;
	}
	file_info = _new_file_info(path);
	if (! file_info) {
		_free_ltfs_file_handle(file);
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_CREATE), -ENOMEM, 1);
		return -ENOMEM;
	}

	overwrite = ((!(fi->flags & O_RDONLY) && !(fi->flags & O_APPEND) && !(fi->flags & O_NONBLOCK)) || (fi->flags & O_TRUNC));

	/* Create the file */
	ret = ltfs_fsops_create(path, false, readonly, overwrite, (struct dentry **)&dentry_handle, priv->data);
	if (ret < 0) {
		_free_file_info(file_info);
		_free_ltfs_file_handle(file);
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_CREATE), ret, 0);
		return errormap_fuse_error(ret);
	}

	/* Save handle */
	new_file_info = _file_open(path, dentry_handle, file_info, priv);
	if (file_info != new_file_info)
		_free_file_info(file_info);
	file->file_info = new_file_info;

	fi->fh = STRUCT_TO_FILEHANDLE(file);

#ifdef __APPLE__
    /* Comment from MacFUSE author about direct_io on OSX:
     * direct_io is a rather abnormal mode of operation from Mac OS X's
     * standpoint. Unless your file system requires this mode, I wouldn't
     * recommend using this option.
     */
    fi->direct_io  = 0;
    fi->keep_cache = 0;
#else
#if FUSE_VERSION <= 27
	/* for FUSE <= 2.7, set direct_io when creating */
	fi->direct_io = 1;
	fi->keep_cache = 0;
#else
	/* cannot set keep cache if any process has the file open with direct_io set! so only
	 * set it on newer FUSE versions, where we don't use direct_io. */
	fi->direct_io = 0;
	fi->keep_cache = 1;
#endif
#endif

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_CREATE), 0,
					   ((struct dentry *)(file->file_info->dentry_handle))->uid);

	return 0;
}

int ltfs_fuse_mkdir(const char *path, mode_t mode)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	void *dentry_handle;
	uint64_t uid = 0;
	int ret;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_MKDIR), (uint64_t)mode, 0);

	ltfsmsg(LTFS_DEBUG, 14041D, path);

	ret = ltfs_fsops_create(path, true, false, false, (struct dentry **)&dentry_handle, priv->data);
	if (ret == 0) {
		uid = ((struct dentry *)dentry_handle)->uid;
		ltfs_fsops_close(dentry_handle, false, false, false, priv->data);
	}

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_MKDIR), ret, uid);

	return errormap_fuse_error(ret);
}

int ltfs_fuse_truncate(const char *path, off_t length)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	ltfs_file_id id;
	int ret;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_TRUNCATE), (uint64_t)length, 0);

	ltfsmsg(LTFS_DEBUG, 14042D, path, (long long)length);

	ret = ltfs_fsops_truncate_path(path, length, &id, priv->data);

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_TRUNCATE), ret, id.uid);

	return errormap_fuse_error(ret);
}

int ltfs_fuse_ftruncate(const char *path, off_t length, struct fuse_file_info *fi)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_file_handle *file = FILEHANDLE_TO_STRUCT(fi->fh);
	int ret;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_FTRUNCATE), (uint64_t)length, 0);

	ltfsmsg(LTFS_DEBUG, 14043D, _dentry_name(path, file->file_info), (long long) length);

	ret = ltfs_fsops_truncate(file->file_info->dentry_handle, length, priv->data);

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_FTRUNCATE), ret,
					   ((struct dentry *)(file->file_info->dentry_handle))->uid);

	return errormap_fuse_error(ret);
}

int ltfs_fuse_unlink(const char *path)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	ltfs_file_id id;
	int ret;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_UNLINK), 0, 0);

	ltfsmsg(LTFS_DEBUG, 14044D, path);

	ret = ltfs_fsops_unlink(path, &id, priv->data);

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_UNLINK), ret, id.uid);

	return errormap_fuse_error(ret);
}

int ltfs_fuse_rmdir(const char *path)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	ltfs_file_id id;
	int ret;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_RMDIR), 0, 0);

	ltfsmsg(LTFS_DEBUG, 14045D, path);

	ret = ltfs_fsops_unlink(path, &id, priv->data);

 	ltfs_request_trace(FUSE_REQ_EXIT(REQ_RMDIR), ret, id.uid);

	return errormap_fuse_error(ret);
}

int ltfs_fuse_rename(const char *from, const char *to)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	ltfs_file_id id;
	int ret;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_RENAME), 0, 0);

	ltfsmsg(LTFS_DEBUG, 14046D, from, to);

	ret = ltfs_fsops_rename(from, to, &id, priv->data);

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_RENAME), ret, id.uid);

	return errormap_fuse_error(ret);
}

int _ltfs_fuse_filldir(void *buf, const char *name, void *priv)
{
	int ret;
	char *new_name;
	fuse_fill_dir_t filler = priv;

	ret = pathname_unformat(name, &new_name);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 14027E, "unformat", ret);
		return ret;
	}

#ifdef __APPLE__
	free(new_name);

	ret = pathname_nfd_normalize(name, &new_name);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 14027E, "nfd", ret);
		return ret;
	}

	ret = filler(buf, new_name, NULL, 0);
#else
	ret = filler(buf, name, NULL, 0);
#endif

	free(new_name);
	if (ret)
		return -ENOBUFS;
	return 0;
}

int ltfs_fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
	off_t offset, struct fuse_file_info *fi)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_file_handle *file = FILEHANDLE_TO_STRUCT(fi->fh);
	int ret;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_READDIR), (uint64_t)offset, 0);

	ltfsmsg(LTFS_DEBUG, 14047D, _dentry_name(path, file->file_info));

	if (filler(buf, ".",  NULL, 0)) {
		/* No buffer space */
		ltfsmsg(LTFS_DEBUG, 14026D);
		return -ENOBUFS;
	}
	if (filler(buf, "..", NULL, 0)) {
		/* No buffer space */
		ltfsmsg(LTFS_DEBUG, 14026D);
		return -ENOBUFS;
	}

	ret = ltfs_fsops_readdir(file->file_info->dentry_handle, buf, _ltfs_fuse_filldir,
							 filler, priv->data);

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_READDIR), ret,
					   ((struct dentry *)(file->file_info->dentry_handle))->uid);

	return errormap_fuse_error(ret);
}

int ltfs_fuse_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_file_handle *file = FILEHANDLE_TO_STRUCT(fi->fh);
	int ret;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_WRITE), (uint64_t)offset, (uint64_t)size);

	ltfsmsg(LTFS_DEBUG3, 14048D, _dentry_name(path, file->file_info), (long long)offset, size);

	ret = ltfs_fsops_write(file->file_info->dentry_handle, buf, size, offset, true, priv->data);

	if (ret == 0) {
		ltfs_mutex_lock(&file->lock);
		file->dirty = true;
		ltfs_mutex_unlock(&file->lock);

		ltfs_mutex_lock(&file->file_info->lock);
		file->file_info->write_index = true;
		ltfs_mutex_unlock(&file->file_info->lock);

		ltfs_request_trace(FUSE_REQ_EXIT(REQ_WRITE), (uint64_t)size,
						   ((struct dentry *)(file->file_info->dentry_handle))->uid);

		return size;
	} else {
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_WRITE), (uint64_t)ret,
						   ((struct dentry *)(file->file_info->dentry_handle))->uid);
		return errormap_fuse_error(ret);
	}
}

int ltfs_fuse_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct ltfs_file_handle *file = FILEHANDLE_TO_STRUCT(fi->fh);
	int ret;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_READ), (uint64_t)offset, (uint64_t)size);

	ltfsmsg(LTFS_DEBUG3, 14049D, _dentry_name(path, file->file_info), (long long)offset, size);

	ret = ltfs_fsops_read(file->file_info->dentry_handle, buf, size, offset, priv->data);

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_READ), (uint64_t)ret,
					   ((struct dentry *)(file->file_info->dentry_handle))->uid);

	return errormap_fuse_error(ret);
}

#ifdef __APPLE__
int ltfs_fuse_setxattr(const char *path, const char *name, const char *value, size_t size,
	int flags, uint32_t position)
#else
int ltfs_fuse_setxattr(const char *path, const char *name, const char *value, size_t size,
	int flags)
#endif /* __APPLE__ */
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	ltfs_file_id id;
	int ret;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_SETXATTR), (uint64_t)size, 0);

	ltfsmsg(LTFS_DEBUG3, 14050D, path, name, size);

	/* position argument is only supported for resource forks
	 * on OS X, and we have no resource forks
	 * TODO: is it correct to behave this way?
	 */
#ifdef __APPLE__
	if (position) {
		/* Position argument must be zero */
		ltfsmsg(LTFS_ERR, 14023E);
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_SETXATTR), -EINVAL, 0);
		return -EINVAL;
	}
#endif /* __APPLE__ */

	ret = ltfs_fsops_setxattr(path, name, value, size, flags, &id, priv->data);

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_SETXATTR), ret, id.uid);

	return errormap_fuse_error(ret);
}

#ifdef __APPLE__
int ltfs_fuse_getxattr(const char *path, const char *name, char *value, size_t size,
	uint32_t position)
#else
int ltfs_fuse_getxattr(const char *path, const char *name, char *value, size_t size)
#endif /* __APPLE__ */
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	ltfs_file_id id;
	int ret;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_GETXATTR), (uint64_t)size, 0);

	ltfsmsg(LTFS_DEBUG3, 14051D, path, name);

	/* position argument is only supported for resource forks
	 * on OS X, and we have no resource forks
	 * TODO: is it correct to behave this way?
	 */
#ifdef __APPLE__
	if (position) {
		/* Position argument must be zero */
		ltfsmsg(LTFS_ERR, 14024E);
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_GETXATTR), -EINVAL, 0);
		return -EINVAL;
	}
#else
	/* Short-circuit requests for system EAs to avoid mounting the same unnecessarily in
	 * library mode. */
	if (strstr(name, "system.") == name || strstr(name, "security.") == name) {
		ltfs_request_trace(FUSE_REQ_EXIT(REQ_GETXATTR), -LTFS_NO_XATTR, 0);
		return errormap_fuse_error(-LTFS_NO_XATTR);
	}
#endif /* __APPLE__ */

	ret = ltfs_fsops_getxattr(path, name, value, size, &id, priv->data);

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_GETXATTR), ret, id.uid);

	return errormap_fuse_error(ret);
}

int ltfs_fuse_listxattr(const char *path, char *list, size_t size)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	ltfs_file_id id;
	int ret;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_LISTXATTR), (uint64_t)size, 0);

	ltfsmsg(LTFS_DEBUG, 14052D, path);

	ret = ltfs_fsops_listxattr(path, list, size, &id, priv->data);

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_LISTXATTR), ret, id.uid);

	return errormap_fuse_error(ret);
}

int ltfs_fuse_removexattr(const char *path, const char *name)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	ltfs_file_id id;
	int ret;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_REMOVEXATTR), 0, 0);

	ltfsmsg(LTFS_DEBUG, 14053D, path, name);

	ret = ltfs_fsops_removexattr(path, name, &id, priv->data);

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_REMOVEXATTR), ret, id.uid);

	return errormap_fuse_error(ret);
}

/**
 * Mount the filesystem. This function assumes a volume has been
 * allocated and ltfs_mount has been called; it just does some secondary setup.
 */
void * ltfs_fuse_mount(struct fuse_conn_info *conn)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	struct statvfs *stats = &priv->fs_stats;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_MOUNT), 0, 0);

	if (priv->pid_orig != getpid()) {
		/*
		 * Reopen device when LTFS was forked in fuse_main().
		 * Backend must handle reopen correctly if it sis needed.
		 * For example, iokit backend must handle reopen. But ibmtape backend
		 * doesn't need handle reopen because file descriptor is took over to a child
		 * process.
		 */
		ltfs_device_reopen(priv->devname, priv->data);
	}

#ifndef mingw_PLATFORM
	/*
	 * Open the I/O scheduler, if one has been specified by the user.
	 * Please note that when we run in library mode the I/O scheduler
	 * is loaded individually for each mounted volume.
	 */
	if (iosched_init(&priv->iosched_plugin, priv->data) < 0) {
		/* I/O scheduler disabled. Performance down, memory usage up. */
		ltfsmsg(LTFS_WARN, 14028W);
	}

	/* fill in fixed filesystem stats */
	stats->f_bsize = ltfs_get_blocksize(priv->data); /* Filesystem optimal transfer block size */

	/* Filesystem fragment size. Linux allows any f_frsize, whereas OS X (with MacFUSE) expects
	 * a power of 2 between 512 and 131072. */
#ifdef __APPLE__
	int nshift;

	if (stats->f_bsize > 131072)
		stats->f_frsize = 131072;
	else if (stats->f_bsize < 512)
		stats->f_frsize = 512;
	else {
		nshift = 0;
		stats->f_frsize = stats->f_bsize;
		while (stats->f_frsize != 1) {
			stats->f_frsize >>= 1;
			++nshift;
		}
		stats->f_frsize = 1 << nshift;
		if (stats->f_frsize < stats->f_bsize)
			stats->f_frsize <<= 1;
	}

	/* Having f_bsize different from f_frsize should technically be okay, but it
	 * seems that many (most?) programs don't understand the difference. So the only
	 * way to get consistent space usage results is to make them the same. */
	stats->f_bsize = stats->f_frsize;
#else
	stats->f_frsize = stats->f_bsize;
#endif /* __APPLE__ */

	stats->f_favail = 0;                               /* Ignored by FUSE */
	stats->f_flag = 0;                                 /* Ignored by FUSE */
	stats->f_fsid = LTFS_SUPER_MAGIC;                  /* Ignored by FUSE */
	stats->f_namemax = LTFS_FILENAME_MAX;

	ltfsmsg(LTFS_INFO, 14029I);
#endif /* mingw_PLATFORM */

	/* Kick timer thread for sync by time */
	if (priv->sync_type == LTFS_SYNC_TIME)
		periodic_sync_thread_init(priv->sync_time, priv->data);

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_MOUNT), (uint64_t)priv, 0);

	return priv;
}

/**
 * Unmount a filesystem. This function flushes all data to tape, makes the cartridge consistent,
 * closes the device, and frees the ltfs_volume field of the FUSE private data.
 */
void ltfs_fuse_umount(void *userdata)
{
	struct ltfs_fuse_data *priv = userdata;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_UNMOUNT), 0, 0);

	if (periodic_sync_thread_initialized(priv->data))
		periodic_sync_thread_destroy(priv->data);

	/*
	 * Destroy the I/O scheduler, if one has been specified by the user.
	 * Please note that when we run in library mode the I/O scheduler
	 * is destroyed individually for each mounted volume.
	 */
	ltfs_fsops_flush(NULL, true, priv->data);
	if (iosched_initialized(priv->data))
		iosched_destroy(priv->data);

	if (kmi_initialized(priv->data))
		kmi_destroy(priv->data);

	ltfs_set_commit_message_reason(SYNC_UNMOUNT, priv->data);
	ltfs_unmount(SYNC_UNMOUNT, priv->data);

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_UNMOUNT), 0, 0);
}

int ltfs_fuse_symlink(const char* to, const char* from)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	ltfs_file_id id;
	int ret;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_SYMLINK), 0, 0);

	ret = ltfs_fsops_symlink_path(to, from, &id, priv->data);

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_SYMLINK), ret, id.uid);

	return errormap_fuse_error(ret);
}

int ltfs_fuse_readlink(const char* path, char* buf, size_t size)
{
	struct ltfs_fuse_data *priv = fuse_get_context()->private_data;
	ltfs_file_id id;
	int ret;

	ltfs_request_trace(FUSE_REQ_ENTER(REQ_READLINK), (uint64_t)size, 0);

	ret = ltfs_fsops_readlink_path(path, buf, size, &id, priv->data);

	ltfs_request_trace(FUSE_REQ_EXIT(REQ_READLINK), ret, id.uid);

	return errormap_fuse_error(ret);
}

struct fuse_operations ltfs_ops = {
	.init        = ltfs_fuse_mount,
	.destroy     = ltfs_fuse_umount,
	.getattr     = ltfs_fuse_getattr,
	.fgetattr    = ltfs_fuse_fgetattr,
	.access      = ltfs_fuse_access,
	.statfs      = ltfs_fuse_statfs,
	.open        = ltfs_fuse_open,
	.release     = ltfs_fuse_release,
	.fsync       = ltfs_fuse_fsync,
	.flush       = ltfs_fuse_flush,
	.utimens     = ltfs_fuse_utimens,
	.chmod       = ltfs_fuse_chmod,
	.chown       = ltfs_fuse_chown,
	.create      = ltfs_fuse_create,
	.truncate    = ltfs_fuse_truncate,
	.ftruncate   = ltfs_fuse_ftruncate,
	.unlink      = ltfs_fuse_unlink,
	.rename      = ltfs_fuse_rename,
	.mkdir       = ltfs_fuse_mkdir,
	.rmdir       = ltfs_fuse_rmdir,
	.opendir     = ltfs_fuse_opendir,
	.readdir     = ltfs_fuse_readdir,
	.releasedir  = ltfs_fuse_releasedir,
	.fsyncdir    = ltfs_fuse_fsyncdir,
	.write       = ltfs_fuse_write,
	.read        = ltfs_fuse_read,
	.setxattr    = ltfs_fuse_setxattr,
	.getxattr    = ltfs_fuse_getxattr,
	.listxattr   = ltfs_fuse_listxattr,
	.removexattr = ltfs_fuse_removexattr,
	.symlink     = ltfs_fuse_symlink,
	.readlink    = ltfs_fuse_readlink,
#if FUSE_VERSION >= 28
	.flag_nullpath_ok = 1,
#endif
};
