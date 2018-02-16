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
** FILE NAME:       ltfs_fsops_raw.c
**
** DESCRIPTION:     LTFS raw file and directory operations (no I/O scheduler).
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

#include "ltfs.h"
#include "ltfs_internal.h"
#include "fs.h"
#include "index_criteria.h"
#include "arch/time_internal.h"
#include "tape.h"
#include "dcache.h"

int ltfs_fsraw_open(const char *path, bool open_write, struct dentry **d, struct ltfs_volume *vol)
{
	int ret;
	struct dentry *dtmp;

	CHECK_ARG_NULL(path, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);

	ret = ltfs_get_volume_lock(false, vol);
	if (ret < 0)
		return ret;

	if (dcache_initialized(vol))
		ret = dcache_open(path, &dtmp, vol);
	else
		ret = fs_path_lookup(path, 0, &dtmp, vol->index);
	if (ret < 0) {
		/* Print message only if the error code is an unexpected one */
		if (ret != -LTFS_NO_DENTRY && ret != -LTFS_NAMETOOLONG)
			ltfsmsg(LTFS_ERR, 11040E, ret);
		releaseread_mrsw(&vol->lock);
		return ret;
	}

	if (open_write && ! dtmp->isdir) {
		uint64_t max_filesize = index_criteria_get_max_filesize(vol);
		acquirewrite_mrsw(&dtmp->meta_lock);
		if (! dtmp->matches_name_criteria && max_filesize > 0 && dtmp->size <= max_filesize)
			dtmp->matches_name_criteria = index_criteria_match(dtmp, vol);
		releasewrite_mrsw(&dtmp->meta_lock);
	}

	*d = dtmp;
	releaseread_mrsw(&vol->lock);
	return 0;
}

int ltfs_fsraw_close(struct dentry *d)
{
	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	if (dcache_initialized(d->vol))
		dcache_close(d, true, true, d->vol);
	else
		fs_release_dentry(d);
	return 0;
}

/**
 * Non-locking version of ltfs_fsraw_write_data.
 * This function should be called with a write lock on vol->lock. The lock is converted
 * to a read lock on exit.
 * It takes the tape device lock internally, so the caller must not hold any dentry meta lock.
 */
