/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2025 IBM Corp. All rights reserved.
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
** FILE NAME:       label.c
**
** DESCRIPTION:     Implements label handling functions.
**
** AUTHOR:          Takashi Ashida
**                  IBM Yamato, Japan
**                  ashida@jp.ibm.com
**
*************************************************************************************
*/

#include "label.h"
#include "libltfs/ltfslogging.h"
#include "ltfs_internal.h"

/**
 * Allocate label.
 * @param label To be set the allocated label pointer
 * @return 0 if labels match or a negative value otherwise.
 */
int label_alloc(struct ltfs_label **label)
{
	struct ltfs_label *newlabel;

	CHECK_ARG_NULL(label, -LTFS_NULL_ARG);

	newlabel = calloc(1, sizeof(struct ltfs_label));
	if (!newlabel) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return -LTFS_NO_MEMORY;
	}
	newlabel->version = LTFS_LABEL_VERSION;

	*label = newlabel;
	return 0;
}

/**
 * Free label
 * @param label label to be free
 */
void label_free(struct ltfs_label **label)
{
	if (label && *label) {
		if ((*label)->creator)
			free((*label)->creator);
		free(*label);
		*label = NULL;
	}
}

/**
 * Check whether two labels are equal.
 * @param label1 the first label
 * @param label2 the second label
 * @return 0 if labels match or a negative value otherwise.
 */
int label_compare(struct ltfs_label *label1, struct ltfs_label *label2)
{
	char *tmp;

	CHECK_ARG_NULL(label1, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(label2, -LTFS_NULL_ARG);

	if (strncmp(label1->barcode, label2->barcode, 6)) {
		ltfsmsg(LTFS_ERR, 11182E);
		return -LTFS_LABEL_MISMATCH;

	} else if (strncmp(label1->vol_uuid, label2->vol_uuid, 36)) {
		ltfsmsg(LTFS_ERR, 11183E);
		return -LTFS_LABEL_MISMATCH;

	} else if (label1->format_time.tv_sec != label2->format_time.tv_sec ||
		label1->format_time.tv_nsec != label2->format_time.tv_nsec) {
		ltfsmsg(LTFS_ERR, 11184E);
		return -LTFS_LABEL_MISMATCH;

	} else if (label1->blocksize != label2->blocksize) {
		ltfsmsg(LTFS_ERR, 11185E);
		return -LTFS_LABEL_MISMATCH;

	} else if (label1->enable_compression != label2->enable_compression) {
		ltfsmsg(LTFS_ERR, 11186E);
		return -LTFS_LABEL_MISMATCH;

	} else if (! ltfs_is_valid_partid(label1->partid_dp) ||
			   ! ltfs_is_valid_partid(label1->partid_ip)) {
		ltfsmsg(LTFS_ERR, 11187E);
		return -LTFS_LABEL_MISMATCH;

	} else if (label1->partid_dp == label1->partid_ip) {
		ltfsmsg(LTFS_ERR, 11188E);
		return -LTFS_LABEL_MISMATCH;

	} else if (label2->partid_dp != label1->partid_dp ||
			   label2->partid_ip != label1->partid_ip) {
		ltfsmsg(LTFS_ERR, 11189E);
		return -LTFS_LABEL_MISMATCH;

	} else if ((label1->this_partition != label1->partid_dp &&
			    label1->this_partition != label1->partid_ip) ||
			   (label2->this_partition != label1->partid_dp &&
			    label2->this_partition != label1->partid_ip)) {
		ltfsmsg(LTFS_ERR, 11190E);
		return -LTFS_LABEL_MISMATCH;

	} else if (label1->this_partition == label2->this_partition) {
		ltfsmsg(LTFS_ERR, 11191E, label1->this_partition);
		return -LTFS_LABEL_MISMATCH;

	} else if (label1->version != label2->version) {
		ltfsmsg(LTFS_ERR, 11197E);
		return -LTFS_LABEL_MISMATCH;
	}

	/* check for valid barcode number */
	if (label1->barcode[0] != ' ') {
		tmp = label1->barcode;
		while (*tmp) {
			if ((*tmp < '0' || *tmp > '9') && (*tmp < 'A' || *tmp > 'Z')) {
				ltfsmsg(LTFS_ERR, 11192E);
				return -LTFS_LABEL_MISMATCH;
			}
			++tmp;
		}
	}

	return 0;
}

/**
 * Generate an 80-byte ANSI label.
 * @param vol LTFS volume containing the barcode number
 * @param label output buffer, must be at least 80 bytes
 */
void label_make_ansi_label(struct ltfs_volume *vol, char *label, size_t size)
{
	size_t barcode_len;
	memset(label,' ',size);
	memcpy(label,"VOL1",4);
	barcode_len = strlen(vol->label->barcode);
	if (barcode_len > 0)
		memcpy(label+4, vol->label->barcode, barcode_len > 6 ? 6 : barcode_len);
	label[10] = 'L';
	memcpy(label+24,"LTFS",4);
	/* TODO: fill "owner identifier" field? */
	label[size-1] = '4';
}
