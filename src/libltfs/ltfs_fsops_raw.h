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
** FILE NAME:       ltfs_fsops_raw.h
**
** DESCRIPTION:     Defines raw file and directory operations (no I/O scheduler).
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

/**
 * Open a file or directory. This means looking it up in the name tree and incrementing its
 * reference count.
 * @param path Path to open. This must be an LTFS-valid path as confirmed by
 *             the pathname_validate_path function.
 * @param open_write True if the caller plans to write to the file. Ignored for directories.
 * @param d On success, points to the dentry corresponding to 'path'. Undefined on error.
 * @param vol LTFS volume.
 * @return
 *    - 0 on success
 *    - -LTFS_NULL_ARG if any of the input arguments are NULL
 *    - -LTFS_NAMETOOLONG if any component of the path is too long
 *    - -LTFS_NO_DENTRY if the path does not exist
 *    - Another negative value if an internal error occurs
 */
int ltfs_fsraw_open(const char *path, bool open_write, struct dentry **d, struct ltfs_volume *vol);

/**
 * Close a previously opened file or directory.
 * This decrements the dentry's reference count, freeing the dentry if the reference count
 * becomes 0.
 * @param d Dentry to close.
 * @return
 *    - 0 on success
 *    - -LTFS_NULL_ARG if 'd' is NULL
 */
int ltfs_fsraw_close(struct dentry *d);

/**
 * Write data blocks to the tape.
 * @param partition Partition to write to.
 * @param buf Data buffer to write.
 * @param count Size of data buffer.
 * @param repetitions Number of copies of buf to write. If repetitions > 1, count must be a
 *                    multiple of vol->label->blocksize.
 * @param startblock Output pointer, contains the first block number of the written data on
 *                   success. Undefined on failure. Ignored if NULL.
 * @param vol LTFS volume.
 * @return
 *    - 0 on success
 *    - -LTFS_RDONLY_VOLUME if the underlying device is read-only
 *    - -LTFS_NO_SPACE if the requested partition is out of space
 *    - -LTFS_NULL_ARG if any of the input arguments (except startblock) are NULL
 *    - -LTFS_BAD_PARTNUM if 'partition' is not a valid partition ID
 *    - -LTFS_BAD_ARG if repetitions > 1 and count is not a multiple of vol->label->blocksize
 *    - Another negative value if an internal error occurred or if writing to the device failed
 */
int ltfs_fsraw_write_data(char partition, const char *buf, size_t count, uint64_t repetitions,
	tape_block_t *startblock, struct ltfs_volume *vol);

/**
 * Save a new extent to a file, updating the file size and times as appropriate.
 * The data corresponding to the new extent must be on the device by the time this function
 * is called.
 * @param d File to write.
 * @param ext Extent containing new file data.
 * @param update_time True to update the file's modify and change times, false to ignore them.
 * @param vol LTFS volume.
 * @return
 *    - 0 on success
 *    - -LTFS_RDONLY_VOLUME if the underlying device is read-only
 *    - -LTFS_NULL_ARG if any of the input arguments are NULL
 *    - -LTFS_NO_MEMORY if allocating memory for a new extent failed
 *    - Another negative value if an internal error occurs
 */
int ltfs_fsraw_add_extent(struct dentry *d, struct extent_info *ext, bool update_time,
	struct ltfs_volume *vol);

/**
 * Remove extent which is written beyond the write perm error position.
 * @param d File to modify.
 * @param err_pos  write perm error position.
 * @param blocksize blocksize of the data.
 * @param vol LTFS volume.
 * @return
 *    - 0 on success
 *    - Negative value if an internal error occurs
 */
int ltfs_fsraw_cleanup_extent(struct dentry *d, struct tc_position err_pos, unsigned long blocksize, struct ltfs_volume *vol);

/**
 * Write data to a file without buffering.
 * @param d File to write.
 * @param buf Data buffer to write.
 * @param count Size of the input buffer.
 * @param offset Logical file offset where the new data should be written.
 * @param partition Logical partition ID where the new data should be written.
 * @param update_time True to update the file's modify and change times, false to ignore them.
 * @param vol LTFS volume.
 * @return
 *    - 0 on success
 *    - -LTFS_RDONLY_VOLUME if the underlying device is read-only
 *    - -LTFS_NO_SPACE if the requested partition is out of space
 *    - -LTFS_NULL_ARG if any of the input arguments are NULL
 *    - -LTFS_BAD_PARTNUM if 'partition' is not a valid partition ID
 *    - Another negative value if an internal error occurs or if writing to the device fails
 */
int ltfs_fsraw_write(struct dentry *d, const char *buf, size_t count, off_t offset, char partition,
	bool update_time, struct ltfs_volume *vol);

/**
 * Read data from a file without using the I/O scheduler.
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
 *    - Another negative value if an internal error or device error occurred
 */
ssize_t ltfs_fsraw_read(struct dentry *d, char *buf, size_t count, off_t offset,
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
 *    - Another negative value if an internal error occurs
 */
int ltfs_fsraw_truncate(struct dentry *d, off_t length, struct ltfs_volume *vol);

/**
 * Get another reference on a dentry.
 * @param d Dentry to reference.
 * @param vol LTFS volume.
 * @retval The same dentry on success, or NULL if either of the input arguments are NULL.
 */
struct dentry *ltfs_fsraw_get_dentry(struct dentry *d, struct ltfs_volume *vol);

/**
 * Release a reference on a dentry.
 * @param d Dentry to release.
 * @param vol LTFS volume.
 */
void ltfs_fsraw_put_dentry(struct dentry *d, struct ltfs_volume *vol);

