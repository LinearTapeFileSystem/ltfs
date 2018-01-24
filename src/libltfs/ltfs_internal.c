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
** FILE NAME:       ltfs_internal.c
**
** DESCRIPTION:     Implements private core libltfs functions.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
*************************************************************************************
*/

#include "ltfs.h"
#include "ltfs_internal.h"
#include "libltfs/ltfslogging.h"
#include "tape.h"
#include "fs.h"
#include "xml_libltfs.h"
#include "label.h"
#include "dcache.h"
#include "index_criteria.h"
#include "arch/time_internal.h"
#include "libltfs/arch/osx/osx_string.h"
#include "iosched.h"
#include "ltfs_fsops.h"
#include "xattr.h"

/**
 * Allocate an empty LTFS index.
 * @param[out] index The newly allocated index.
 * @param vol LTFS volume structure this index will be used with. This parameter is used to set
 *            a pointer from the index's dentries back to the volume. It may be NULL if that
 *            pointer is not needed.
 * @return 0 on success or a negative value on error.
 */
int ltfs_index_alloc(struct ltfs_index **index, struct ltfs_volume *vol)
{
	int ret;
	struct ltfs_index *newindex;

	CHECK_ARG_NULL(index, -LTFS_NULL_ARG);
	/* 'vol' is allowed to be NULL */

	newindex = calloc(1, sizeof(struct ltfs_index));
	if (!newindex) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return -LTFS_NO_MEMORY;
	}

	ret = ltfs_mutex_init(&newindex->dirty_lock);
	if (ret) {
		ltfsmsg(LTFS_ERR, 11166E, ret);
		ltfs_index_free(&newindex);
		return -ret;
	}

	ret = ltfs_mutex_init(&newindex->refcount_lock);
	if (ret) {
		ltfsmsg(LTFS_ERR, 11166E, ret);
		ltfs_index_free(&newindex);
		return -ret;
	}

	ret = ltfs_mutex_init(&newindex->rename_lock);
	if (ret) {
		ltfsmsg(LTFS_ERR, 11166E, ret);
		ltfs_index_free(&newindex);
		return -ret;
	}

	newindex->generation = 0;
	newindex->refcount = 1;
	newindex->uid_number = 1;
	newindex->version = LTFS_INDEX_VERSION;

	newindex->root = fs_allocate_dentry(NULL, "/", NULL, true, false, false, newindex);
	if (! newindex->root) {
		ltfsmsg(LTFS_ERR, 11168E);
		ltfs_index_free(&newindex);
		return -LTFS_NO_MEMORY;
	}
	newindex->root->link_count++; /* Root dentry has an extra link from its implicit parent */
	newindex->root->vol = vol;

	newindex->symerr_count = 0;
	newindex->symlink_conflict = NULL;

	*index = newindex;
	return 0;
}

void _ltfs_index_free(bool force, struct ltfs_index **index)
{
	size_t i;

	if (! index || ! *index)
		return;

	ltfs_mutex_lock(&(*index)->refcount_lock);
	(*index)->refcount--;
	if ((*index)->refcount == 0 || force) {
		ltfs_mutex_unlock(&(*index)->refcount_lock);
		ltfs_mutex_destroy(&(*index)->refcount_lock);

		if ((*index)->root)
			fs_release_dentry((*index)->root);
		ltfs_mutex_destroy(&(*index)->dirty_lock);
		ltfs_mutex_destroy(&(*index)->rename_lock);

		if ((*index)->tag_count > 0) {
			for (i=0; i<(*index)->tag_count; ++i)
				free((*index)->preserved_tags[i]);
			free((*index)->preserved_tags);
		}
		index_criteria_free(&((*index)->original_criteria));
		index_criteria_free(&((*index)->index_criteria));
		if ((*index)->commit_message)
			free((*index)->commit_message);
		if ((*index)->volume_name.name)
			free((*index)->volume_name.name);
		if ((*index)->creator)
			free((*index)->creator);
		if ((*index)->symerr_count > 0)
			free((*index)->symlink_conflict);
		free(*index);
		*index = NULL;
	} else
		ltfs_mutex_unlock(&(*index)->refcount_lock);
}

/**
 * Read label from a tape, storing the information in a volume structure.
 * @param vol the volume
 * @return 0 on success or a negative value on error.
 */
int ltfs_read_labels(bool trial, struct ltfs_volume *vol)
{
	int ret;
	struct ltfs_label *label0 = NULL, *label1 = NULL;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);

	ret = label_alloc(&label0);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 11169E, ret);
		goto out_free;
	}
	ret = label_alloc(&label1);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 11169E, ret);
		goto out_free;
	}

	ret = ltfs_read_one_label(0, label0, vol);
	if (ret < 0) {
		if (! trial || ret != -LTFS_LABEL_INVALID)
			ltfsmsg(LTFS_ERR, 11170E, ret);
		goto out_free;
	}
	ret = ltfs_read_one_label(1, label1, vol);
	if (ret < 0) {
		if (! trial || ret != -LTFS_LABEL_INVALID)
			ltfsmsg(LTFS_ERR, 11171E, ret);
		goto out_free;
	}

	/* Check labels for consistency */
	ret = label_compare(label0, label1);
	if (ret < 0) {
		if (! trial || ret != -LTFS_LABEL_MISMATCH)
			ltfsmsg(LTFS_ERR, 11172E, ret);
		goto out_free;
	}

	/* Clear label */
	if (vol->label->creator)
		free(vol->label->creator);

	/* Store label data in the supplied volume */
	vol->label->creator = label0->creator;
	label0->creator = NULL;
	strncpy(vol->label->barcode, label0->barcode, 6);
	vol->label->barcode[6] = '\0';
	strncpy(vol->label->vol_uuid, label0->vol_uuid, 36);
	vol->label->vol_uuid[36] = '\0';
	vol->label->format_time = label0->format_time;
	vol->label->blocksize = label0->blocksize;
	vol->label->enable_compression = label0->enable_compression;
	vol->label->partid_dp = label0->partid_dp;
	vol->label->partid_ip = label0->partid_ip;
	vol->label->part_num2id[0] = label0->this_partition;
	vol->label->part_num2id[1] = label1->this_partition;
	vol->label->version = label0->version;