int _ltfs_fsraw_write_data_unlocked(char partition, const char *buf, size_t count, uint64_t repetitions,
	tape_block_t *startblock, struct ltfs_volume *vol)
{
	int ret;
	uint64_t blocksize, rep_count;
	size_t to_write, write_count = 0;
	ssize_t nwrite_last;
	bool is_first_dp_locate = false;
	struct ltfs_timespec ts_start, ts_end;
	struct tc_position start;

	blocksize = vol->label->blocksize;

	/* Validate partition ID */
	if (partition != ltfs_dp_id(vol) && partition != ltfs_ip_id(vol)) {
		ltfsmsg(LTFS_ERR, 11067E);
		writetoread_mrsw(&vol->lock);
		return -LTFS_BAD_PARTNUM;
	}

	/* Exit immediately if no data will be written */
	if (count == 0 || repetitions == 0) {
		writetoread_mrsw(&vol->lock);
		return 0;
	}

	/* Can only write multiple repetitions if the input buffer contains an integer
	 * number of blocks */
	if (repetitions > 1 && count % blocksize != 0) {
		ltfsmsg(LTFS_ERR, 11068E);
		writetoread_mrsw(&vol->lock);
		return -LTFS_BAD_ARG;
	}

	/* Lock the device now, as we may need to issue multiple writes atomically */
	ret = tape_device_lock(vol->device);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 11004E, __FUNCTION__);
		writetoread_mrsw(&vol->lock);
		return ret;
	}

	/* Check read-only status of the medium. */
	ret = ltfs_get_partition_readonly(partition, vol);
	if (ret < 0) {
		writetoread_mrsw(&vol->lock);
		goto out_unlock;
	}

	/* Write index to the other partition if necessary */
	if (partition == ltfs_ip_id(vol))
		ret = ltfs_write_index_conditional(ltfs_dp_id(vol), vol);
	else /* partition == ltfs_dp_id(vol) */
		ret = ltfs_write_index_conditional(ltfs_ip_id(vol), vol);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 11069E, ret);
		writetoread_mrsw(&vol->lock);
		goto out_unlock;
	}

	/* We're about to write. Let other users of the volume know the target partition does not
	 * end in an index. */
	if (partition == ltfs_ip_id(vol))
		vol->ip_index_file_end = false;
	else { /* partition == ltfs_dp_id(vol) */
		vol->dp_index_file_end = false;
		if (!vol->first_locate.tv_sec && !vol->first_locate.tv_nsec)
			is_first_dp_locate = true;
	}

	/* Write lock on the volume is not needed past this point */
	writetoread_mrsw(&vol->lock);

	if (is_first_dp_locate) {
		get_current_timespec(&ts_start);
		vol->first_locate.tv_sec = UINT64_MAX;
	}

	/* Seek to partition append position (not necessarily end of data) */
	ret = tape_seek_append_position(vol->device, ltfs_part_id2num(partition, vol), partition == vol->label->partid_ip);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 11070E, partition);
		goto out_unlock;
	}

	if (is_first_dp_locate) {
		get_current_timespec(&ts_end);
		timer_sub(&ts_end, &ts_start, &(vol->first_locate));
	}

	ret = tape_get_position(vol->device, &start);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 11071E, ret);
		goto out_unlock;
	}

	/* Tell the caller about the first block written */
	if (startblock)
		*startblock = start.block;

	/* write blocks to tape */
	for (rep_count = 0; rep_count < repetitions; ++rep_count) {
		write_count = 0;
		while (write_count < count) {
			to_write = (count - write_count > blocksize) ? blocksize : count - write_count;
			nwrite_last = tape_write(vol->device, buf + write_count, to_write, false, false);
			if (nwrite_last < 0) {
				ret = nwrite_last;
				ltfsmsg(LTFS_ERR, 11072E, ret);
				goto out_unlock;
			}
			write_count += to_write;
		}
	}

	ret = 0;

out_unlock:
	if (NEED_REVAL(ret))
		tape_start_fence(vol->device);
	else if (IS_UNEXPECTED_MOVE(ret))
		vol->reval = -LTFS_REVAL_FAILED;
	tape_device_unlock(vol->device);
	return ret;
}

int ltfs_fsraw_write_data(char partition, const char *buf, size_t count, uint64_t repetitions,
	tape_block_t *startblock, struct ltfs_volume *vol)
{
	int ret;

	CHECK_ARG_NULL(buf, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);

start:
	ret = ltfs_get_volume_lock(true, vol);
	if (ret < 0)
		return ret;
	ret = _ltfs_fsraw_write_data_unlocked(partition, buf, count, repetitions, startblock, vol);
	if (ret == -LTFS_DEVICE_FENCED || NEED_REVAL(ret)) {
		ret = (ret == -LTFS_DEVICE_FENCED) ?
			ltfs_wait_revalidation(vol) : ltfs_revalidate(false, vol);
		if (ret == 0)
			goto start;
	} else if (IS_UNEXPECTED_MOVE(ret)) {
		vol->reval = -LTFS_REVAL_FAILED;
		releaseread_mrsw(&vol->lock);
	} else
		releaseread_mrsw(&vol->lock);
	return ret;
}

/**
 * Non-locking version of ltfs_fsraw_add_extent.
 * The caller should hold a read lock on vol->lock and a write lock on d->contents_lock.
 */
int _ltfs_fsraw_add_extent_unlocked(struct dentry *d, struct extent_info *ext, bool update_time,
	struct ltfs_volume *vol)
{
	struct extent_info *entry, *preventry;
	struct extent_info *ext_copy, *splitentry;
	bool ext_used = false, free_ext = false;
	uint64_t ext_fileoffset_end, fileoffset_diff;
	uint64_t entry_fileoffset_end, entry_byteoffset_end, entry_blockcount, entry_byteoffset_mod;
	uint64_t realsize_new, blocksize;

