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
** FILE NAME:       ltfs_internal.h
**
** DESCRIPTION:     Defines private interfaces to core LTFS functionality.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
**                  Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/

#ifndef __ltfs_internal_h__
#define __ltfs_internal_h__

/** \file
 * Low-level functions for libltfs. Normal applications should use the functions prototyped
 * in ltfs.h, but the functions here may be useful for an application (e.g., mkltfs)
 * which needs to interact at a lower level with the tape and/or the LTFS data structures.
 */

#include "ltfs.h"

int ltfs_index_alloc(struct ltfs_index **index, struct ltfs_volume *vol);

void _ltfs_index_free(bool force, struct ltfs_index **index);
#define ltfs_index_free(idx)					\
	_ltfs_index_free(false, idx)
#define ltfs_index_free_force(idx)				\
	_ltfs_index_free(true, idx)

int ltfs_check_medium(bool fix, bool deep, bool recover_extra, bool recover_symlink, struct ltfs_volume *vol);
int ltfs_read_labels(bool trial, struct ltfs_volume *vol);
int ltfs_read_one_label(tape_partition_t partition, struct ltfs_label *label,
	struct ltfs_volume *vol);
int ltfs_read_index(uint64_t eod_pos, bool recover_symlink, bool skip_dir, struct ltfs_volume *vol);
int ltfs_read_indexfile(char* filename, bool recover_symlink, struct ltfs_volume *vol);

int ltfs_update_cart_coherency(struct ltfs_volume *vol);
int ltfs_write_index_conditional(char partition, struct ltfs_volume *vol);
bool ltfs_is_valid_partid(char id);

int ltfs_seek_index(char partition, tape_block_t *eod_pos, tape_block_t *index_end_pos,
			bool *fm_after, bool *blocks_after, bool recover_symlink, struct ltfs_volume *vol);
void _ltfs_last_ref(struct dentry *d, tape_block_t *dp_last, tape_block_t *ip_last,
					struct ltfs_volume *vol);
int ltfs_split_symlink(struct ltfs_volume *vol);
int ltfs_set_dentry_dirty(struct dentry *d, struct ltfs_volume *vol);

#endif /* __ltfs_internal_h__ */