out_free:
	if (label0)
		label_free(&label0);
	if (label1)
		label_free(&label1);

	return ret;
}

/**
 * Read one label from the given partition.
 * @param partition partition to read label from
 * @param label structure to populate with label contents
 * @param vol LTFS volume
 */
int ltfs_read_one_label(tape_partition_t partition, struct ltfs_label *label,
	struct ltfs_volume *vol)
{
	int ret;
	char *buf = NULL;
	unsigned int bufsize;
	struct tc_position seekpos;
	ssize_t nread;
	char ansi_sig[5];
	bool too_long = false, ansi_valid = false;

	ret = tape_get_max_blocksize(vol->device, &bufsize);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 17195E, "read label", ret);
		return ret;
	}

	if (bufsize < LTFS_LABEL_MAX) {
		ltfsmsg(LTFS_ERR, 17185E, bufsize);
		return -LTFS_SMALL_BLOCKSIZE;
	} else
		bufsize = LTFS_LABEL_MAX;

	buf = calloc(1, bufsize + LTFS_CRC_SIZE);
	if (!buf) {
		ltfsmsg(LTFS_ERR, 10001E, "ltfs_read_one_label: buffer");
		return -LTFS_NO_MEMORY;
	}

	/* seek to start of the partition */
	seekpos.partition = partition;
	seekpos.block = 0;
	ret = tape_seek(vol->device, &seekpos);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 11173E, ret, (unsigned long)partition);
		/* Simple heuristic to detect an unpartitioned tape: seeking to partition 1 fails.
		 * Note that the seek may fail for other reasons, which can't currently be distinguished
		 * by looking at the backend return codes. */
		if (ret <= -LTFS_ERR_MIN && partition == 1)
			ret = -LTFS_NOT_PARTITIONED;
		goto out_free;
	}

	/* read ANSI label */
	memset(buf, 0, 81);
	nread = tape_read(vol->device, buf, (size_t)bufsize, true, vol->kmi_handle);
	if (nread < 0) {
		ltfsmsg(LTFS_ERR, 11174E, (int)nread);
		if (nread == -EDEV_EOD_DETECTED || nread == -EDEV_RECORD_NOT_FOUND)
			ret = -LTFS_LABEL_INVALID;
		else
			ret = nread;
		goto out_free;
	} else if (nread < 80) {
		ltfsmsg(LTFS_ERR, 11175E, (int)nread);
		ret = -LTFS_LABEL_INVALID;
		goto out_free;
	} else if (nread > 80) {
		ltfsmsg(LTFS_ERR, 11177E, (int)nread);
		too_long = true;
	}

	memcpy(label->barcode, buf+4, 6);
	label->barcode[6] = 0x00;

	/* check for LTFS signature */
	memcpy(ansi_sig, buf+24, 4);
	ansi_sig[4] = '\0';
	if (strcmp(ansi_sig, "LTFS")) {
		ltfsmsg(LTFS_ERR, 11176E);
		ret = -LTFS_LABEL_INVALID;
		goto out_free;
	}
	ansi_valid = true;

	/* Check for file mark after ANSI label */
	nread = tape_read(vol->device, buf, (size_t)bufsize, true, vol->kmi_handle);
	if (nread < 0) {
		ltfsmsg(LTFS_ERR, 11295E, (int)nread);
		if (nread == -EDEV_EOD_DETECTED)
			ret = -LTFS_LABEL_INVALID;
		else
			ret = nread;
		goto out_free;
	} else if (nread > 0) {
		/* no file mark after ANSI label */
		ltfsmsg(LTFS_ERR, 11296E);
		ret = -LTFS_LABEL_INVALID;
		goto out_free;
	}

	/* Read XML label */
	nread = tape_read(vol->device, buf, (size_t)bufsize, true, vol->kmi_handle);
	if (nread < 0) {
		ltfsmsg(LTFS_ERR, 11178E, (int)nread);
		if (nread == -EDEV_EOD_DETECTED)
			ret = -LTFS_LABEL_INVALID;
		else
			ret = nread;
		goto out_free;
	}
	ret = xml_label_from_mem(buf, nread, label);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 11179E, ret);
		goto out_free;
	}

	/* check for trailing file mark */
	nread = tape_read(vol->device, buf, (size_t)bufsize, true, vol->kmi_handle);
	if (nread < 0) {
		ltfsmsg(LTFS_ERR, 11180E, (int)nread);
		if (nread == -EDEV_EOD_DETECTED)
			ret = -LTFS_LABEL_INVALID;
		else
			ret = nread;
		goto out_free;
	} else if (nread > 0) {
		/* no file mark after XML label */
		ltfsmsg(LTFS_ERR, 11181E);
		ret = -LTFS_LABEL_INVALID;
		goto out_free;
	}

	ret = 0;

out_free:
	free(buf);

	if (ret && too_long && ansi_valid)
		return -LTFS_LABEL_POSSIBLE_VALID;

	return ret;
}

/**
 * Read an index file from tape at the current position, storing the result in the given volume.
 * The volume structure must already contain valid label data (blocksize and volume UUID).
 * This function does not read over another file mark
 * @param eod_pos EOD position for current partition, or 0 to assume that EOD will not be
 *                encountered during parsing.
 * @param vol the volume
 * @return 0 on success, 1 if index file does not end with a file mark (but is otherwise valid),
 *         or a negative value on error.
 */