	blocksize = vol->label->blocksize;
	ext_fileoffset_end = ext->fileoffset + ext->bytecount;
	realsize_new = d->realsize;

	/* Copy the input extent now to avoid failing after the extent list has already been updated */
	ext_copy = malloc(sizeof(struct extent_info));
	if (! ext_copy) {
		ltfsmsg(LTFS_ERR, 10001E, "ltfs_append_extent_unlocked: extent copy");
		return -LTFS_NO_MEMORY;
	}
	*ext_copy = *ext;

	/* Update the extent list */
	if (! TAILQ_EMPTY(&d->extentlist)) {
		TAILQ_FOREACH_REVERSE_SAFE(entry, &d->extentlist, extent_struct, list, preventry) {
			entry_fileoffset_end = entry->fileoffset + entry->bytecount;
			entry_byteoffset_end = entry->byteoffset + entry->bytecount;
			entry_blockcount = entry_byteoffset_end / blocksize;

			/* Update existing entry by truncating, deleting, or splitting it */
			if (ext->fileoffset <= entry->fileoffset && ext_fileoffset_end > entry->fileoffset) {
				if (entry_fileoffset_end <= ext_fileoffset_end) {
					/* Delete entry */
					TAILQ_REMOVE(&d->extentlist, entry, list);
					realsize_new -= entry->bytecount;
					free(entry);
					entry = NULL;
				} else {
					/* Truncate entry from its beginning */
					fileoffset_diff = ext_fileoffset_end - entry->fileoffset;
					entry_byteoffset_mod = fileoffset_diff + entry->byteoffset;
					entry->start.block += entry_byteoffset_mod / blocksize;
					entry->byteoffset = entry_byteoffset_mod % blocksize;
					entry->bytecount -= fileoffset_diff;
					entry->fileoffset += fileoffset_diff;
					realsize_new -= fileoffset_diff;
					entry_byteoffset_end = entry->byteoffset + entry->bytecount;
					entry_blockcount = entry_byteoffset_end / blocksize;
				}
			} else if (ext->fileoffset > entry->fileoffset &&
				ext->fileoffset < entry_fileoffset_end) {
				if (ext_fileoffset_end >= entry_fileoffset_end) {
					/* Truncate entry from its end */
					entry->bytecount = ext->fileoffset - entry->fileoffset;
					realsize_new -= entry_fileoffset_end - ext->fileoffset;
					entry_fileoffset_end = entry->fileoffset + entry->bytecount;
					entry_byteoffset_end = entry->byteoffset + entry->bytecount;
					entry_blockcount = entry_byteoffset_end / blocksize;
				} else {
					/* Split entry */
					splitentry = malloc(sizeof(struct extent_info));
					if (! splitentry) {
						ltfsmsg(LTFS_ERR, 10001E, "ltfs_append_extent_unlocked: splitentry");
						free(ext_copy);
						return -LTFS_NO_MEMORY;
					}

					/* Set up splitentry, which will be the last of the 3 new extents */
					fileoffset_diff = ext_fileoffset_end - entry->fileoffset;
					entry_byteoffset_mod = fileoffset_diff + entry->byteoffset;
					splitentry->start.partition = entry->start.partition;
					splitentry->start.block = entry->start.block +
						(entry_byteoffset_mod / blocksize);
					splitentry->byteoffset = entry_byteoffset_mod % blocksize;
					splitentry->bytecount = entry->bytecount - fileoffset_diff;
					splitentry->fileoffset = ext_fileoffset_end;
					TAILQ_INSERT_AFTER(&d->extentlist, entry, splitentry, list);

					entry->bytecount = ext->fileoffset - entry->fileoffset;
					entry_fileoffset_end = entry->fileoffset + entry->bytecount;
					entry_byteoffset_end = entry->byteoffset + entry->bytecount;
					entry_blockcount = entry_byteoffset_end / blocksize;
					realsize_new -= ext->bytecount;
				}
			}

			/* Process ext's contents by appending to entry or inserting ext after entry */
			if (entry && ext->fileoffset == entry_fileoffset_end &&
				entry->start.partition == ext->start.partition &&
				entry_byteoffset_end % blocksize == 0 &&
				entry->start.block + entry_blockcount == ext->start.block &&
				ext->byteoffset == 0) {
				/* Add ext's bytes to entry */
				entry->bytecount += ext->bytecount;
				realsize_new += ext->bytecount;
				ext_used = true;
				free_ext = true;
				break;
			} else if (entry && ext->fileoffset >= entry_fileoffset_end) {
				/* Insert ext after entry */
				TAILQ_INSERT_AFTER(&d->extentlist, entry, ext_copy, list);
				realsize_new += ext->bytecount;
				ext_used = true;
				break;
			}
		}
	}

