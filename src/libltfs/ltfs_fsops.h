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
** FILE NAME:       ltfs_fsops_raw.h
**
** DESCRIPTION:     Defines LTFS file and directory operations.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
**                  Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
*************************************************************************************
*/


#ifndef ltfs_fsops_h__
#define ltfs_fsops_h__

#ifdef __cplusplus
extern "C" {
#endif

typedef	struct _ltfs_file_id
{
	uint64_t uid;
	uint64_t ino;
} ltfs_file_id;

/**
 * Open a file or directory. This means looking it up in the name tree and incrementing its
 * reference count, as well as informing the I/O scheduler that the file is open.
 * For each call to this function, there must be a call to ltfs_fsops_close() with an identical
 * use_iosched flag.
 * @param path Path to open.
 * @param open_write True if the caller plans to write to the file. Ignored for directories.
 * @param use_iosched True to inform the I/O scheduler (if any) that the path is being opened.
 *                    This flag must be set if the resulting dentry handle will be used to read
 *                    or write data; otherwise, leaving it unset may be faster.
 * @param d On success, points to the dentry corresponding to 'path'. Undefined on error.
 * @param vol LTFS volume.
 * @return
 *    - 0 on success
 *    - -LTFS_NULL_ARG if any of the input arguments are NULL
 *    - -LTFS_RDONLY_VOLUME if the underlying device is read-only
 *    - -LTFS_INVALID_SRC_PATH if the path cannot be UTF-8 encoded or contains invalid characters
 *    - -LTFS_NAMETOOLONG if any component of the path is too long
 *    - -LTFS_NO_DENTRY if the path does not exist
 *    - Another negative value if an unexpected error occurred
 */
int ltfs_fsops_open(const char *path, bool open_write, bool use_iosched, struct dentry **d,
	struct ltfs_volume *vol);

/**
 * Open a file or directory. This means looking it up in the name tree and incrementing its
 * reference count, as well as informing the I/O scheduler that the file is open.
 * For each call to this function, there must be a call to ltfs_fsops_close() with an identical
 * use_iosched flag.
 * @param path Path to open.
 * @param open_write True if the caller plans to write to the file. Ignored for directories.
 * @param use_iosched True to inform the I/O scheduler (if any) that the path is being opened.
 *                    This flag must be set if the resulting dentry handle will be used to read
 *                    or write data; otherwise, leaving it unset may be faster.
 * @param d On success, points to the dentry corresponding to 'path'. Undefined on error.
 * @param is_readonly On success, read only flag of the dentry in d is filled. Undefined on error.
 * @param isopendir True if called as opendir
 * @param vol LTFS volume.
 * @return
 *    - 0 on success
 *    - -LTFS_NULL_ARG if any of the input arguments are NULL
 *    - -LTFS_RDONLY_VOLUME if the underlying device is read-only
 *    - -LTFS_INVALID_SRC_PATH if the path cannot be UTF-8 encoded or contains invalid characters
 *    - -LTFS_NAMETOOLONG if any component of the path is too long
 *    - -LTFS_NO_DENTRY if the path does not exist
 *    - Another negative value if an unexpected error occurred
 */
int ltfs_fsops_open_combo(const char *path, bool open_write, bool use_iosched,
						  struct dentry **d, bool *is_readonly,
						  bool isopendir, struct ltfs_volume *vol);

/**
 * Close a prevously opened file or directory.
 * For each call to ltfs_fsops_open(), there must be a call to this function with an identical
 * use_iosched flag. For each call to ltfs_fsops_create(), there must be a call to this function
 * with use_iosched set for files and unset for directories.
 * @param d File or directory to close.
 * @param dirty True if this dentry handle has been used to write data. Ignored for directories.
 * @param use_iosched True to inform the I/O scheduler that the path is being closed.
 *                    This must have the same setting passed to ltfs_fsops_open(). It must
 *                    be 'true' if the dentry handle was obtained through ltfs_fsops_create().
 * @param open_write True if the file was opened by write mode
 * @param vol LTFS volume to which the dentry belongs.
 * @return
 *    - 0 on success
 *    - -LTFS_NULL_ARG if any of the input arguments are NULL
 *    - Another negative value if an unexpected error occurred. This can only happen if
 *      use_iosched is set and 'd' is a file (not a directory).
 */
int ltfs_fsops_close(struct dentry *d, bool dirty, bool open_write, bool use_iosched,  struct ltfs_volume *vol);

/**
 * Recalculate used blocks of specified dentry and reflect it to valid_blocks in the index structure
 * @param d File or directory to update.
 * @param vol LTFS volume to which the dentry belongs.
 * @return
 *    - 0 on success
 *    - -LTFS_NULL_ARG if any of the input arguments are NULL
 *    - Another negative value if an unexpected error occurred. This can only happen if
 *      use_iosched is set and 'd' is a file (not a directory).
 */
int ltfs_fsops_update_used_blocks(struct dentry *d, struct ltfs_volume *vol);

/**
 * Create a new file or directory and open it for writing.
 * For each call to this function, there must be a call to ltfs_fsops_close() (with use_iosched
 * set for files and unset for directories).
 * @param path Path to create.
 * @param isdir True to create a directory, false to create a file.
 * @param readonly True to mark the new file or directory read-only.
 * @param overwrite True to mark that this operation may overwrite existing data.
 * @param dentry On success, points to the newly created file or directory.
 * @param vol LTFS volume where the file or directory will be created.
 * @return
 *    - 0 on success
 *    - -LTFS_NULL_ARG if any of the input arguments are NULL
 *    - -LTFS_RDONLY_VOLUME if the underlying device is read-only
 *    - -LTFS_NO_SPACE if the underlying device is out of space
 *    - -LTFS_DEVICE_UNREADY if the underlying device is not ready
 *    - -LTFS_INVALID_PATH if the path cannot be UTF-8 encoded or contains invalid characters
 *    - -LTFS_NAMETOOLONG if any component of the path is too long
 *    - -LTFS_NO_DENTRY if the parent directory does not exist
 *    - -LTFS_DENTRY_EXISTS if the target path already exists
 *    - Another negative value if an unexpected error occurred
 */
int ltfs_fsops_create(const char *path, bool isdir, bool readonly, bool overwrite, struct dentry **dentry,
	struct ltfs_volume *vol);

/**
 * Unlink a file or directory from the tree.
 * @param path File or directory to remove.
 * @param id (out) File identifier to be processed
 * @param vol LTFS volume to modify.
 * @return
 *    - 0 on success
 *    - -LTFS_NULL_ARG if any of the input arguments are NULL
 *    - -LTFS_RDONLY_VOLUME if the underlying device is read-only
 *    - -LTFS_NO_SPACE if the underlying device is out of space
 *    - -LTFS_DEVICE_UNREADY if the underlying device is not ready
 *    - -LTFS_INVALID_SRC_PATH if the path cannot be UTF-8 encoded or contains invalid characters
 *    - -LTFS_NAMETOOLONG if any component of the path is too long
 *    - -LTFS_NO_DENTRY if the path does not exist
 *    - -LTFS_UNLINKROOT if path is "/"
 *    - -LTFS_DIRNOTEMPTY if the target is a non-empty directory
 *    - Another negative value if an unexpected error occurred
 */
int ltfs_fsops_unlink(const char *path, ltfs_file_id *id, struct ltfs_volume *vol);

/**
 * Rename a file or directory, unlinking the target path if it exists.
 * @param from Source file or directory.
 * @param to Target path.
 * @param id (out) File identifier to be processed
 * @param vol LTFS volume to modify.
 * @return
 *    - 0 on success
 *    - -LTFS_NULL_ARG if any of the input arguments are NULL
 *    - -LTFS_RDONLY_VOLUME if the underlying device is read-only
 *    - -LTFS_NO_SPACE if the underlying device is out of space
 *    - -LTFS_DEVICE_UNREADY if the underlying device is not ready
 *    - -LTFS_INVALID_SRC_PATH if 'from' cannot be UTF-8 encoded or contains invalid characters
 *    - -LTFS_INVALID_PATH if 'to' cannot be UTF-8 encoded or contains invalid characters
 *    - -LTFS_NAMETOOLONG if any component of either path is too long
 *    - -LTFS_NO_DENTRY if the source path or target directory does not exist, or if the source
 *                      path cannot be UTF-8 encoded or contains invalid characters
 *    - -LTFS_DIRNOTEMPTY if the target is a non-empty directory
 *    - -LTFS_DIR_MOVE (OS X only) if a move between directories was requested
 *    - -LTFS_RENAMELOOP if 'from' is the parent or ancestor of 'to'
 *    - Another negative value if an unexpected error occurred
 */
int ltfs_fsops_rename(const char *from, const char *to, ltfs_file_id *id, struct ltfs_volume *vol);

/**
 * Get attributes for a file or directory.
 * @param d File or directory to inspect.
 * @param attr Output buffer for attributes.
 * @param vol LTFS volume.
 * @return
 *    - 0 on success
 *    - -LTFS_NULL_ARG if any of the input arguments are NULL
 *    - Another negative value if an internal error occurred in the I/O scheduler
 */
int ltfs_fsops_getattr(struct dentry *d, struct dentry_attr *attr, struct ltfs_volume *vol);

/**
 * Wrapper for ltfs_fsops_getattr() that takes a path argument.
 */
int ltfs_fsops_getattr_path(const char *path, struct dentry_attr *attr, ltfs_file_id *id, struct ltfs_volume *vol);

/**
 * Set an extended attribute on a file or directory.
 * @param path File or directory for which the extended attribute will be set.
 * @param name Name to set.
 * @param value Value to set, may be binary, not necessarily null-terminated.
 * @param size Size of value in bytes.
 * @param flags XATTR_REPLACE to fail if xattr doesn't exist, XATTR_CREATE to fail if it does
 *              exist, or 0 to ignore any existing value.
 * @param id (out) File identifier to be processed
 * @param vol LTFS volume.
 * @return
 *    - 0 on success
 *    - LTFS_NULL_ARG if any of the input arguments are NULL
 *    - Another negative value if an unexpected error occurred.
 */
int ltfs_fsops_setxattr(const char *path, const char *name, const char *value, size_t size,
						int flags, ltfs_file_id *id, struct ltfs_volume *vol);

/*
 * Get an extended attribute from a file or directory.
 * @param path File/directory to check
 * @param name Extended attribute name
 * @param value On success, contains the extended attribute value. Can be NULL, in which case
 *        the return value will indicate how many bytes are necessary to be allocated in the
 *        'value' buffer to have the extended attribute data copied.
 * @param size Size of the output buffer. If set to 0, then no data is copied to the 'list'
 *        buffer and the return value will indicate how many bytes will be necessary for the
 *        user to allocate in the 'param' buffer to have the extended attribute copied.
 * @param id (out) File identifier to be processed
 * @param vol LTFS volume
 * @return
 *    - if size is nonzero, the number of bytes returned in the value buffer.
 *    - if size is zero, the number of bytes in the xattr value.
 *    - -LTFS_NO_XATTR is the requested attribute or namespace doesn't exist
 *    - -LTFS_BAD_ARG if size is greater than 0 but 'value' is NULL
 *    - -LTFS_SMALL_BUFFER is size is too small to hold the xattr value.
 *    - Another negative value if an unexpected error occurred.
 */
int ltfs_fsops_getxattr(const char *path, const char *name, char *value, size_t size,
						ltfs_file_id *id, struct ltfs_volume *vol);

/**
 * List extended attributes for a file or directory.
 * @param path File or directory to inspect.
 * @param list Output buffer where the extended attributes found will be copied to. If set to
 *        NULL, then no data is copied and the return value will indicate how many bytes will
 *        be necessary for the user to allocate in the 'list' buffer to have the extended
 *        attributes list copied.
 * @param size Size of the output buffer. If set to 0, then no data is copied to the 'list'
 *        buffer and the return value will indicate how many bytes will be necessary for the
 *        user to allocate in the 'list' buffer to have the extended attributes list copied.
 * @param id (out) File identifier to be processed
 * @param vol LTFS volume.
 * @return
 *    - The number of bytes copied to the 'list' buffer on success
 *    - LTFS_NULL_ARG if the 'd' or 'vol' input arguments are NULL
 *    - Another negative value if an unexpected error occurred.
 */
int ltfs_fsops_listxattr(const char *path, char *list, size_t size, ltfs_file_id *id, struct ltfs_volume *vol);

/*
 * Remove an extended attribute from a file or directory.
 * @param path File or directory to operate on
 * @param name Extended attribute name to delete
 * @param id (out) File identifier to be processed
 * @param vol LTFS volume
 * @return
 *    - 0 on success
 *    - -LTFS_NO_XATTR is the requested attribute or namespace doesn't exist
 *    - Another negative value if an unexpected error occurred.
 */
int ltfs_fsops_removexattr(const char *path, const char *name, ltfs_file_id *id, struct ltfs_volume *vol);

/**
 * List directory contents, invoking a callback function for each directory entry.
 * It does not invoke the filler for the "." and ".." entries.
 * @param d Directory to list.
 * @param buf Output buffer, passed to the filler function.
 * @param filler Callback invoked for each directory entry.
 * @param filler_priv Pointer to private data used by the filler function. May be NULL.
 * @param vol LTFS volume.
 * @return
 *    - 0 on success
 *    - -LTFS_NULL_ARG if any of the input arguments are NULL
 *    - -LTFS_ISFILE if the provided dentry is not a directory
 *    - Another negative value if an unexpected error occurred or if the filler callback failed
 */
int ltfs_fsops_readdir(struct dentry *d, void *buf, ltfs_dir_filler filler, void *filler_priv,
	struct ltfs_volume *vol);

/**
 * Get an entry in the directory.
 * It does get the "." and ".." entries only when d is specified non volume root directory.
 * @param d Directory to list.
 * @param dirent Output buffer
 * @param index index number to read the entry in d
 * @param vol LTFS volume.
 * @return
 *    - 0 on success
 *    - -LTFS_NULL_ARG if any of the input arguments are NULL
 *    - -LTFS_ISFILE if the provided dentry is not a directory
 *    - -LTFS_NO_DENTRY if the provided index of dentry is not existed in the directory
 *    - Another negative value if an unexpected error occurred
 */
int ltfs_fsops_read_direntry(struct dentry *d, struct ltfs_direntry *dirent, unsigned long index,
	struct ltfs_volume *vol);

/**
 * Get an entry in the directory. It always does get the "." and ".." entries.
 * @param d Directory to list.
 * @param dirent Output buffer
 * @param index index number to read the entry in d
 * @param vol LTFS volume.
 * @return
 *    - 0 on success
 *    - -LTFS_NULL_ARG if any of the input arguments are NULL
 *    - -LTFS_ISFILE if the provided dentry is not a directory
 *    - -LTFS_NO_DENTRY if the provided index of dentry is not existed in the directory
 *    - Another negative value if an unexpected error occurred
 */
int ltfs_fsops_read_direntry_noroot(struct dentry *d, struct ltfs_direntry *dirent,
	unsigned long index, struct ltfs_volume *vol);

/**
 * Set access and modification times on a file or directory.
 * @param d File or directory to modify.
 * @param ts New times to set.
 * @param vol LTFS volume.
 * @return
 *    - 0 on success
 *    - -LTFS_NULL_ARG if any of the input arguments are NULL
 *    - -LTFS_RDONLY_VOLUME if the underlying device is read-only
 *    - -LTFS_NO_SPACE if the underlying device is out of space
 *    - -LTFS_DEVICE_UNREADY if the underlying device is not ready
 *    - Another negative value if an unexpected error occurred
 */
int ltfs_fsops_utimens(struct dentry *d, const struct ltfs_timespec ts[2], struct ltfs_volume *vol);

/**
 * Wrapper for ltfs_fsops_utimens() that takes a path argument.
 */
int ltfs_fsops_utimens_path(const char *path, const struct ltfs_timespec ts[2], ltfs_file_id *id, struct ltfs_volume *vol);

/**
 * Set access and modification times on a file or directory.
 * @param d File or directory to modify.
 * @param ts New times to set.
 *        ts[0] access_time
 *        ts[1] modify_time
 *        ts[2] creation_time
 *        ts[3] change_time
 * @param vol LTFS volume.
 * @return
 *    - 0 on success
 *    - -LTFS_NULL_ARG if any of the input arguments are NULL
 *    - -LTFS_RDONLY_VOLUME if the underlying device is read-only
 *    - -LTFS_NO_SPACE if the underlying device is out of space
 *    - -LTFS_DEVICE_UNREADY if the underlying device is not ready
 *    - Another negative value if an unexpected error occurred
 */
int ltfs_fsops_utimens_all(struct dentry *d, const struct ltfs_timespec ts[4], struct ltfs_volume *vol);

/**
 * Set the readonly flag on a file or directory.
 * @param d File or directory to modify.
 * @param readonly New value for the readonly flag.
 * @param vol LTFS volume.
 * @return
 *    - 0 on success
 *    - -LTFS_NULL_ARG if any of the input arguments are NULL
 *    - -LTFS_RDONLY_VOLUME if the underlying device is read-only
 *    - -LTFS_NO_SPACE if the underlying device is out of space
 *    - -LTFS_DEVICE_UNREADY if the underlying device is not ready
 *    - Another negative value if an unexpected error occurred
 */
int ltfs_fsops_set_readonly(struct dentry *d, bool readonly, struct ltfs_volume *vol);

/**
 * Wrapper for ltfs_fsops_set_readonly() that takes a path argument.
 */
int ltfs_fsops_set_readonly_path(const char *path, bool readonly, ltfs_file_id *id, struct ltfs_volume *vol);

/**
 * Write data to a file, buffering data in the I/O scheduler if possible.
 * @param d File to write.
 * @param buf Data buffer to write.
 * @param count Size of the input buffer.
 * @param offset Logical file offset where the new data should be written.
 * @param isupdatetime False if callar is Windows system.
 * @param vol LTFS volume.
 * @return
 *    - 0 on success
 *    - -LTFS_RDONLY_VOLUME if the underlying device is read-only
 *    - -LTFS_NO_SPACE if the requested partition is out of space
 *    - -LTFS_NULL_ARG if any of the input arguments are NULL
 *    - -LTFS_BAD_PARTNUM if 'partition' is not a valid partition ID
 *    - -LTFS_ISDIRECTORY if 'd' is a directory
 *    - Another negative value if an internal error occurs or if writing to the device fails
 */
int ltfs_fsops_write(struct dentry *d, const char *buf, size_t count, off_t offset,
	bool isupdatetime, struct ltfs_volume *vol);

/**
 * Read data from a file.
 * The number of bytes read may be less than requested, or even 0, if the read location extents
 * past the logical end of the file.
 * @param d File to read.
 * @param buf Output buffer.
 * @param count Number of bytes to read.
 * @param offset Logical file offset to read from.
 * @param vol LTFS volume.
 * @return
 *    - Number of bytes read on success (may be less than 'count', or even 0)
 *    - -LTFS_NULL_ARG if any of the input arguments are NULL
 *    - -LTFS_ISDIRECTORY if 'd' is a directory
 *    - Another negative value if an internal error or device error occurred
 */
ssize_t ltfs_fsops_read(struct dentry *d, char *buf, size_t count, off_t offset,
	struct ltfs_volume *vol);

/**
 * Truncate a file to shorten it or extend it with zeros.
 * When extending a file, the file is made sparse; explicit zeros are not written to the medium.
 * @param d File to modify.
 * @param length New logical file size.
 * @param vol LTFS volume.
 * @return
 *    - 0 on success
 *    - -LTFS_NULL_ARG if any of the input arguments are NULL
 *    - -LTFS_RDONLY_VOLUME if the underlying device is read-only
 *    - -LTFS_NO_SPACE if the requested partition is out of space
 *    - -LTFS_DEVICE_UNREADY if the underlying device is not ready
 *    - -LTFS_BAD_ARG if 'length' is negative
 *    - -LTFS_ISDIRECTORY if 'd' is a directory
 *    - Another negative value if an internal error occurs
 */
int ltfs_fsops_truncate(struct dentry *d, off_t length, struct ltfs_volume *vol);

/**
 * Wrapper for ltfs_fsops_truncate() that takes a path argument.
 */
int ltfs_fsops_truncate_path(const char *path, off_t length, ltfs_file_id *id, struct ltfs_volume *vol);

/**
 * Flush cached data for a file to the medium. Has no effect if no I/O scheduler is in use.
 * @param d File to flush, or NULL to flush all cached data.
 * @param closeflag True if flushing prior to closing a file. The scheduler may use this flag as
 *                  a hint to discard its read cache (if applicable). The use of this flag is
 *                  mostly obsoleted by the newer iosched_close() API.
 * @param vol LTFS volume.
 * @return
 *    - 0 on success
 *    - -LTFS_NULL_ARG if 'vol' is NULL
 *    - -LTFS_ISDIRECTORY if 'd' is a directory
 *    - Another negative value if an internal error or device write error occurred.
 *    - -LTFS_RDONLY_VOLUME or -LTFS_NO_SPACE may be returned if the I/O scheduler has dirty
 *      buffers when the flush request is made, but the caller cannot rely on receiving those
 *      values to determine the read-only status of the medium.
 */
int ltfs_fsops_flush(struct dentry *d, bool closeflag, struct ltfs_volume *vol);

/**
 * Create a symbolic link node
 * @param to path to the target file of symbolic link
 * @param from path to the symbolic link node
 * @param id (out) File identifier to be processed
 * @param vol LTFS volume.
 * @return
 *    - 0 on success
 *    - -LTFS_NULL_ARG if 'vol' is NULL
 *    - Another negative value if an internal error or device write error occurred.
 */
int ltfs_fsops_symlink_path(const char *to, const char *from, ltfs_file_id *id, struct ltfs_volume *vol);

/**
 * Read a target of the symbolic link node
 * @param path path to the symbolic link node
 * @param buf buffer to copy the a target of the symbolic link node (string)
 * @param size sife of buffer
 * @param id (out) File identifier to be processed
 * @param vol LTFS volume.
 * @return
 *    - 0 on success
 *    - -LTFS_NULL_ARG if 'vol' is NULL
 *    - Another negative value if an internal error or device write error occurred.
 */
int ltfs_fsops_readlink_path(const char *path, char *buf, size_t size, ltfs_file_id *id, struct ltfs_volume *vol);

/**
 * Change target path from relative to absolute (Use for Windows)
 * @param link path to the symbolic link node
 * @param target path to the symbolic link target
 * @param buf buffer to copy the a target absolute path of the symbolic link node (string)
 * @param size sife of buffer
 * @return
 *    - 0 on success
 *    - -LTFS_NULL_ARG if 'vol' is NULL
 *    - -LTFS_NO_DENTRY if cannot be resolved
 *    - -LTFS_SMALL_BUFFER if input buffer is smaller than the resolved string
 *    - Another negative value if an internal error or device write error occurred.
 */
int ltfs_fsops_target_absolute_path(const char *link, const char *target, char *buf, size_t size );

/**
 * Flush all cached data to the medium and write index.
 * @param reason reason of sync operation. This reason is stored into the index.
 * @param vol LTFS volume.
 * @return
 *    - 0 on success
 *    - -LTFS_NULL_ARG if 'vol' is NULL
 *    - Another negative value if an internal error or device write error occurred.
 *    - -LTFS_RDONLY_VOLUME or -LTFS_NO_SPACE may be returned if the I/O scheduler has dirty
 *      buffers when the flush request is made, but the caller cannot rely on receiving those
 *      values to determine the read-only status of the medium.
 */
int ltfs_fsops_volume_sync(char *reason, struct ltfs_volume *vol);

#ifdef __cplusplus
}
#endif

#endif /* ltfs_fsops_h__ */