int ltfs_read_index(uint64_t eod_pos, bool recover_symlink, struct ltfs_volume *vol)
{
	int ret;
	struct tc_position pos;
	bool end_fm = true;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);

	ret = tape_get_position(vol->device, &pos);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 11193E, ret);
		return ret;
	}

	ltfs_index_free(&vol->index);
	ret = ltfs_index_alloc(&vol->index, vol);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 11297E, ret);
		return ret;
	}

	/* Parse and validate the schema */
	ret = xml_schema_from_tape(eod_pos, vol);
	if ( vol->index->symerr_count ) {
		if ( recover_symlink ) {
			int rc;
			rc = ltfs_split_symlink( vol );
			if ( rc<0 ) ret=rc;
			else if ( ret==-LTFS_SYMLINK_CONFLICT ) ret=0;
		}
		else
		{
			ltfsmsg(LTFS_ERR, 11321E);
		}
		free( vol->index->symlink_conflict );
		vol->index->symerr_count=0;
	}

	if (ret < 0) {
		ltfsmsg(LTFS_WARN, 11194W, ret);
		return ret;
	} else if (ret == 1)
		end_fm = false;

	/* check volume UUID */
	if (strncmp(vol->index->vol_uuid, vol->label->vol_uuid, 36)) {
		ltfsmsg(LTFS_WARN, 11195W);
		return -LTFS_INDEX_INVALID;
	}

	/* check self pointer */
	if (vol->index->selfptr.partition != vol->label->part_num2id[pos.partition] ||
		vol->index->selfptr.block != pos.block) {
		ltfsmsg(LTFS_WARN, 11196W);
		return -LTFS_INDEX_INVALID;
	}

	/* basic back pointer checks */
	if (vol->index->backptr.partition != 0 &&
		vol->index->backptr.partition != vol->label->partid_dp) {
		ltfsmsg(LTFS_ERR, 11197E);
		return -LTFS_INDEX_INVALID;
	} else if (vol->index->backptr.partition == vol->index->selfptr.partition &&
		vol->index->selfptr.block != 5 &&
		vol->index->backptr.block != vol->index->selfptr.block &&
		vol->index->backptr.block >= vol->index->selfptr.block - 2 ) {
		ltfsmsg(LTFS_ERR, 11197E);
		return -LTFS_INDEX_INVALID;
	} else if (vol->index->backptr.partition != 0 && vol->index->backptr.block < 5) {
		ltfsmsg(LTFS_ERR, 11197E);
		return -LTFS_INDEX_INVALID;
	}

	/* space forward 1 FM if possible */
	if (end_fm) {
		ret = tape_spacefm(vol->device, 1);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 11198E, ret);
			return ret;
		}
	}

	return end_fm ? 0 : 1;
}

/**
 * Returns true iff a given char corresponds to a valid logical partition ID.
 */
bool ltfs_is_valid_partid(char id)
{
	return (id >= 'a' && id <= 'z');
}

#define check_err(cmd, msgid, label) \
	do { \
		ret = (cmd); \
		if (ret < 0) { \
			ltfsmsg(LTFS_ERR, msgid, ret); \
			goto label; \
		} \
	} while (0)

/**
 * Search a partition for the latest index file.
 * @param partition partition to search
 * @param eod_pos on success, the EOD position on the partition
 * @param index_end_pos on success, the next block number after the end of the index file.
 * @param fm_after outputs true if the index file is correctly closed by a file mark. Undefined
 *                 if no index file is found.
 * @param blocks_after outputs true if the index file is not at EOD. Undefined if no index
 *                     file is found.
 * @param vol LTFS volume
 * @return 0 on success, 1 if no index was found, or a negative value on error.
 */