	if (! ext_used) {
		TAILQ_INSERT_HEAD(&d->extentlist, ext_copy, list);
		realsize_new += ext->bytecount;
	} else if (free_ext)
		free(ext_copy);

	/* Update file size and times */
	acquirewrite_mrsw(&d->meta_lock);
	if (ext_fileoffset_end > d->size)
		d->size = ext_fileoffset_end;
	d->realsize = realsize_new;
	if (update_time) {
		get_current_timespec(&d->modify_time);
		d->change_time = d->modify_time;
	}

	/*
	 *  Mark file contents is update.
	 *  No need to mark at this time but reserve this value for fueture release
	 */
	d->extents_dirty = true;
	d->dirty = true;
	releasewrite_mrsw(&d->meta_lock);

	ltfs_set_index_dirty(true, false, vol->index);

	return 0;
}

int ltfs_fsraw_add_extent(struct dentry *d, struct extent_info *ext, bool update_time,
	struct ltfs_volume *vol)
{
	int ret;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(ext, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);

	/* This function is not called until blocks are already on the tape, so the
	 * out of space condition is not a problem. */
	ret = ltfs_get_partition_readonly(ltfs_ip_id(vol), vol);
	if (ret < 0 && ret != -LTFS_NO_SPACE && ret != -LTFS_LESS_SPACE)
		return ret;
	ret = ltfs_get_partition_readonly(ltfs_dp_id(vol), vol);
	if (ret < 0 && ret != -LTFS_NO_SPACE && ret != -LTFS_LESS_SPACE)
		return ret;

	ret = ltfs_get_volume_lock(false, vol);
	if (ret < 0)
		return ret;

	acquirewrite_mrsw(&d->contents_lock);
	ret = _ltfs_fsraw_add_extent_unlocked(d, ext, update_time, vol);
	releasewrite_mrsw(&d->contents_lock);

	if (dcache_initialized(vol))
		ret = dcache_flush(d, FLUSH_EXTENT_LIST, vol);

	releaseread_mrsw(&vol->lock);

	return ret;
}

int ltfs_fsraw_cleanup_extent(struct dentry *d, struct tc_position err_pos, unsigned long blocksize, struct ltfs_volume *vol)
{
	int ret = 0;
	struct name_list *entry, *tmp;
	struct extent_info *ext, *preventry;

	if (HASH_COUNT(d->child_list) != 0) {
		HASH_ITER(hh, d->child_list, entry, tmp) {
			if (entry->d->isdir) {
				ret = ltfs_fsraw_cleanup_extent(entry->d, err_pos, blocksize, vol);
			}
			else {
                TAILQ_FOREACH_REVERSE_SAFE(ext, &entry->d->extentlist, extent_struct, list, preventry) {
					if (err_pos.block <= (ext->start.block + ext->bytecount/blocksize)) {
						ltfsmsg(LTFS_INFO, 11334I, entry->name, (unsigned long long)ext->start.block, (unsigned long long)ext->bytecount);

						ret = ltfs_get_volume_lock(false, vol);
						if (ret < 0)
							return ret;

						acquirewrite_mrsw(&d->contents_lock);
                        entry->d->size -= ext->bytecount;
						TAILQ_REMOVE(&entry->d->extentlist, ext, list);
						free(ext);
						releasewrite_mrsw(&d->contents_lock);

						if (dcache_initialized(vol))
							ret = dcache_flush(d, FLUSH_EXTENT_LIST, vol);

						releaseread_mrsw(&vol->lock);
					}
				}
			}
		}
	}
	return ret;
}