int ltfs_seek_index(char partition, tape_block_t *eod_pos, tape_block_t *index_end_pos,
	bool *fm_after, bool *blocks_after, bool recover_symlink, struct ltfs_volume *vol)
{
	int ret;
	struct tc_position eod, pos;
	bool have_index;
	struct tc_coherency *coh;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(eod_pos, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(fm_after, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(blocks_after, -LTFS_NULL_ARG);

	/* find EOD */
	check_err(tape_seek_eod(vol->device, ltfs_part_id2num(partition, vol)), 11199E, out);
	check_err(tape_get_position(vol->device, &eod), 11200E, out);
	*eod_pos = eod.block;
	if (eod.block <= 4) { /* nothing on the partition except the label */
		return 1;
	}

	/* space back to the first candidate index file location */
	check_err(tape_spacefm(vol->device, -1), 11201E, out);
	check_err(tape_get_position(vol->device, &pos), 11200E, out);
	if (pos.block == 3) { /* is this the end of the label? */
		return 1;
	} else if (pos.block == eod.block - 1)
		check_err(tape_spacefm(vol->device, -1), 11201E, out);

	have_index = false;
	while (! have_index) {
		/* did we reach the end of the partition label? */
		check_err(tape_get_position(vol->device, &pos), 11200E, out);
		if (pos.block == 3) {
			return 1;
		}

		/* try to read an index file */
		check_err(tape_spacefm(vol->device, 1), 11202E, out);
		ret = ltfs_read_index(*eod_pos, recover_symlink, vol);

		if (ret == 0 || ret == 1) { /* found an index file */
			have_index = true;
			*fm_after = (ret == 0);
			check_err(tape_get_position(vol->device, &pos), 11200E, out);
			*index_end_pos = pos.block;
			*blocks_after = ! (pos.block == eod.block);
		} else { /* no index file found: go back 2 file marks and try again */
			ltfsmsg(LTFS_DEBUG, 11204D);
			if (!vol->ignore_wrong_version && ret == -LTFS_UNSUPPORTED_INDEX_VERSION)
				goto out;
			else
				check_err(tape_spacefm(vol->device, -2), 11203E, out);
		}
	}

	/* Check that partition searched is correct */
	if (partition != vol->index->selfptr.partition) {
		ltfsmsg(LTFS_ERR, 11328E, partition, vol->index->selfptr.partition);
		return -LTFS_INDEX_INVALID;
	}

	if (partition == ltfs_ip_id(vol))
		coh = &vol->ip_coh;
	else
		coh = &vol->dp_coh;
	strcpy(coh->uuid, vol->label->vol_uuid);
	coh->count = vol->index->generation;
	coh->set_id = vol->index->selfptr.block;

out:
	return ret;
}

/**
 * Perform a basic sanity check on the extents in an index structure. It checks that each extent's
 * blocks fall in an appropriate range (not in the label or past EOD).
 * @param d dentry to check. Its children are checked recursively.
 * @param ip_eod index partition EOD
 * @param dp_eod data partition EOD
 * @param vol LTFS volume. The label field must be populated with a block size and partition map.
 * @return 0 on success or a negative value on error.
 */
int _ltfs_check_extents(struct dentry *d, tape_block_t ip_eod, tape_block_t dp_eod,
	struct ltfs_volume *vol)
{
	int ret;
	struct extent_info *ext;
	tape_block_t ext_lastblock;
	struct name_list *list, *tmp;

	if (d->isdir && HASH_COUNT(d->child_list) != 0) {
		HASH_ITER(hh, d->child_list, list, tmp) {
			ret = _ltfs_check_extents(list->d, ip_eod, dp_eod, vol);
			if (ret < 0)
				return ret;
		}

	} else if (! TAILQ_EMPTY(&d->extentlist)) {
		TAILQ_FOREACH(ext, &d->extentlist, list) {
			ext_lastblock = ext->start.block + ext->bytecount / vol->label->blocksize;
			ext_lastblock += (ext->bytecount % vol->label->blocksize > 0) ? 1 : 0;
			if (ext->start.block < 4)
				return -LTFS_INDEX_INVALID;
			if ((ext->start.partition == vol->label->partid_ip && ext_lastblock >= ip_eod) ||
				(ext->start.partition == vol->label->partid_dp && ext_lastblock >= dp_eod))
				return -LTFS_INDEX_INVALID;
		}
	}

	return 0;
}

/**
 * Perform a quick check that the two given index files form a sane pointer chain. If necessary,
 * trace back to their common ancestor to verify that the back pointers are sane. This function
 * does NOT perform complete (or even extensive) verification.
 *  - It will not check for arbitrary breaks in the pointer chain.
 *  - It will not walk the back pointer chain past the common ancestor of the given index files.
 *  - It assumes that the basic checks performed by ltfs_read_index have passed.
 * @param ip index index file from the index partition, or NULL if none is available
 * @param dp_index index file from the data partition, or NULL if none is available
 * @param vol LTFS volume
 * @return 0 if IP index file is newer, 1 if DP index file is newer, 2 if both index files
 *         are NULL, or a negative value on error.
 */
int _ltfs_check_pointers(struct ltfs_index *ip_index, struct ltfs_index *dp_index,
	struct ltfs_volume *vol)
{
	int ret;
	tape_block_t ip_backptr, dp_backptr;
	struct tc_position seekpos;

	if (! ip_index) {
		if (dp_index) {
			/* DP index file is newer */
			return 1;
		} else {
			/* no index files, so nothing to do */
			return 2;
		}
	}

	if (! dp_index) {
		if (ip_index->backptr.partition != 0) {
			/* IP backpointer to nonexistent DP index file */
			ltfsmsg(LTFS_ERR, 11205E);
			return -LTFS_INDEX_INVALID;
		} else {
			/* IP index file is newer */
			return 0;
		}

	/* have both index files */
	} else {
		if (ip_index->generation >= dp_index->generation &&
			ip_index->backptr.partition == dp_index->selfptr.partition &&
			ip_index->backptr.block == dp_index->selfptr.block) {
			/* IP index file is newer */
			return 0;
		} else if (ip_index->generation > dp_index->generation) {
			/* IP backpointer doesn't match DP index file */
			ltfsmsg(LTFS_ERR, 11206E);
			return -LTFS_INDEX_INVALID;
		} else if (ip_index->generation == dp_index->generation &&
				   ip_index->backptr.partition == 0) {
			/* DP index file is newer */
			return 1;
		} else {
			/* Check only one previous back pinter */
			dp_backptr = dp_index->backptr.block;
			ip_backptr = ip_index->backptr.block;
			seekpos.partition = ltfs_part_id2num(vol->label->partid_dp, vol);
			if (dp_backptr > ip_backptr) {
				seekpos.block = dp_backptr;
				ret = tape_seek(vol->device, &seekpos);
				if (ret < 0)
					return ret;
				ret = ltfs_read_index(0, false, vol);
				if (ret < 0)
					return ret;
				dp_backptr = vol->index->backptr.block;
				if (ip_index->backptr.partition == 0 &&
					vol->index->generation < ip_index->generation) {
					/* IP index file is missing its back pointer */
					ltfsmsg(LTFS_ERR, 11207E);
					ltfs_index_free(&vol->index);
					return -LTFS_INDEX_INVALID;
				}
				ltfs_index_free(&vol->index);
			}
			/* DP index file is newer */
			return 1;
		}
	}
}

void _ltfs_last_ref(struct dentry *d, tape_block_t *dp_last, tape_block_t *ip_last,
	struct ltfs_volume *vol)
{
	struct extent_info *ext;
	tape_block_t ext_lastblock;
	struct name_list *list, *tmp;

	if (d->isdir && HASH_COUNT(d->child_list) != 0) {
		HASH_ITER(hh, d->child_list, list, tmp) {
			_ltfs_last_ref(list->d, dp_last, ip_last, vol);
		}

	} else if (! TAILQ_EMPTY(&d->extentlist)) {
		TAILQ_FOREACH(ext, &d->extentlist, list) {
			ext_lastblock = ext->start.block + ext->bytecount / vol->label->blocksize;
			ext_lastblock += (ext->bytecount % vol->label->blocksize > 0) ? 1 : 0;
			if (ext->start.partition == vol->label->partid_ip && ext_lastblock > *ip_last)
				*ip_last = ext_lastblock;
			else if (ext->start.partition == vol->label->partid_dp && ext_lastblock > *dp_last)
				*dp_last = ext_lastblock;
		}
	}
}

/**
 * Read the given partition, placing each discovered extent into
 * /_ltfs_lostandfound/partitionN_blockM. Here N is the partition number and M is the block
 * where the extent starts.
 * @return 0 on success or a negative value on error.
 */
int _ltfs_populate_lost_found(char partition, tape_block_t part_lastref,
	tape_block_t part_eod, struct ltfs_volume *vol)
{
	int err, ret = 0;
	char *buf;
	bool dcache_enabled, lfdir_descend = false;
	struct tc_position seekpos;
	ssize_t nr;
	char *fname, *fname_path;
	struct dentry *lf_dir, *file, *root = NULL;
	struct extent_info *ext;

	dcache_enabled = dcache_initialized(vol);

	/* create lost and found directory */

	if (dcache_enabled) {
		err = dcache_open("/", &root, vol);
		if (err < 0)
			return ret;
		err = dcache_openat("/", root, LTFS_LOSTANDFOUND_DIR, &lf_dir, vol);
		if (err < 0) {
			dcache_close(root, true, true, vol);
			return err;
		} else if (! lf_dir) {
			err = dcache_create(LTFS_LOSTANDFOUND_DIR, root, vol);
			if (err < 0) {
				dcache_close(root, true, true, vol);
				return err;
			}
			err = dcache_open("/"LTFS_LOSTANDFOUND_DIR, &lf_dir, vol);
			if (err < 0) {
				dcache_close(root, true, true, vol);
				return err;
			}
			ret = -LTFS_NO_DENTRY;
			lfdir_descend = true;
		}
	} else {
		ret = fs_path_lookup("/"LTFS_LOSTANDFOUND_DIR, 0, &lf_dir, vol->index);
		if (ret == -LTFS_NO_DENTRY) {
			lf_dir = fs_allocate_dentry(vol->index->root, LTFS_LOSTANDFOUND_DIR, NULL, true, false, true,
					vol->index);
			if (! lf_dir) {
				ltfsmsg(LTFS_ERR, 11209E);
				return -LTFS_NO_MEMORY;
			}
			++lf_dir->numhandles;
		} else if (ret < 0)
			return ret;
	}

	if (ret == -LTFS_NO_DENTRY) {
		get_current_timespec(&lf_dir->creation_time);
		lf_dir->modify_time = lf_dir->creation_time;
		lf_dir->access_time = lf_dir->creation_time;
		lf_dir->change_time = lf_dir->creation_time;
		lf_dir->backup_time = lf_dir->creation_time;
		lf_dir->readonly = true;
		ltfs_set_index_dirty(true, false, vol->index);
	}

	buf = malloc(vol->label->blocksize + LTFS_CRC_SIZE);
	if (! buf) {
		ltfsmsg(LTFS_ERR, 10001E, "_ltfs_populate_lost_found: buffer");
		if (dcache_enabled)
			dcache_close(lf_dir, true, lfdir_descend, vol);
		else
			fs_release_dentry(lf_dir);
		return -LTFS_NO_MEMORY;
	}

	/* Seek to first unreferenced block */
	seekpos.partition = ltfs_part_id2num(partition, vol);
	seekpos.block = (part_lastref > 4) ? part_lastref : 4;
	check_err(tape_seek(vol->device, &seekpos), 11212E, out_free);

	/* Populate lost and found directory with index partition extents */
	ret = 0;
	while (seekpos.block < part_eod) {
		nr = tape_read(vol->device, buf, vol->label->blocksize, true, vol->kmi_handle);

		if (nr < 0) {
			/* TODO: for some errors (e.g. block too large), it might be possible
			 * to skip this block and continue on */
			break;
		} else if (nr == 0) {
			ltfsmsg(LTFS_WARN, 11210W, (unsigned long)seekpos.partition);
		} else {
			/* generate a descriptive-but-probably-not-helpful filename */
			ret = asprintf(&fname_path, "/%s/partition%"PRIu32"_block%"PRIu64"_%zdbytes",
				LTFS_LOSTANDFOUND_DIR, seekpos.partition, seekpos.block, nr);
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, 10001E, "_ltfs_populate_lost_found: file name");
				ret = -LTFS_NO_MEMORY;
				goto out_free;
			}
			fname = strstr(&fname_path[1], "/") + 1;

			/* create a file containing this extent */
			ret = dcache_enabled ?
				dcache_open(fname_path, &file, vol) :
				fs_directory_lookup(lf_dir, fname, &file);
			if (ret < 0)
				goto out_free;
			if (! file) {
				if (dcache_enabled) {
					ret = dcache_create(fname, lf_dir, vol);
					if (ret < 0) {
						free(fname_path);
						goto out_free;
					}
					ret = dcache_open(fname_path, &file, vol);
					free(fname_path);
					if (ret < 0) {
						/* Cannot populate lost and found directory: failed to allocate file data */
						ltfsmsg(LTFS_ERR, 11211E);
						goto out_free;
					}
				} else {
					file = fs_allocate_dentry(lf_dir, fname, NULL, false, true, true, vol->index);
					free(fname_path);
					if (! file) {
						/* Cannot populate lost and found directory: failed to allocate file data */
						ltfsmsg(LTFS_ERR, 11211E);
						ret = -LTFS_NO_MEMORY;
						goto out_free;
					}
				}

				ext = calloc(1, sizeof(struct extent_info));
				if (! ext) {
					ltfsmsg(LTFS_ERR, 10001E, "_ltfs_populate_lost_found: extent");
					ret = -LTFS_NO_MEMORY;
					goto out_free;
				}

				acquirewrite_mrsw(&file->contents_lock);
				acquirewrite_mrsw(&file->meta_lock);
				if (! dcache_enabled)
					++file->numhandles;
				get_current_timespec(&file->creation_time);
				file->modify_time = file->creation_time;
				file->access_time = file->creation_time;
				file->change_time = file->creation_time;
				file->backup_time = file->creation_time;
				lf_dir->modify_time = file->creation_time;
				lf_dir->change_time = file->creation_time;

				ltfs_set_index_dirty(true, false, vol->index);
				file->matches_name_criteria = false;
				file->readonly = true;
				file->size = nr;
				file->realsize = nr;

				ext->start.partition = partition;
				ext->start.block = seekpos.block;
				ext->byteoffset = 0;
				ext->bytecount = nr;
				ext->fileoffset = 0;
				TAILQ_INSERT_TAIL(&file->extentlist, ext, list);
				releasewrite_mrsw(&file->contents_lock);

				if (dcache_enabled)
					dcache_close(file, false, true, vol);
				else
					/* file->meta_lock is released by fs_release_dentry_unlocked() */
					fs_release_dentry_unlocked(file);
			} else {
				/* can this happen ?! */
				if (dcache_enabled)
					dcache_close(file, true, true, vol);
				else
					fs_release_dentry(file);
				free(fname_path);
			}
		}

		++seekpos.block;
	}

out_free:
	if (dcache_enabled) {
		if (root)
			dcache_close(root, true, true, vol);
		if (lf_dir)
			dcache_close(lf_dir, true, lfdir_descend, vol);
	} else
		fs_release_dentry(lf_dir);
	free(buf);
	return ret;
}

/**
 * Recover extra (unreferenced) blocks from the tape by creating a "lost and found" directory
 * with one file for each extra extent found at the end of each partition.
 */
int _ltfs_make_lost_found(tape_block_t ip_eod, tape_block_t dp_eod,
	tape_block_t ip_endofidx, tape_block_t dp_endofidx, struct ltfs_volume *vol)
{
	int ret;
	tape_block_t lastblock_d = 0, lastblock_i = 0;

	/* Find last referenced data block on each partition */
	_ltfs_last_ref(vol->index->root, &lastblock_d, &lastblock_i, vol);

	/* Populate the lost and found directory with unreferenced extents */
	if (ip_endofidx < ip_eod) {
		if (lastblock_i >= ip_endofidx)
			ret = _ltfs_populate_lost_found(ltfs_ip_id(vol), lastblock_i + 1, ip_eod, vol);
		else
			ret = _ltfs_populate_lost_found(ltfs_ip_id(vol), ip_endofidx, ip_eod, vol);
		if (ret < 0)
			return ret;
	}

	if (dp_endofidx < dp_eod) {
		if (lastblock_d >= dp_endofidx)
			ret = _ltfs_populate_lost_found(ltfs_dp_id(vol), lastblock_d + 1, dp_eod, vol);
		else
			ret = _ltfs_populate_lost_found(ltfs_dp_id(vol), dp_endofidx, dp_eod, vol);
		if (ret < 0)
			return ret;
	}

	ltfs_set_index_dirty(true, false, vol->index);
	return 0;
}