int ltfs_fsraw_write(struct dentry *d, const char *buf, size_t count, off_t offset, char partition,
	bool update_time, struct ltfs_volume *vol)
{
	int ret;
	struct extent_info tmpext;
	struct tape_offset logical_start = { .partition = partition, .block = 0 };

	ltfsmsg(LTFS_DEBUG2, 11252D, d->platform_safe_name, (long long)offset, (unsigned long long)count);

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(buf, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);

	/* Take locks and write data to the medium */
start:
	ret = ltfs_get_volume_lock(true, vol);
	if (ret < 0)
		return ret;
	ret = _ltfs_fsraw_write_data_unlocked(partition, buf, count, 1, &logical_start.block, vol);
	if (ret == -LTFS_DEVICE_FENCED || NEED_REVAL(ret)) {
		ret = (ret == -LTFS_DEVICE_FENCED) ?
			ltfs_wait_revalidation(vol) : ltfs_revalidate(false, vol);
		if (ret == 0)
			goto start;
		return ret;
	} else if (IS_UNEXPECTED_MOVE(ret)) {
		vol->reval = -LTFS_REVAL_FAILED;
		releaseread_mrsw(&vol->lock);
		return ret;
	} else if (ret < 0) {
		ltfsmsg(LTFS_ERR, 11077E, ret);
		releaseread_mrsw(&vol->lock);
		return ret;
	}

	/* Save extent to the file */
	tmpext.start = logical_start;
	tmpext.byteoffset = 0;
	tmpext.bytecount = count;
	tmpext.fileoffset = (uint64_t)offset;

	acquirewrite_mrsw(&d->contents_lock);
	ret = _ltfs_fsraw_add_extent_unlocked(d, &tmpext, update_time, vol);
	releasewrite_mrsw(&d->contents_lock);

	releaseread_mrsw(&vol->lock);
	return ret;
}

ssize_t ltfs_fsraw_read(struct dentry *d, char *buf, size_t count, off_t offset,
	struct ltfs_volume *vol)
{
	int ret;
	uint64_t next_off, last_off;
	ssize_t nread, ncopy;
	size_t read_count;
	struct extent_info *entry;
	struct tc_position seekpos, curpos;
	uint64_t firstbyte, lastbyte, blockbytes;
	uint64_t entry_fileoffset_end;
	unsigned long blocksize;
	bool is_first_dp_locate = false;
	struct ltfs_timespec ts_start, ts_end;

	ltfsmsg(LTFS_DEBUG2, 11254D, d->platform_safe_name, (long long)offset, (unsigned long long)count);

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(buf, -LTFS_NULL_ARG);

	if (count == 0)
		return 0;

	/* Lock the index, dentry and device */
start:
	read_count = 0;
	ret = ltfs_get_volume_lock(false, vol);
	if (ret < 0)
		return ret;
	acquireread_mrsw(&d->contents_lock);
	ret = tape_device_lock(vol->device);
	if (ret == -LTFS_DEVICE_FENCED) {
		releaseread_mrsw(&d->contents_lock);
		ret = ltfs_wait_revalidation(vol);
		if (ret == 0)
			goto start;
		else
			return ret;
	} else if (ret < 0) {
		ltfsmsg(LTFS_ERR, 11004E, __FUNCTION__);
		releaseread_mrsw(&d->contents_lock);
		releaseread_mrsw(&vol->lock);
		return ret;
	}

	/* allocate the last block cache if necessary */
	if (! vol->last_block) {
		vol->last_block = malloc(vol->label->blocksize);
		if (! vol->last_block) {
			ltfsmsg(LTFS_ERR, 10001E, "ltfs_fsraw_read: block cache");
			ret = -LTFS_NO_MEMORY;
			goto out_unlock;
		}
	}

	blocksize = vol->label->blocksize;
	next_off = (uint64_t)offset;
	last_off = (uint64_t)offset + count;