/**
 * Check a volume for physical consistency. This should be called when there is some doubt about
 * the validity of the MAM parameters; it reads index files from both partitions and verifies
 * that everything seems sane. This function does not check the partition labels; use
 * ltfs_read_labels() for that.
 *
 * This function restores the consistency of the cartridge if a simple fix is possible. If it
 * returns an error, ltfsck should be used to perform a more thorough analysis of the problem.
 *
 * @param fix allow simple fixes to make the tape consistent?
 *            Here, simple means writing an additional logically unmodified copy
 *            or copies of an index file already present on the tape.
 * @param deep Allow fancy recovery procedures? In particular, this flag enables recovery in the
 *             case where extra blocks (after the last index file on a partition) are found on the
 *             tape. The nature of this recovery is controlled by the recover_extra flag.
 * @param recover_extra If deep recovery is enabled, place extra blocks in a lost&found directory?
 *                      If this is disabled, the extra blocks will be erased instead.
 * @param vol LTFS volume. If the consistency check succeeds, its index is
 *            populated with the contents of the tape.
 * @return 0 on success or a negative value on error.
 */
int ltfs_check_medium(bool fix, bool deep, bool recover_extra, bool recover_symlink, struct ltfs_volume *vol)
{

	int ret;
	bool dp_have_index = false, ip_have_index = false;
	bool dp_blocks_after, ip_blocks_after;
	bool dp_fm_after, ip_fm_after;
	bool extra_blocks;
	tape_block_t dp_eod, ip_eod, dp_endofidx = 0, ip_endofidx = 0;
	tape_block_t lastblock_d = 0, lastblock_i = 0;
	tape_partition_t ip_num, dp_num;
	struct ltfs_index *dp_index = NULL, *ip_index = NULL;;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);

	ip_num = ltfs_part_id2num(ltfs_ip_id(vol), vol);
	dp_num = ltfs_part_id2num(ltfs_dp_id(vol), vol);

	/* look for index files */
	check_err(ltfs_seek_index(vol->label->partid_ip, &ip_eod, &ip_endofidx, &ip_fm_after,
		&ip_blocks_after, recover_symlink, vol), 11214E, out_unlock);
	ip_have_index = (ret == 0);
	if (ip_have_index) {
		ip_index = vol->index;
		vol->index = NULL;
	}

	check_err(ltfs_seek_index(vol->label->partid_dp, &dp_eod, &dp_endofidx, &dp_fm_after,
		&dp_blocks_after, recover_symlink, vol), 11213E, out_unlock);
	dp_have_index = (ret == 0);
	if (dp_have_index) {
		dp_index = vol->index;
		vol->index = NULL;
	}

	/* TODO: print more detailed diagnostic data here */
	if (! ip_have_index && ! dp_have_index) {
		ltfsmsg(LTFS_ERR, 11253E);
		ret = -LTFS_NO_INDEX;
		goto out_unlock;
	}

	if (! ip_have_index)
		ltfsmsg(LTFS_INFO, 11257I);
	if (! dp_have_index)
		ltfsmsg(LTFS_INFO, 11258I);

	/* fill in missing file marks if necessary */
	if (dp_have_index && ! dp_blocks_after && ! dp_fm_after) {
		ltfsmsg(LTFS_INFO, 11255I);
		check_err(tape_seek_eod(vol->device, dp_num), 11215E, out_unlock);
		check_err(tape_write_filemark(vol->device, 1, true, true, false), 11217E, out_unlock);
		dp_fm_after = true;
		++dp_eod;
	}
	if (ip_have_index && ! ip_blocks_after && ! ip_fm_after) {
		ltfsmsg(LTFS_INFO, 11256I);
		check_err(tape_seek_eod(vol->device, ip_num), 11216E, out_unlock);
		check_err(tape_write_filemark(vol->device, 1, true, true, false), 11218E, out_unlock);
		ip_fm_after = true;
		++ip_eod;
	}

	extra_blocks =
		(dp_have_index && dp_blocks_after) || (! dp_have_index && dp_eod != 4) ||
		(ip_have_index && ip_blocks_after) || (! ip_have_index && ip_eod != 4);

	if (! deep && extra_blocks) {
		ltfsmsg(LTFS_ERR, 11220E);
		ret = -LTFS_INCONSISTENT;
		goto out_unlock;
	}

	/* Verify sanity of back/self pointers and Decide which index file to use. */
	check_err(_ltfs_check_pointers(ip_index, dp_index, vol), 11219E, out_unlock);

	if (! dp_have_index && ! ip_have_index) {
		/* no index file on the tape. set up an empty index. */
		ltfs_index_free(&dp_index);
		ltfs_index_free(&ip_index);
		check_err(ltfs_index_alloc(&vol->index, vol), 11225E, out_unlock);
		strcpy(vol->index->vol_uuid, vol->label->vol_uuid);
		vol->index->mod_time = vol->label->format_time;
		vol->index->root->creation_time = vol->index->mod_time;
		vol->index->root->change_time = vol->index->mod_time;
		vol->index->root->modify_time = vol->index->mod_time;
		vol->index->root->access_time = vol->index->mod_time;
		vol->index->root->backup_time = vol->index->mod_time;
		ltfs_set_index_dirty(true, false, vol->index);
		ret = 0;

	} else if (! ip_have_index || ! dp_have_index) {
		/* one partition is empty. check extent list for bad references. */
		if (ip_have_index)
			ret = _ltfs_check_extents(ip_index->root, ip_eod, dp_eod, vol);
		else
			ret = _ltfs_check_extents(dp_index->root, ip_eod, dp_eod, vol);
		if (ret == 0) {
			if (dp_index)
				vol->index = dp_index;
			else
				vol->index = ip_index;
			ltfs_set_index_dirty(true, false, vol->index);
		} else
			ltfsmsg(LTFS_ERR, 11221E);

	} else {
		/* we have index files on both partitions. return the newer index file. */
		if (ret == 0)
			vol->index = ip_index;
		else { /* ret == 1 */
			vol->index = dp_index;
			ltfs_set_index_dirty(true, false, vol->index);
		}
		ret = 0;
	}
	if (ret < 0)
		goto out_unlock;

	/* Set append position for index partition. */
	if (ip_have_index && ! ip_blocks_after) {
		check_err(tape_set_append_position(vol->device, ip_num, ip_index->selfptr.block - 1),
			11222E, out_unlock);
	}

	/* Recover extra blocks or schedule them to be erased. */
	if (deep && extra_blocks) {
		if (recover_extra) {
			ltfsmsg(LTFS_INFO, 11223I);
			ret = _ltfs_make_lost_found(ip_eod, dp_eod, ip_endofidx, dp_endofidx, vol);
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, 11224E, ret);
				goto out_unlock;
			}
		} else {
			_ltfs_last_ref(vol->index->root, &lastblock_d, &lastblock_i, vol);

			/* Set index partition append position. */
			if (ip_have_index && ip_blocks_after) {
				if (lastblock_i >= ip_endofidx && lastblock_i < ip_eod) {
					ltfsmsg(LTFS_INFO, 11226I);
					check_err(tape_set_append_position(vol->device, ip_num, lastblock_i),
						11229E, out_unlock);
				} else if (lastblock_i < ip_endofidx) {
					ltfsmsg(LTFS_INFO, 11226I);
					check_err(tape_set_append_position(vol->device, ip_num,
						ip_index->selfptr.block - 1), 11229E, out_unlock);
				}
			} else if (! ip_have_index && ip_eod > 4) {
				ltfsmsg(LTFS_INFO, 11226I);
				check_err(tape_set_append_position(vol->device, ip_num, 4), 11229E,out_unlock);
			}

			/* Set data partition append position. */
			if (dp_have_index && dp_blocks_after) {
				if (lastblock_d >= dp_endofidx && lastblock_d < dp_eod) {
					ltfsmsg(LTFS_INFO, 11227I);
					check_err(tape_set_append_position(vol->device, dp_num, lastblock_d),
						11228E, out_unlock);
				} else if (lastblock_d < dp_endofidx) {
					ltfsmsg(LTFS_INFO, 11227I);
					check_err(tape_set_append_position(vol->device, dp_num, dp_endofidx),
						11228E, out_unlock);
				}
			} else if (! dp_have_index && dp_eod > 4) {
				ltfsmsg(LTFS_INFO, 11227I);
				check_err(tape_set_append_position(vol->device, dp_num, 4), 11228E,out_unlock);
			}
		}

		ltfs_set_index_dirty(true, false, vol->index);
	}

	if (ip_have_index && ! ip_blocks_after)
		vol->ip_index_file_end = true;
	if (dp_have_index && ! dp_blocks_after)
		vol->dp_index_file_end = true;

	/* If necessary, restore consistency by writing index files. */
	if (vol->index->dirty) {
		if (fix) {
			ltfsmsg(LTFS_INFO, 11230I);
			/* Check index position written in Date Partition */
			/* The index position must be higher than position located by index */
			lastblock_d = 0;
			lastblock_i = 0;
			_ltfs_last_ref(vol->index->root, &lastblock_d, &lastblock_i, vol);
			if (vol->device->append_pos[dp_num] != 0) {
				if (lastblock_d > vol->device->append_pos[dp_num]) {
					ltfsmsg(LTFS_ERR, 11329E, (unsigned long long)lastblock_d, (unsigned long long)vol->device->append_pos[dp_num], dp_num);
					ret = -LTFS_INDEX_INVALID;
					goto out_unlock;
				}
			}
			/* write to data partition if it doesn't end in an index file */
			if (! dp_have_index || dp_blocks_after)
				ret = ltfs_write_index(vol->label->partid_dp, SYNC_RECOVERY, vol);
			if (!ret)
				ltfs_write_index(vol->label->partid_ip, SYNC_RECOVERY, vol);
		} else {
			ltfsmsg(LTFS_ERR, 11231E);
			ltfsmsg(LTFS_ERR, 11232E);
			ret = -LTFS_INCONSISTENT;
		}

	} else {
		ltfsmsg(LTFS_INFO, 11233I);
		ltfs_update_cart_coherency(vol);
	}

out_unlock:
	if (ip_have_index && vol->index != ip_index)
		ltfs_index_free(&ip_index);
	if (dp_have_index && vol->index != dp_index)
		ltfs_index_free(&dp_index);

	return ret;
}

/**
 * Write MAM parameters for all complete partitions.
 */
int ltfs_update_cart_coherency(struct ltfs_volume *vol)
{
	uint64_t current_vcr;
	tape_get_volume_change_reference(vol->device, &current_vcr);

	/* If the VCR is invalid, can't use MAM parameters */
	if (current_vcr == 0 || current_vcr == 0xffffffffffffffff)
		return 0;

	if (vol->ip_index_file_end) {
		if (vol->index->selfptr.partition == ltfs_ip_id(vol)) {
			vol->ip_coh.count = vol->index->generation;
			vol->ip_coh.set_id = vol->index->selfptr.block;
		}
		vol->ip_coh.version = 1; /* From PGA2 */
		vol->ip_coh.volume_change_ref = current_vcr;
		if (vol->ip_coh.uuid[0] == '\0')
			strcpy(vol->ip_coh.uuid, vol->label->vol_uuid);
		tape_set_cart_coherency(vol->device, ltfs_part_id2num(ltfs_ip_id(vol), vol),
			&vol->ip_coh);
	}

	if (vol->dp_index_file_end) {
		if (vol->index->selfptr.partition == ltfs_dp_id(vol)) {
			vol->dp_coh.count = vol->index->generation;
			vol->dp_coh.set_id = vol->index->selfptr.block;
		}
		vol->dp_coh.version = 1; /* From PGA2 */
		vol->dp_coh.volume_change_ref = current_vcr;
		if (vol->dp_coh.uuid[0] == '\0')
			strcpy(vol->dp_coh.uuid, vol->label->vol_uuid);
		tape_set_cart_coherency(vol->device, ltfs_part_id2num(ltfs_dp_id(vol), vol),
			&vol->dp_coh);
	}

	return 0;
}