	TAILQ_FOREACH(entry, &d->extentlist, list) {
		if (read_count == count)
			break;

		entry_fileoffset_end = entry->fileoffset + entry->bytecount;

		/* Fill buffer with zeros if bytes are needed before this extent */
		if (next_off < entry->fileoffset) {
			if (entry->fileoffset > last_off) {
				memset(buf + read_count, 0, last_off - next_off);
				read_count = count;
				break;
			} else
				memset(buf + read_count, 0, entry->fileoffset - next_off);
			read_count += entry->fileoffset - next_off;
			next_off = entry->fileoffset;
		}

		/* Read any needed data from this extent */
		if (entry_fileoffset_end > next_off) {
			/* Compute current position */
			ret = tape_get_position(vol->device, &curpos);
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, 11085E, ret);
				goto out_unlock;
			}

			/* Compute target position */
			seekpos.partition = ltfs_part_id2num(entry->start.partition, vol);
			seekpos.block = entry->start.block +
				(next_off - entry->fileoffset + entry->byteoffset) / blocksize;

			/* Seek if required */
			if ((curpos.partition != seekpos.partition || curpos.block != seekpos.block) &&
				! (curpos.partition == seekpos.partition && curpos.block == seekpos.block + 1 &&
				   entry->start.partition == vol->last_pos.partition &&
				   seekpos.block == vol->last_pos.block)) {

				if (!vol->first_locate.tv_sec && !vol->first_locate.tv_nsec &&
					(seekpos.partition == (uint32_t)ltfs_dp_id(vol))) {
					get_current_timespec(&ts_start);
					is_first_dp_locate = true;
					vol->first_locate.tv_sec = UINT64_MAX;
				}

				ret = tape_seek(vol->device, &seekpos);
				if (ret < 0) {
					ltfsmsg(LTFS_ERR, 11086E, ret, entry->start.partition, (unsigned long long)seekpos.block);
					goto out_unlock;
				}

				if (is_first_dp_locate) {
					get_current_timespec(&ts_end);
					timer_sub(&ts_end, &ts_start, &(vol->first_locate));
				}
			}

			/* Read from the extent until end of extent or output buffer full */
			firstbyte = entry->fileoffset - entry->byteoffset +
				(seekpos.block - entry->start.block) * blocksize;
			lastbyte = firstbyte;
			while (entry_fileoffset_end > next_off && read_count < count) {
				lastbyte += blocksize;
				if (entry_fileoffset_end < lastbyte)
					lastbyte = entry_fileoffset_end;
				blockbytes = lastbyte - firstbyte;

				/* Read this block into a temp buffer or return the existing contents */
				if (entry->start.partition == vol->last_pos.partition &&
					seekpos.block == vol->last_pos.block &&
					(seekpos.partition == curpos.partition && seekpos.block + 1 == curpos.block)) {
					if (vol->last_size < blockbytes) {
						ltfsmsg(LTFS_ERR, 11087E, (unsigned int)blockbytes, vol->last_size);
						ret = -LTFS_SMALL_BLOCK;
						goto out_unlock;
					}

				} else {
					if (blocksize == blockbytes)
						nread = tape_read(vol->device, vol->last_block, blocksize, false,
							vol->kmi_handle);
					else
						nread = tape_read(vol->device, vol->last_block, blocksize, true,
							vol->kmi_handle);

					if (nread < 0) {
						ret = nread;
						ltfsmsg(LTFS_ERR, 11088E, ret);
						goto out_unlock;
					} else if ((size_t) nread < blockbytes) {
						ltfsmsg(LTFS_ERR, 11089E, (unsigned int)blockbytes, (unsigned int)nread);
						ret = -LTFS_SMALL_BLOCK;
						goto out_unlock;
					}

					vol->last_pos.partition = entry->start.partition;
					vol->last_pos.block = seekpos.block;
					vol->last_size = nread;
					++curpos.block;
				}

				/* Copy data into output buffer */
				ncopy = (lastbyte > last_off ? last_off : lastbyte) - next_off;
				memcpy(buf + read_count, vol->last_block + (next_off - firstbyte), ncopy);

				firstbyte += blocksize;
				next_off += ncopy;
				read_count += ncopy;
				++seekpos.block;
			}
		}
	}

	/* Handle sparse end-of-file: fill buffer with zeros */
	if (count > read_count && next_off < d->size) {
		ncopy = (last_off < d->size) ? (last_off - next_off) : (d->size - next_off);
		memset(buf + read_count, 0, ncopy);
		read_count += ncopy;
	}

	/* update access time */
	acquirewrite_mrsw(&d->meta_lock);
	get_current_timespec(&d->access_time);
	releasewrite_mrsw(&d->meta_lock);

	ltfs_set_index_dirty(true, true, vol->index);