/**
 * Write an index file to the given partition unless that partition already ends in an index file.
 * This function is called for a partition immediately before writing data blocks to the
 * other partition.
 * The caller must hold a write lock on the volume, as well as the device lock.
 * @param partition Partition to write to.
 * @param vol LTFS volume.
 * @return 0 on success or a negative value on error.
 */
int ltfs_write_index_conditional(char partition, struct ltfs_volume *vol)
{
	int ret = 0;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);

	if (partition == ltfs_ip_id(vol) && ! vol->ip_index_file_end)
		ret = ltfs_write_index(partition, SYNC_CASCHE_PRESSURE, vol);
	else if (partition == ltfs_dp_id(vol) &&
	         (! vol->dp_index_file_end ||
	          (vol->ip_index_file_end && vol->index->selfptr.partition == ltfs_ip_id(vol))))
		ret = ltfs_write_index(partition, SYNC_CASCHE_PRESSURE, vol);

	return ret;
}

int ltfs_split_symlink( struct ltfs_volume *vol )
{
	size_t i, size;
	struct dentry *d, *workd;
	int ret=0;
	char *name, *lfdir, *path, *tok, *next_tok;
	bool basedir=true, use_iosche=false;
	char value[32];
	ltfs_file_id id;

	if ( iosched_initialized(vol) ) use_iosche=true;

	/* check lost_and_found directory and make if it doesn't exist */
	asprintf( &lfdir, "/%s", LTFS_LOSTANDFOUND_DIR );
	ret = fs_path_lookup(lfdir, 0, &workd, vol->index);
	if ( ret==-LTFS_NO_DENTRY  ) {
		ret = ltfs_fsops_create( lfdir, true, false, false, &workd, vol);
		if ( ret<0 ) {
			free(lfdir);
			return ret;
		} else {
			basedir=false;
		}
	} else if ( ret<0 ) {
		free(lfdir);
		return ret;
	}
	ret = ltfs_fsops_close( workd, true, true, use_iosche, vol);
	path=strdup(lfdir);

	/* loop for conflicted files */
	for( i=0; i<(vol->index->symerr_count); i++ ){
		d = *(vol->index->symlink_conflict+i);

		ret = fs_dentry_lookup(d, &name);
		if (ret<0) goto out_func;

		tok=strtok( name+1, "/" );
		next_tok=strtok( NULL, "/" );

		/* check directory path and make if it doesn't exist */
		while( next_tok ){
			asprintf( &path, "%s/%s", path, tok );
			if ( basedir ) {
				ret = fs_path_lookup(path, 0, &workd, vol->index);
				if ( ret==-LTFS_NO_DENTRY  )
					basedir=false;
				else if ( ret<0 )
					goto err_out_func;
			}

			if( !basedir ) {
				ret = ltfs_fsops_create( path, true, false, false, &workd, vol);
				if ( ret<0 )
					goto err_out_func;

			}
			ret = ltfs_fsops_close( workd, true, true, use_iosche, vol);
			tok = next_tok;
			next_tok=strtok( NULL, "/" );
		}

		/* Make filename with path in lost_and_found */
		asprintf( &path, "%s/%s", path, tok);
		ret = fs_path_lookup(path, 0, &workd, vol->index);
		if ( ret == 0 ) {
			/* delete same name old symlink */
			ret = ltfs_fsops_unlink( path, &id, vol);
			if (ret<0)
				goto err_out_func;
		} else if ( ret != -LTFS_NO_DENTRY )
			goto err_out_func;

		ret = ltfs_fsops_symlink_path( (const char*)d->target.name, (const char*)path, &id, vol );
		if (ret<0)
			goto err_out_func;

		/* get old file's EA info then store it in the new file*/
		memset( value, 0, sizeof(value));
		ret = xattr_get(d, LTFS_LIVELINK_EA_NAME, value, sizeof(value), vol);
		if (ret>0) {
			size = ret;
			ret = fs_path_lookup(path, 0, &workd, vol->index);
			if (ret<0)
				goto err_out_func;
			ret = xattr_set_mountpoint_length( workd, value, size );
			if (ret<0)
				goto err_out_func;
			ret = xattr_do_remove(d, LTFS_LIVELINK_EA_NAME, true, vol);
			if (ret<0)
				goto err_out_func;
			ret = ltfs_fsops_close( workd, true, true, use_iosche, vol);
			if (ret<0)
				goto err_out_func;
		}

		d->isslink = false;
		free(d->target.name);
		free(name);
		strcpy(path,lfdir);
		basedir=true;
	}
	goto out_func;

err_out_func:
	free(name);
out_func:
	free(lfdir);
	free(path);
	return ret;
}