out_unlock:
	releaseread_mrsw(&d->contents_lock);
	if (NEED_REVAL(ret)) {
		tape_start_fence(vol->device);
		tape_device_unlock(vol->device);
		ret = ltfs_revalidate(false, vol);
		if (ret == 0)
			goto start;
	} else if (IS_UNEXPECTED_MOVE(ret)) {
		vol->reval = -LTFS_REVAL_FAILED;
		tape_device_unlock(vol->device);
		releaseread_mrsw(&vol->lock);
	} else {
		tape_device_unlock(vol->device);
		releaseread_mrsw(&vol->lock);
	}

	if (ret < 0)
		return ret;
	return read_count;
}

int ltfs_fsraw_truncate(struct dentry *d, off_t length, struct ltfs_volume *vol)
{
	int ret;
	struct extent_info *entry, *preventry;
	uint64_t ulength = (uint64_t)length, new_realsize, entry_fileoffset_last;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);

	ret = ltfs_get_volume_lock(false, vol);
	if (ret < 0)
		return ret;
	acquirewrite_mrsw(&d->contents_lock);

	new_realsize = d->realsize;

	/* Truncate the extent list if necessary */
	if (ulength < d->size && ! TAILQ_EMPTY(&d->extentlist)) {
		TAILQ_FOREACH_REVERSE_SAFE(entry, &d->extentlist, extent_struct, list, preventry) {
			entry_fileoffset_last = entry->fileoffset + entry->bytecount;
			if (entry->fileoffset >= ulength || ulength == 0) {
				/* This extent is full past the new EOF */
				TAILQ_REMOVE(&d->extentlist, entry, list);
				new_realsize -= entry->bytecount;
				free(entry);
			} else if (entry_fileoffset_last > ulength) {
				new_realsize -= entry_fileoffset_last - ulength;
				entry->bytecount = ulength - entry->fileoffset;
			} else
				break;
		}
	}

	/* Update size, realsize and times */
	acquirewrite_mrsw(&d->meta_lock);
	d->size = ulength;
	d->realsize = new_realsize;
	get_current_timespec(&d->modify_time);
	d->change_time = d->modify_time;
	releasewrite_mrsw(&d->meta_lock);

	releasewrite_mrsw(&d->contents_lock);

	ltfs_set_index_dirty(true, false, vol->index);
	d->dirty = true;

	releaseread_mrsw(&vol->lock);
	return 0;
}

struct dentry *ltfs_fsraw_get_dentry(struct dentry *d, struct ltfs_volume *vol)
{
	int ret;

	CHECK_ARG_NULL(d, NULL);
	CHECK_ARG_NULL(vol, NULL);

	ret = ltfs_get_volume_lock(false, vol);
	if (ret < 0)
		return NULL;
	if (dcache_initialized(vol)) {
		dcache_get_dentry(d, vol);
	} else {
		acquirewrite_mrsw(&d->meta_lock);
		d->numhandles++;
		releasewrite_mrsw(&d->meta_lock);
	}
	releaseread_mrsw(&vol->lock);
	return d;
}

void ltfs_fsraw_put_dentry(struct dentry *d, struct ltfs_volume *vol)
{
	if (! d) {
		ltfsmsg(LTFS_WARN, 10006W, "d", __FUNCTION__);
		return;
	} else if (! vol) {
		ltfsmsg(LTFS_WARN, 10006W, "vol", __FUNCTION__);
		return;
	}
	if (dcache_initialized(vol))
		dcache_put_dentry(d, vol);
	else
		fs_release_dentry(d);
}
