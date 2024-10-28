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
** FILE NAME:       tape_drivers/generic/file/itdtimg_tc.c
**
** DESCRIPTION:     Implements a tape simulator for ITDT tape image.
**
** AUTHORS:         Bernd Freitag
**                  IBM Germany
**                  bfreitag@de.ibm.com
**
**                  Thomas Pietralla
**                  IBM Germany
**                  Thomas.Pietralla@de.ibm.com
**
*************************************************************************************
*/
#ifdef mingw_PLATFORM
#include "libltfs/arch/win/win_util.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <libgen.h>
#include <dirent.h>

#include "libltfs/ltfs_fuse_version.h"
#include <fuse.h>

#include "ltfs_copyright.h"
#include "libltfs/ltfslogging.h"
#include "libltfs/ltfs.h"
#include "libltfs/ltfs_endian.h"
#include "libltfs/tape_ops.h"
#include "libltfs/ltfs_error.h"

volatile char *copyright = LTFS_COPYRIGHT_0"\n"LTFS_COPYRIGHT_1"\n"LTFS_COPYRIGHT_2"\n" \
	LTFS_COPYRIGHT_3"\n"LTFS_COPYRIGHT_4"\n"LTFS_COPYRIGHT_5"\n";

/* Default directory where the emulated tape contents go to */
const char *itdtimage_default_device = "tape.img";

#define DEBUG 0
#define MAX_PARTITIONS 2
#define KB   (1024)
#define MB   (KB * 1024)
#define GB   (MB * 1024)
#define FILE_DEBUG_MAX_BLOCK_SIZE (4 * MB)
#define XML_MIN_DATA_SIZE 1024

/* O_BINARY is defined only in MinGW */
#ifndef O_BINARY
#define O_BINARY 0
#endif

#define MISSING_EOD (0xFFFFFFFFFFFFFFFFLL)

/* For drive link feature */
#ifdef mingw_PLATFORM
#define DRIVE_LIST_DIR	"ltfs"
#else
#define DRIVE_LIST_DIR	"/tmp"
#endif

/**
 * Emulator-specific data structures, used in lieu of a file descriptor
 */
const int itdtimage_attributes[] = { 0x9,
									 0x800,
									 0x801,
									 0x802,
									 0x803,
									 0x805,
									 0x806,
									 0x80b,
									 0x80c};

struct itdtimage_runlist {
	long long count_rec;   /**< Record Count */
	long long length_rec;  /**< Length of the record */
	long long pos_tape;    /**< Tape position */
	long long offset_img;  /**< Offset of the image */
};

struct itdtimage_attrlist {
	unsigned char partition;
	short         attr_id;
	short         length;
	long long     offset_img;
};

struct itdtimage_data {
	bool device_reserved;				 /**< True when the device has been successfully reserved */
	bool medium_locked;				     /**< True when preventing medium removal by the user */
	struct tc_position current_position; /**< Current tape position (partition, block) */
	uint32_t max_block_size;			 /**< Maximum block size, in bytes */
	char *filename;					     /**< File contains the pointer to directory where blocks reside */
	bool ready;						     /**< Is the "tape" loaded? */
	uint64_t last_block[MAX_PARTITIONS]; /**< Last positions for all partitions */
	uint64_t eod[MAX_PARTITIONS];		 /**< Append positions (1 + last block) for all partitions */

	uint64_t write_pass_prev;			 /**< Previous write Pass */
	uint64_t write_pass;				 /**< Current write Pass of LTO drive for consistency check*/

	int rll_count;
	struct itdtimage_runlist *runlist;
	int attr_count;
	struct itdtimage_attrlist *attr_info;
	FILE *img_file;
	int  partitions;					 /**< Number of available partitions */
	unsigned long long part1_img_offset;
	unsigned long long part_unit_size;
	unsigned long long part0_size;
	unsigned long long part1_size;
	unsigned long long vcilength;
	char version;
	unsigned long long byte_count;
	unsigned long long density_code;
	char *serial_number;                 /**< Serial number of this dummy tape device */
};

/* local prototypes */
int _itdtimage_write_eod(struct itdtimage_data *state);
int _itdtimage_remove_current_record(const struct itdtimage_data *state);
int _itdtimage_remove_record(const struct itdtimage_data *state,int partition, uint64_t blknum);
int _itdtimage_space_fm(struct itdtimage_data *state, uint64_t count, bool back);
int _itdtimage_space_rec(struct itdtimage_data *state, uint64_t count, bool back);

long long _itdtimage_getattr_offest(const struct itdtimage_data *state, int part, int id);
long long _itdtimage_getattr_len(const struct itdtimage_data *state, int part, int id);
long long _itdtimage_getrec_offset(const struct itdtimage_data *state,int part, uint64_t pos);
long long _itdtimage_getrec_len(const struct itdtimage_data *state,int part, uint64_t pos);
int _itdtimage_free(struct itdtimage_data *state);

char *memstr(const char *s, const char *find, size_t slen);
char *_read_XML_tag(char *buf, int buf_len, char *needle);
unsigned long long _read_XML_tag_value(char *buf, int buf_len, char *needle);
unsigned long long _get_file_size(FILE *fStream);
unsigned long long _seek_file(FILE *file, unsigned long long position);

/* Command-line options recognized by this module */
static struct fuse_opt filedebug_opts[] = {
	FUSE_OPT_END
};

int null_parser(void *priv, const char *arg, int key, struct fuse_args *outargs)
{
	return 1;
}

int itdtimage_parse_opts(void *vstate, void *opt_args)
{
	struct itdtimage_data *state = (struct itdtimage_data *) vstate;
	struct fuse_args *args = (struct fuse_args *) opt_args;
	int ret;
	/* fuse_opt_parse can handle a NULL device parameter just fine */
	ret = fuse_opt_parse(args, state, filedebug_opts, null_parser);
	if (ret < 0)
		return ret;

	return 0;
}

void itdtimage_help_message(const char *progname)
{
	ltfsresult(31199I, itdtimage_default_device);
}

int itdtimage_open(const char *name, void **handle)
{
	struct itdtimage_data *state;
	long long length;
	int read_length = XML_MIN_DATA_SIZE;
	char *buffer;
	int index, partition;
	unsigned long long offset = 0, num_rec = 0;
	int currentPartition = 0, i, j;
	long bytes_read;

	ltfsmsg(LTFS_INFO, 31000I, name);

	CHECK_ARG_NULL(handle, -LTFS_NULL_ARG);
	*handle = NULL;

	state = (struct itdtimage_data *)calloc(1,sizeof(struct itdtimage_data));
	if (!state) {
		ltfsmsg(LTFS_ERR, 10001E, "itdtimage_open: private data");
		return -EDEV_NO_MEMORY;
	}

	/*
	 *
	 *  At the point name must be a regular file
	 *  open image file
	 */
	state->img_file = fopen(name, "r");
	if ( !state->img_file ) {
		ltfsmsg(LTFS_ERR, 31001E, name, "fopen", (unsigned long long)errno);
		_itdtimage_free(state);
		return -EDEV_DEVICE_UNOPENABLE;
	}
	state->filename = SAFE_STRDUP(name);
	if ( !state->filename ) {
		ltfsmsg(LTFS_ERR, 10001E, "itdtimage_open: filename");
		_itdtimage_free(state);
		return -EDEV_NO_MEMORY;
	}

	/* seek to end of image file */
	length = _get_file_size(state->img_file);
	if ( length < XML_MIN_DATA_SIZE)
		read_length = length;
	if (_seek_file(state->img_file, length-read_length)!=0){
		ltfsmsg(LTFS_ERR, 31002E, (long long)length-read_length, state->filename, (unsigned long long)errno);
		_itdtimage_free(state);
		return -EDEV_HARDWARE_ERROR;
	}

	/*
	 * letzte 2K aus image file auslese
	 * allocate memory:
	 */
	buffer = calloc (1,read_length);
	if (buffer==NULL){
		// add debug message
		_itdtimage_free(state);
		return -EDEV_NO_MEMORY;
	}
	/* read data as a block */
	bytes_read = fread(buffer,1,read_length,state->img_file);
	if (bytes_read!=read_length){
		_itdtimage_free(state);
		free(buffer);
		return -EDEV_HARDWARE_ERROR;
	}

	/* XML tags and values read from the end of the image file */
	state->rll_count      = _read_XML_tag_value(buffer,bytes_read, "rllCount");
	state->partitions     = _read_XML_tag_value(buffer,bytes_read, "partitionCount");
	state->part_unit_size = _read_XML_tag_value(buffer,bytes_read, "partitionUnitSize");
	state->part0_size     = _read_XML_tag_value(buffer,bytes_read, "partitionSize_0");
	state->part1_size     = _read_XML_tag_value(buffer,bytes_read, "partitionSize_1");
	state->vcilength      = _read_XML_tag_value(buffer,bytes_read, "vcilength");
	state->version        = _read_XML_tag_value(buffer,bytes_read, "version");
	state->byte_count     = _read_XML_tag_value(buffer,bytes_read, "byteCount");
	state->density_code   = _read_XML_tag_value(buffer,bytes_read, "densityCode");

	if ( ! state->rll_count ){
		ltfsmsg(LTFS_ERR, 31001E, state->filename, "Meta Info [rll_count] is not valid", (unsigned long long)state->rll_count);
		_itdtimage_free(state);
		free(buffer);
		return -EDEV_DEVICE_UNOPENABLE;
	}
	if ( state->version < 2 ){
		ltfsmsg(LTFS_ERR, 31001E, state->filename, "Unsupported ITDT Image file version", (unsigned long long)state->version);
		_itdtimage_free(state);
		free(buffer);
		return -EDEV_DEVICE_UNOPENABLE;
	}
	if ( ! state->byte_count ){
		ltfsmsg(LTFS_ERR, 31001E, state->filename, "Meta Info [byte_count] is not valid", state->byte_count);
		_itdtimage_free(state);
		free(buffer);
		return -EDEV_DEVICE_UNOPENABLE;
	}

	/* allocate memory for rllList */
	state->runlist = (struct itdtimage_runlist*)malloc(state->rll_count * sizeof(struct itdtimage_runlist));
	if (state->runlist==NULL){
		_itdtimage_free(state);
		free(buffer);
		return -EDEV_NO_MEMORY;
	}
	state->attr_count=0;
	for (int partition=0; partition<2; partition++)  {
		for (j = 0; j < (int)(sizeof(itdtimage_attributes)/sizeof(int)); j++) {
			int attr = itdtimage_attributes[j];
			char attrTag[50];
			char *stringValue;
			sprintf(attrTag,"attr_%d_%x",partition,attr);
			if ((stringValue=_read_XML_tag(buffer,read_length,attrTag))!=NULL) {
				free(stringValue);
				state->attr_count++;
			}
		}
	}
	if (state->attr_count==0){
		ltfsmsg(LTFS_ERR, 31001E, state->filename, "Meta Info [attr_] is not valid", (unsigned long long)state->attr_count);
		_itdtimage_free(state);
		free(buffer);
		return -EDEV_DEVICE_UNOPENABLE;
	}

	state->attr_info = (struct itdtimage_attrlist*)malloc(state->attr_count*sizeof(struct itdtimage_attrlist));
	if (state->attr_info==NULL){
		_itdtimage_free(state);
		free(buffer);
		return -EDEV_NO_MEMORY;
	}

	index = 0;
	for (partition = 0; partition < 2; partition++)  {
		for (j = 0; j < (int)(sizeof(itdtimage_attributes)/sizeof(int)); j++) {
			char attrTag[50];
			int attr=itdtimage_attributes[j];
			char *stringValue;
			sprintf(attrTag,"attr_%d_%x",partition,attr);
			if ((stringValue=_read_XML_tag(buffer,read_length,attrTag))!=NULL){
				long long offset, length;
				sscanf(stringValue, "%lld,%lld", &offset, &length);
				state->attr_info[index].attr_id=attr;
				state->attr_info[index].partition=partition;
				state->attr_info[index].length=length;
				state->attr_info[index].offset_img=offset;
				index++;
			}
		}
	}

	/* fill rllList with data from image file */
	if ( _seek_file(state->img_file, state->byte_count) ) {
		ltfsmsg(LTFS_ERR, 31002E, (long long)state->byte_count, state->filename, (unsigned long long)errno);
		_itdtimage_free(state);
		free(buffer);
		return -EDEV_HARDWARE_ERROR;
	}

	offset = num_rec = 0;
	currentPartition=0;
	for (i = 0; i < MAX_PARTITIONS; i++)
		state->eod[currentPartition] = MISSING_EOD;

	for (i = 0; i < state->rll_count; i++) {
		int index=0;
		long long xferSize = 0;
		long long count = 0;
		memset(buffer,0,sizeof(read_length));
		do {
			bytes_read =fread(buffer+index,1,1,state->img_file);
			index++;
		}while ((bytes_read==1) && (buffer[index-1]!=0xa));
		sscanf(buffer, "%lld,%lld", &xferSize, &count);
		state->runlist[i].length_rec=xferSize;
		state->runlist[i].count_rec=count;
		state->runlist[i].offset_img=offset;
		state->runlist[i].pos_tape=num_rec;
		if(count>0)
			num_rec += count;

		if(xferSize > 0)
			offset += xferSize*count;
		else
			if (xferSize == -1){
				state->eod[currentPartition]=num_rec-1;
				num_rec = 0;
				currentPartition++;
				/* Add marker for end of partition 0 */
				if(state->part1_img_offset == 0)
					state->part1_img_offset = i+1;
			}
	}

	state->ready = false;
	state->max_block_size = 16 * MB;
	*handle = (void *) state;
	free(buffer);
	return 0;
}

int itdtimage_reopen(const char *name, void *vstate)
{
	/* Do nothing */
	return 0;
}

int itdtimage_close(void *vstate)
{
	struct itdtimage_data *state = (struct itdtimage_data *)vstate;
	ltfsmsg(LTFS_INFO, 31003I, state->filename);
	_itdtimage_free(state);
	return 0;
}

int _itdtimage_free(struct itdtimage_data *state)
{
	if (state) {
		if (state->filename)
			free(state->filename);
		if (state->runlist)
			free(state->runlist);
		if (state->attr_info)
			free(state->attr_info);
		if (state->img_file)
			fclose(state->img_file);
		free(state);
	}
	return 0;
}

int itdtimage_close_raw(void *vstate)
{
	return 0;
}

int itdtimage_is_connected(const char *devname)
{
	return 0;
}

int itdtimage_inquiry(void *vstate, struct tc_inq *inq)
{
	memset(inq, 0, sizeof(struct tc_inq));
	return DEVICE_GOOD;
}

int itdtimage_inquiry_page(void *vstate, unsigned char page, struct tc_inq_page *inq)
{
	memset(inq, 0, sizeof(struct tc_inq_page));
	return DEVICE_GOOD;
}

int itdtimage_test_unit_ready(void *vstate)
{
	struct itdtimage_data *state = (struct itdtimage_data *)vstate;
	if (!state->ready)
		return -EDEV_NEED_INITIALIZE;
	return DEVICE_GOOD;
}

int itdtimage_read(void *vstate, char *buf, size_t count, struct tc_position *pos,
				   const bool unusual_size)
{
	struct itdtimage_data *state = (struct itdtimage_data *)vstate;
	int ret;
	long long offset;
	size_t length_rec;

	ltfsmsg(LTFS_DEBUG, 31004D, (unsigned long long)count, state->current_position.partition,
			(unsigned long long)state->current_position.block,
			(unsigned long long)state->current_position.filemarks);

	if (!state->ready) {
		ltfsmsg(LTFS_ERR, 31005E);
		return -EDEV_NOT_READY;
	}

	/* check for EOD (reading is an error) */
	if (state->eod[state->current_position.partition] == state->current_position.block) {
		return -EDEV_EOD_DETECTED;
	}

	offset = _itdtimage_getrec_offset(state, state->current_position.partition, state->current_position.block);
	if (offset==-1){
		return -EDEV_HARDWARE_ERROR;
	}

	length_rec = _itdtimage_getrec_len(state, state->current_position.partition, state->current_position.block);
	if (count < length_rec)
		length_rec=count;
	if ( _seek_file(state->img_file, offset) ){
	 	ltfsmsg(LTFS_ERR, 31002E , (long long)length_rec, state->filename, offset);
		return -EDEV_HARDWARE_ERROR;
	}

	ret = fread(buf, 1, length_rec, state->img_file);
	++state->current_position.block;
	pos->block = state->current_position.block;
	return ret;
}

int itdtimage_write(void *vstate, const char *buf, size_t count, struct tc_position *pos)
{
	return -EDEV_WRITE_PROTECTED;
}

int itdtimage_writefm(void *vstate, size_t count, struct tc_position *pos, bool immed)
{
	return -EDEV_WRITE_PROTECTED;
}

int itdtimage_rewind(void *vstate, struct tc_position *pos)
{
	struct itdtimage_data *state = (struct itdtimage_data *)vstate;
	if (!state->ready) {
		ltfsmsg(LTFS_ERR, 31006E);
		return -EDEV_NOT_READY;
	}
	/* Does rewinding reset the partition? */
	state->current_position.block = 0;
	state->current_position.filemarks = 0;
	pos->block = state->current_position.block;
	pos->filemarks = 0;
	pos->early_warning = false;
	pos->programmable_early_warning = false;

	return DEVICE_GOOD;
}

int itdtimage_locate(void *vstate, struct tc_position dest, struct tc_position *pos)
{
	int rc = 0;
	struct itdtimage_data *state = (struct itdtimage_data *)vstate;
	tape_filemarks_t count_fm = 0;
	int i;

	ltfsmsg(LTFS_DEBUG, 31197D, "locate", (unsigned long long)dest.partition,
			(unsigned long long)dest.block);

	if (!state->ready) {
		ltfsmsg(LTFS_ERR, 31007E);
		rc = -EDEV_NOT_READY;
		return rc;
	}
	if (dest.partition >= MAX_PARTITIONS) {
		ltfsmsg(LTFS_ERR, 31008E, (unsigned long)dest.partition);
		rc = -EDEV_INVALID_ARG;
		return rc;
	}

	state->current_position.partition = dest.partition;
	if (state->eod[dest.partition] == MISSING_EOD &&
		state->last_block[dest.partition] < dest.block)
		state->current_position.block = state->last_block[dest.partition] + 1;
	else if (state->eod[dest.partition] < dest.block)
		state->current_position.block = state->eod[dest.partition];
	else
		state->current_position.block = dest.block;
	pos->partition = state->current_position.partition;
	pos->block	 = state->current_position.block;

	for(i = 0; i < state->rll_count; i++) {
		if ( state->runlist[i].pos_tape >= (long long)state->current_position.block )
			break;
		if ( ! state->runlist[i].length_rec )
			count_fm++;
	}
	rc = 0;
	state->current_position.filemarks = count_fm;
	pos->filemarks = state->current_position.filemarks;
	return rc;
}

int itdtimage_space(void *vstate, size_t count, TC_SPACE_TYPE type, struct tc_position *pos)
{
	int rc = 0;
	struct itdtimage_data *state = (struct itdtimage_data *)vstate;
	tape_filemarks_t count_fm = 0;
	if (!state->ready) {
		ltfsmsg(LTFS_ERR, 31009E);
		rc = -EDEV_NOT_READY;
		return rc;
	}

	switch(type) {
	case TC_SPACE_EOD:
		ltfsmsg(LTFS_DEBUG, 31195D, "space to EOD");
		state->current_position.block = state->eod[state->current_position.partition];
		if(state->current_position.block == MISSING_EOD) {
			rc = -EDEV_RW_PERM;
			return rc;
		} else
			rc = 0;
		break;
	case TC_SPACE_FM_F:
		ltfsmsg(LTFS_DEBUG, 31196D, "space forward file marks", (unsigned long long)count);
		if(state->current_position.block == MISSING_EOD) {
			rc = -EDEV_RW_PERM;
			return rc;
		} else
			rc = _itdtimage_space_fm(state, count, false);
		break;
	case TC_SPACE_FM_B:
		ltfsmsg(LTFS_DEBUG, 31196D, "space back file marks", (unsigned long long)count);
		if(state->current_position.block == MISSING_EOD) {
			rc = -EDEV_RW_PERM;
			return rc;
		} else
			rc = _itdtimage_space_fm(state, count, true);
		break;
	case TC_SPACE_F:
		ltfsmsg(LTFS_DEBUG, 31196D, "space forward records", (unsigned long long)count);
		if(state->current_position.block == MISSING_EOD) {
			rc = -EDEV_RW_PERM;
			return rc;
		} else
			rc = _itdtimage_space_rec(state, count, false);
		break;
	case TC_SPACE_B:
		ltfsmsg(LTFS_DEBUG, 31196D, "space back records", (unsigned long long)count);
		if(state->current_position.block == MISSING_EOD) {
			rc = -EDEV_RW_PERM;
			return rc;
		} else
			rc = _itdtimage_space_rec(state, count, true);
		break;
	default:
		ltfsmsg(LTFS_ERR, 31010E);
		rc = -EDEV_INVALID_ARG;
		return rc;
	}

	pos->block = state->current_position.block;
	for(int i = 0; i < (int)state->rll_count; i++) {
		if (state->runlist[i].pos_tape>=(long long)state->current_position.block)
			break;
		if (state->runlist[i].length_rec==0)
			count_fm++;
	}

	state->current_position.filemarks = count_fm;
	pos->filemarks = state->current_position.filemarks;
	ltfsmsg(LTFS_DEBUG, 31011D,
			(unsigned long long)state->current_position.partition,
			(unsigned long long)state->current_position.block,
			(unsigned long long)state->current_position.filemarks,
			(int)state->device_reserved,
			(int)state->medium_locked,
			(int)state->ready);

	return rc;
}

/**
 * NOTE: real tape drives erase from the current position. This function erases the entire
 * partition. The erase function is unused externally, but this implementation will need to be
 * fixed if it is ever needed.
 */
int itdtimage_erase(void *vstate, struct tc_position *pos, bool long_erase)
{
	int ret;
	struct itdtimage_data *state = (struct itdtimage_data *)vstate;

	if (!state->ready) {
		ltfsmsg(LTFS_ERR, 31021E);
		return -EDEV_NOT_READY;
	}

	ltfsmsg(LTFS_DEBUG, 31022D, (unsigned long)state->current_position.partition);
	pos->block	 = state->current_position.block;
	pos->filemarks = state->current_position.filemarks;

	ret = _itdtimage_write_eod(state);
	return ret;
}

int itdtimage_load(void *vstate, struct tc_position *pos)
{
	struct itdtimage_data *state = (struct itdtimage_data *)vstate;

	if (state->ready)
		return DEVICE_GOOD; /* already loaded the tape */
	state->ready = true;
	state->current_position.partition = 0;
	state->current_position.block	 = 0;
	state->current_position.filemarks = 0;
	state->partitions = MAX_PARTITIONS;
	state->write_pass_prev = 0;
	state->write_pass = 0;
	pos->partition = state->current_position.partition;
	pos->block	 = state->current_position.block;
	pos->filemarks = state->current_position.filemarks;
	return DEVICE_GOOD;
}

int itdtimage_unload(void *vstate, struct tc_position *pos)
{
	struct itdtimage_data *state = (struct itdtimage_data *)vstate;
	state->ready = false;
	state->current_position.partition = 0;
	state->current_position.block	 = 0;
	state->current_position.filemarks = 0;
	pos->partition = state->current_position.partition;
	pos->block	 = state->current_position.block;
	pos->filemarks = state->current_position.filemarks;
	return DEVICE_GOOD;
}

int itdtimage_readpos(void *vstate, struct tc_position *pos)
{
	struct itdtimage_data *state = (struct itdtimage_data *)vstate;

	if (!state->ready) {
		ltfsmsg(LTFS_ERR, 31012E);
		return -EDEV_NOT_READY;
	}

	pos->partition = state->current_position.partition;
	pos->block = state->current_position.block;
	pos->filemarks = state->current_position.filemarks;

	ltfsmsg(LTFS_DEBUG, 31198D, "readpos", (unsigned long long)state->current_position.partition,
			(unsigned long long)state->current_position.block,
			(unsigned long long)state->current_position.filemarks);
	return DEVICE_GOOD;
}

int itdtimage_setcap(void *vstate, uint16_t proportion)
{
	struct itdtimage_data *state = (struct itdtimage_data *)vstate;
	struct tc_position pos;

	if(state->current_position.partition != 0 ||
	   state->current_position.block != 0)
	{
		ltfsmsg(LTFS_ERR, 31013E);
		return -EDEV_ILLEGAL_REQUEST;
	}

	state->partitions = 1;

	/* erase all partitions */
	state->current_position.partition = 1;
	state->current_position.block = 0;
	itdtimage_erase(state, &pos, false);
	state->current_position.partition = 0;
	state->current_position.block = 0;
	itdtimage_erase(state, &pos, false);

	return DEVICE_GOOD;
}

int itdtimage_format(void *vstate, TC_FORMAT_TYPE format, const char *vol_name, const char *barcode_name, const char *vol_mam_uuid)
{
	struct itdtimage_data *state = (struct itdtimage_data *)vstate;
	struct tc_position pos;

	if(state->current_position.partition != 0 ||
	   state->current_position.block != 0)
	{
		ltfsmsg(LTFS_ERR, 31014E);
		return -EDEV_ILLEGAL_REQUEST;
	}

	switch(format){
	case TC_FORMAT_DEFAULT:
		state->partitions = 1;
		break;
	case TC_FORMAT_PARTITION:
	case TC_FORMAT_DEST_PART:
		state->partitions = 2;
		break;
	default:
		ltfsmsg(LTFS_ERR, 31015E);
		return -EDEV_INVALID_ARG;
	}

	/* erase all partitions */
	state->current_position.partition = 1;
	state->current_position.block = 0;
	itdtimage_erase(state, &pos, false);
	state->current_position.partition = 0;
	state->current_position.block = 0;
	itdtimage_erase(state, &pos, false);
	return DEVICE_GOOD;
}

int itdtimage_remaining_capacity(void *vstate, struct tc_remaining_cap *cap)
{
	struct itdtimage_data *state = (struct itdtimage_data *)vstate;
	if (!state->ready) {
		ltfsmsg(LTFS_ERR, 31016E);
		return DEVICE_GOOD;
	}
	cap->remaining_p0 = 6UL * (GB / MB);
	cap->max_p0	   = 6UL * (GB / MB);
	if(state->partitions == 2) {
		cap->remaining_p1 = 6UL * (GB / MB);
		cap->max_p1	   = 6UL * (GB / MB);
	} else {
		cap->remaining_p1 = 0;
		cap->max_p1	   = 0;
	}
	return DEVICE_GOOD;
}

int itdtimage_get_cartridge_health(void *device, struct tc_cartridge_health *cart_health)
{
	cart_health->mounts		   = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->written_ds	   = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->write_temps	  = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->write_perms	  = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_ds		  = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_temps	   = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_perms	   = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->write_perms_prev = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_perms_prev  = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->written_mbytes   = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_mbytes	  = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->passes_begin	 = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->passes_middle	= UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->tape_efficiency  = UNSUPPORTED_CARTRIDGE_HEALTH;
	return DEVICE_GOOD;
}

int itdtimage_get_tape_alert(void *device, uint64_t *tape_alert)
{
	*tape_alert = 0;
	return DEVICE_GOOD;
}

int itdtimage_clear_tape_alert(void *device, uint64_t tape_alert)
{
	return DEVICE_GOOD;
}

int itdtimage_get_xattr(void *device, const char *name, char **buf)
{
	return -LTFS_NO_XATTR;
}

int itdtimage_set_xattr(void *device, const char *name, const char *buf, size_t size)
{
	return -LTFS_NO_XATTR;
}

int itdtimage_logsense(void *device, const uint8_t page, const uint8_t subpage, unsigned char *buf, const size_t size)
{
	ltfsmsg(LTFS_ERR, 10007E, __FUNCTION__);
	return -EDEV_UNSUPPORTED_FUNCTION;
}

int itdtimage_modesense(void *device, const uint8_t page, const TC_MP_PC_TYPE pc, const uint8_t subpage, unsigned char *buf, const size_t size)
{
	/* Only clear buffer */
	memset(buf, 0, size);
	return DEVICE_GOOD;
}

int itdtimage_modeselect(void *device, unsigned char *buf, const size_t size)
{
	/* Do nothing */
	return DEVICE_GOOD;
}

int itdtimage_reserve_unit(void *vstate)
{
	struct itdtimage_data *state = (struct itdtimage_data *)vstate;
	if (state->device_reserved) {
		ltfsmsg(LTFS_ERR, 31017E);
		return -EDEV_ILLEGAL_REQUEST;
	}
	state->device_reserved = true;
	return DEVICE_GOOD;
}

int itdtimage_release_unit(void *vstate)
{
	struct itdtimage_data *state = (struct itdtimage_data *)vstate;
	state->device_reserved = false;
	return DEVICE_GOOD;
}

int itdtimage_prevent_medium_removal(void *vstate)
{
	struct itdtimage_data *state = (struct itdtimage_data *)vstate;
	if (!state->ready) {
		ltfsmsg(LTFS_ERR, 31018E);
		return -EDEV_NOT_READY;
	}
	state->medium_locked = true; /* TODO: fail if medium is already locked? */
	return DEVICE_GOOD;
}

int itdtimage_allow_medium_removal(void *vstate)
{
	struct itdtimage_data *state = (struct itdtimage_data *)vstate;
	ltfsmsg(LTFS_DEBUG, 31011D,
			(unsigned long long)state->current_position.partition,
			(unsigned long long)state->current_position.block,
			(unsigned long long)state->current_position.filemarks,
			(int)state->device_reserved,
			(int)state->medium_locked,
			(int)state->ready);
	if (!state->ready) {
		ltfsmsg(LTFS_ERR, 31019E);
		return -EDEV_NOT_READY;
	}
	state->medium_locked = false;
	return DEVICE_GOOD;
}

int itdtimage_read_attribute(void *vstate, const tape_partition_t part, const uint16_t id
							 , unsigned char *buf, const size_t size)
{
	struct itdtimage_data *state = (struct itdtimage_data *)vstate;
	long long offset=_itdtimage_getattr_offest(state,part,id);
	size_t data2ReadFromFile = size;
	size_t attrLength=_itdtimage_getattr_len(state,part,id);

	ltfsmsg(LTFS_DEBUG, 31020D , part , id );

	/* Open attribute record */
	if (offset == -1){
		return -EDEV_CM_PERM;
	}

	if (attrLength < size){
		data2ReadFromFile = attrLength;
	}

	if (_seek_file(state->img_file, offset)!=0){
		ltfsmsg(LTFS_ERR, 31002E, (long long)attrLength, state->filename, offset);
		return -EDEV_HARDWARE_ERROR;
	}

	fread(buf, 1, data2ReadFromFile, state->img_file);
	return DEVICE_GOOD;
}

int itdtimage_write_attribute(void *vstate, const tape_partition_t part
							  , const unsigned char *buf, const size_t size)
{
	return -EDEV_CM_PERM;
}

int itdtimage_allow_overwrite(void *device, const struct tc_position pos)
{
	return DEVICE_GOOD;
}

/**
 * GRAO command is currently unsupported on this device
 */
int itdtimage_grao(void *device, unsigned char *buf, const uint32_t len)
{
	int ret = -EDEV_UNSUPPORETD_COMMAND;
	return ret;
}

/**
 * RRAO command is currently unsupported on this device
 */
int itdtimage_rrao(void *device, unsigned char *buf, const uint32_t len, size_t *out_size)
{
	int ret = -EDEV_UNSUPPORETD_COMMAND;
	return ret;
}

int itdtimage_get_eod_status(void *vstate, int partition)
{
	struct itdtimage_data *state = (struct itdtimage_data *)vstate;
	if(state->eod[partition] == MISSING_EOD)
		return EOD_MISSING;
	else
		return EOD_GOOD;
}

int itdtimage_set_compression(void *vstate, bool enable_compression, struct tc_position *pos)
{
	struct itdtimage_data *state = (struct itdtimage_data *)vstate;
	if (!state->ready) {
		ltfsmsg(LTFS_ERR, 31024E);
		return -EDEV_NOT_READY;
	}
	pos->block = state->current_position.block;
	pos->filemarks = state->current_position.filemarks;
	return DEVICE_GOOD;
}

int itdtimage_set_default(void *device)
{
	return DEVICE_GOOD;
}

int itdtimage_get_parameters(void *vstate, struct tc_drive_param *params)
{
	params->max_blksize = FILE_DEBUG_MAX_BLOCK_SIZE;
	params->write_protect = VOL_PHYSICAL_WP;
	return DEVICE_GOOD;
}
const char *itdtimage_default_device_name(void)
{
	return itdtimage_default_device;
}

/**
 * Write an EOD mark at the current tape position, remove extra records, and
 * update the EOD in the state variable.
 * Returns 0 on success, negative value on failure.
 */
int _itdtimage_write_eod(struct itdtimage_data *state)
{
	return -EDEV_WRITE_PROTECTED;
}

/**
 * Delete the file associated with the current tape position.
 */
int _itdtimage_remove_current_record(const struct itdtimage_data *state)
{
	return _itdtimage_remove_record(state
									, state->current_position.partition
									, state->current_position.block);
}

/**
 * Delete the file associated with a given tape position.
 * @return 1 on successful delete, 0 if no file found, negative on error.
 */
int _itdtimage_remove_record(const struct itdtimage_data *state,
										int partition, uint64_t blknum)
{
	return -EDEV_WRITE_PROTECTED;;
}
/**
 * Make filename for a record.
 * Returns a string on success or NULL on failure. The caller is responsible for freeing
 * the returned memory. Failure probably means asprintf couldn't allocate memory.
 */

long long _itdtimage_getRllIndex4PartitionAndPos(const struct itdtimage_data *state,
												int part, uint64_t pos)
{
	long start = 0;
	long end = state->rll_count-1;
	long middle;

	if(part == 1)
		start =state->part1_img_offset;
	else
		end = state->part1_img_offset-1;
	while (start <= end){
		middle = start + ((end - start) / 2);
		if( ((long long)pos >= state->runlist[middle].pos_tape)
			&& ((long long)pos < state->runlist[middle].pos_tape+state->runlist[middle].count_rec)){
			return middle;
		}
		else {
			if ( (state->runlist[middle].pos_tape+state->runlist[middle].count_rec) > (long long)pos )
				end = middle - 1;
			else
				start = middle + 1;
		}
	}

	return -1;
}

long long _itdtimage_getrec_offset(const struct itdtimage_data *state,
												int part, uint64_t pos)
{
	long long temp, ret;
	long long cur = _itdtimage_getRllIndex4PartitionAndPos(state, part, pos);

	if (cur != -1){
		temp = pos - state->runlist[cur].pos_tape;
		ret = state->runlist[cur].offset_img + state->runlist[cur].length_rec * temp;
		return ret;
	}

	return -1;
}

long long _itdtimage_getrec_len(const struct itdtimage_data *state,
												int part, uint64_t pos)
{
	long long cur = _itdtimage_getRllIndex4PartitionAndPos(state,part,pos);
	if (cur != -1){
		return state->runlist[cur].length_rec;
	}
	return -1;
}

/**
 * Make filename for a Attribute.
 * Returns a string on success or NULL on failure. The caller is responsible for freeing
 * the returned memory. Failure probably means asprintf couldn't allocate memory.
 */
long long _itdtimage_getattr_offest(const struct itdtimage_data *state, int part, int id)
{
	int i;

	for(i = 0; i < state->attr_count; i++) {
		if( (state->attr_info[i].attr_id == id) && (state->attr_info[i].partition == part) )
			return state->attr_info[i].offset_img;
	}

	return -1;
}

long long _itdtimage_getattr_len(const struct itdtimage_data *state, int part, int id)
{
	int i;

	for( i = 0; i < state->attr_count; i++) {
		if( (state->attr_info[i].attr_id==id) && (state->attr_info[i].partition==part) )
			return state->attr_info[i].length;
	}

	return -1;
}


/**
 * Space over filemarks. Position immediately after the FM if spacing forwards, or
 * immediately before it if spacing backwards.
 * @param state the tape state
 * @param count number of filemarks to skip
 * @param back true to skip backwards, false to skip forwards
 * @return 0 on success or a negative value on error
 */
int _itdtimage_space_fm(struct itdtimage_data *state, uint64_t count, bool back)
{
	long start = 0;
	long end = state->rll_count;
	long cur = -1;
	uint64_t filemarkCount = 0;

	ltfsmsg(LTFS_DEBUG, 31004D, (unsigned long long)count, state->current_position.partition,
			(unsigned long long)state->current_position.block,
			(unsigned long long)state->current_position.filemarks);

	if (count == 0)
		return DEVICE_GOOD;

	if( state->current_position.partition == 1 )
		start = state->part1_img_offset;
	else
		end = state->part1_img_offset-1;

	if ( back && (state->current_position.block > 0) )
		--state->current_position.block;

	cur = _itdtimage_getRllIndex4PartitionAndPos(state, state->current_position.partition, state->current_position.block);
	if (cur == -1){
		return -EDEV_RW_PERM;
	}

	if ( back ) {
		if (state->current_position.block == 0)
			return -EDEV_BOP_DETECTED;
		if (state->runlist[cur].length_rec == 0 &&
				state->runlist[cur].count_rec > 1){
			/* check for filemark entry */
			filemarkCount = (state->runlist[cur].count_rec-1) + state->current_position.block - state->runlist[cur].pos_tape;
			if (filemarkCount >= count) {
				state->current_position.block-=count;
				return DEVICE_GOOD;
			}
		}
		/* not in current rll entry check previous onces */
		cur--;
		while ( cur >= start ){
			if (state->runlist[cur].length_rec==0){
				if (filemarkCount+state->runlist[cur].count_rec >= count){
					state->current_position.block = state->runlist[cur].pos_tape + (state->runlist[cur].count_rec - filemarkCount) + 1;
					return DEVICE_GOOD;
				}
				else
					filemarkCount+=state->runlist[cur].count_rec;
			}
			cur--;
		}
		return -EDEV_BOP_DETECTED;
	} else {
		if (state->runlist[cur].length_rec == 0){
			/* check for filemark entry */
			filemarkCount = state->runlist[cur].pos_tape + state->runlist[cur].count_rec-state->current_position.block;
			if (filemarkCount >= count) {
				state->current_position.block += count;
				return DEVICE_GOOD;
			}
		}
		/* not in current rll entry check next onces */
		cur++;
		while ( cur <= end ){
			if (state->runlist[cur].length_rec == 0){
				if (filemarkCount+state->runlist[cur].count_rec >= count){
					state->current_position.block = state->runlist[cur].pos_tape + (state->runlist[cur].count_rec - filemarkCount);
					return DEVICE_GOOD;
				}
				else
					filemarkCount += state->runlist[cur].count_rec;

			}
			cur++;
		}
		ltfsmsg(LTFS_ERR, 31025E, "fimemarks");
		return -EDEV_EOD_DETECTED;
	}
}

/**
 * Space over records. If FM is encountered, position immediately after it if spacing forwards
 * or immediately before it if spacing backwards.
 * TODO returns -EIO if it encounters a FM or BOT/EOD. fix for correct behavior if needed.
 * TODO: add proper error reporting if this function is ever needed
 * NOTE: this function is not used for anything. It may or may not behave as advertised.
 * @param state the tape state
 * @param count number of records to skip
 * @param back true to skip backwards, false to skip forwards
 * @return 0 on success or a negative value on error
 */
int _itdtimage_space_rec(struct itdtimage_data *state, uint64_t count, bool back)
{
	long start = 0;
	long end = state->rll_count;
	long cur = -1;
	uint64_t count_rec = 0;

	if (count == 0)
		return DEVICE_GOOD;

	if(state->current_position.partition==1)
		start = state->part1_img_offset;
	else
		end = state->part1_img_offset - 1;

	if ( back && state->current_position.block > 0 )
		--state->current_position.block;
	cur = _itdtimage_getRllIndex4PartitionAndPos(state,state->current_position.partition, state->current_position.block);
	if (cur == -1){
		return -EDEV_RW_PERM;
	}

	if ( back ){
		if (state->current_position.block == 0)
			return -EDEV_BOP_DETECTED;

		/* 0 = filemark, -1 = end of partition */
		if ( state->runlist[cur].length_rec > 0 &&
			 state->runlist[cur].count_rec > 1) {
			/* check for filemark entry */
			count_rec = state->runlist[cur].pos_tape + state->runlist[cur].count_rec-state->current_position.block;
			if (count_rec >= count) {
				state->current_position.block -= count;
				return DEVICE_GOOD;
			}
		}

		/* not in current rll entry check previous onces */
		cur--;
		while ( cur >= start ){
			if ( state->runlist[cur].length_rec > 0 ) {
				if ( count_rec+state->runlist[cur].count_rec >= count ){
					state->current_position.block = state->runlist[cur].pos_tape + (state->runlist[cur].count_rec-count_rec) + 1;
					return DEVICE_GOOD;
				}
				else
					count_rec += state->runlist[cur].count_rec;
			}
			else if (state->runlist[cur].length_rec ==0 ){
				/* filemark */
				state->current_position.block=state->runlist[cur].pos_tape+state->runlist[cur].count_rec;
				return DEVICE_GOOD;
			}
			cur--;
		}
		return -EDEV_BOP_DETECTED;
	}
	else {
		if ( state->runlist[cur].length_rec > 0 &&
			 state->runlist[cur].count_rec > 1) {
			/* check for filemark entry */
			count_rec=state->runlist[cur].pos_tape+state->runlist[cur].count_rec-state->current_position.block;
			if (count_rec >= count) {
				state->current_position.block+=count;
				return DEVICE_GOOD;
			}
		}
		/* not in current rll entry check next onces */
		cur++;
		while (cur <= end){
			if (state->runlist[cur].length_rec == 0) {
				if (count_rec+state->runlist[cur].count_rec>=count) {
					state->current_position.block = state->runlist[cur].pos_tape + (state->runlist[cur].count_rec-count_rec);
					return DEVICE_GOOD;
				}
				else
					count_rec += state->runlist[cur].count_rec;
			}
			else if (state->runlist[cur].length_rec == 0){
				state->current_position.block = state->runlist[cur].pos_tape;
				return DEVICE_GOOD;
			}
			cur++;
		}
		ltfsmsg(LTFS_ERR, 31025E, "records");
		return -EDEV_EOD_DETECTED;
	}
}

/**
 * Get valid device list. Returns an empty list because there's no way to enumerate
 * all the possible valid devices for this backend.
 */
#define DRIVE_FILE_PREFIX "Drive-"

int itdtimage_get_device_list(struct tc_drive_info *buf, int count)
{
	char *filename, *devdir, line[1024];
	FILE *infile;
	DIR *dp;
	struct dirent *entry;
	int deventries = 0;

	/* Create a file to indicate current directory of drive link (for tape file backend) */
	asprintf(&filename, "%s/ltfs%ld", DRIVE_LIST_DIR, (long)getpid());
	if (!filename) {
		ltfsmsg(LTFS_ERR, 10001E, "filechanger_data drive file name");
		return -LTFS_NO_MEMORY;
	}
	ltfsmsg(LTFS_INFO, 31026I, filename);
	infile = fopen(filename, "r");
	if (!infile) {
		ltfsmsg(LTFS_INFO, 31027I, filename);
		return 0;
	} else {
		devdir = fgets(line, sizeof(line), infile);
		if(devdir[strlen(devdir) - 1] == '\n')
			devdir[strlen(devdir) - 1] = '\0';
		fclose(infile);
		free(filename);
	}

	ltfsmsg(LTFS_INFO, 31028I, devdir);
	dp = opendir(devdir);
	if (! dp) {
		ltfsmsg(LTFS_ERR, 31029E, devdir);
		return 0;
	}
	while ((entry = readdir(dp))) {
		if (strncmp(entry->d_name, DRIVE_FILE_PREFIX, strlen(DRIVE_FILE_PREFIX)))
			continue;

		if (buf && deventries < count) {
			snprintf(buf[deventries].name, TAPE_DEVNAME_LEN_MAX, "%s/%s", devdir, entry->d_name);
			strncpy(buf[deventries].vendor, "DUMMY", TAPE_VENDOR_NAME_LEN_MAX);
			strncpy(buf[deventries].model, "DUMMYDEV", TAPE_MODEL_NAME_LEN_MAX);
			strncpy(buf[deventries].serial_number, &(entry->d_name[strlen(DRIVE_FILE_PREFIX)]), TAPE_SERIAL_LEN_MAX);
			ltfsmsg(LTFS_DEBUG, 31030D, buf[deventries].name, buf[deventries].vendor,
					buf[deventries].model, buf[deventries].serial_number);
		}

		deventries++;
	}

	closedir(dp);

	return deventries;
}

int itdtimage_set_key(void *device, const unsigned char *keyalias, const unsigned char *key)
{
	return -EDEV_UNSUPPORTED_FUNCTION;
}

int itdtimage_get_keyalias(void *device, unsigned char **keyalias)
{
	return -EDEV_UNSUPPORTED_FUNCTION;
}

int itdtimage_takedump_drive(void *device, bool capture_unforced)
{
	/* Do nothing */
	return DEVICE_GOOD;
}

int itdtimage_is_mountable(void *device, const char *barcode, const unsigned char cart_type,
							const unsigned char density)
{
	/* Do nothing */
	return MEDIUM_PERFECT_MATCH;
}

bool itdtimage_is_readonly(void *device)
{
	/* Do nothing */
	return false;
}

int itdtimage_get_worm_status(void *device, bool *is_worm)
{
	*is_worm = false;
	return DEVICE_GOOD;
}

int itdtimage_get_serialnumber(void *vstate, char **result)
{
	struct itdtimage_data *state = (struct itdtimage_data *)vstate;

	CHECK_ARG_NULL(vstate, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(result, -LTFS_NULL_ARG);

	if (state->serial_number)
		*result = SAFE_STRDUP((const char *) state->serial_number);
	else
		*result = SAFE_STRDUP("DUMMY");

	if (! *result)
		return -EDEV_NO_MEMORY;

	return DEVICE_GOOD;
}

int itdtimage_get_info(void *device, struct tc_drive_info *info)
{
	/*
	 * Return dummy data.
	 * This logic is enough only for single drive supported code.
	 */
	info->host    = 0;
	info->channel = 0;
	info->target  = 0;
	info->lun     = -1;

	return 0;
}

int itdtimage_set_profiler(void *device, char *work_dir, bool enable)
{
	/* Do nohting: file backend does not support profiler */
	return 0;
}

int itdtimage_get_next_block_to_xfer(void *device, struct tc_position *pos)
{
	/* This backend never accept write command */
	return -EDEV_WRITE_PROTECTED;;
}

/* Local functions */
char *memstr(const char *s, const char *find, size_t slen)
{
	char *p;
	size_t len;

	if ( !s || slen == 0 ) /* nothing todo */
		return NULL;

	len = strlen(find);
	if (*find == '\0')
		return (char*) s;

	for (p = (char*) s; p < (s + slen-len); p++) {
		if (memcmp(p, find, len) == 0){
			return p;
		}
	}

	return NULL;
}

char *_read_XML_tag(char *buf, int buf_len, char *needle)
{
	char *sptr;  /* start of substring */
	char *eptr;  /* end of substring   */
	char *ret_str;
	char tag_end[100];
	char tag_start[100];
	int length;

	if(buf==NULL ) {
		return NULL;
	}

	sprintf(tag_end,"</%s>",needle);
	sprintf(tag_start,"<%s>",needle);
	sptr = NULL;
	sptr = memstr(buf, tag_start, buf_len);
	if(sptr==NULL ) {
		return NULL;
	}

	sptr = sptr + strlen(tag_start);
	eptr = memstr(sptr, tag_end, buf_len - (sptr-buf));
	if(eptr==NULL ) {
		return NULL;
	}

	length = eptr - sptr;
	ret_str = (char*)calloc(1, length + 1);
	if (ret_str){
		memcpy(ret_str, sptr, length);
		ret_str[length]=0;
	}

	return ret_str;
}

unsigned long long _read_XML_tag_value(char *buf, int buf_len, char *needle)
{
	char *val;
	unsigned long long ret = (unsigned long long)-1;

	val =_read_XML_tag(buf, buf_len, needle);
	if ( !val )
		return 0;

	ret = strtoull(val, NULL, 10);
	free(val);

	return ret;
}

unsigned long long _get_file_size(FILE *fStream)
{
	unsigned long long ret=0;
#ifdef mingw_PLATFORM
	_LARGE_INTEGER p;
	osHANDLE handle;
	int fd = fileno(fStream);
	handle = (void*) _get_osfhandle(fd);

	if(!GetFileSizeEx(handle, &p)){
		ret=0;
	} else {
		ret=p.HighPart;
		ret=ret << 32;
		ret|=p.LowPart;
	}
#else
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__)
	long long tmp = fseeko(fStream, 0LL, SEEK_END);
	if(tmp != -1){
		ret = ftello(fStream);
	}

#else
	long long tmp = fseeko64(fStream, 0LL, SEEK_END);
	if(tmp != -1){
		ret = ftello64(fStream);
	}
#endif
#endif
	return ret;
}

unsigned long long _seek_file(FILE *file, unsigned long long position)
{
	unsigned long long ret=0;
#ifdef mingw_PLATFORM
	if(position > 0){
		_LARGE_INTEGER p;
		p.HighPart=position>>32;
		p.LowPart=position & 0xFFFFFFFFL;
		if(position==0){
			p.HighPart=0;
			p.LowPart=0;
		}
		int fd = fileno(fStream);
		osHANDLE handle = (void*) _get_osfhandle(fd);
		if(!SetFilePointerEx(handle, p, NULL, FILE_BEGIN)){
			//printf("Error in _seek_file()\n");
			ret=0;
		}else
			ret=position;

		if(position==0){
			_LARGE_INTEGER curr;
			SetFilePointerEx(handle, p, &curr, FILE_BEGIN);
			cout << "***Current Pos is " << curr.QuadPart << endl;
		}
	}else
		file->Seek(position, wxFromStart);
#else
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__)
	long long tmp = fseeko(file, position, SEEK_SET);
#else
	long long tmp = fseeko64(file, position, SEEK_SET);
#endif
	if(tmp != -1)
		ret = tmp;
#endif
	return ret;
}

struct tape_ops itdtimage_handler = {
	.open					= itdtimage_open,
	.reopen					= itdtimage_reopen,
	.close					= itdtimage_close,
	.close_raw				= itdtimage_close_raw,
	.is_connected			= itdtimage_is_connected,
	.inquiry				= itdtimage_inquiry,
	.inquiry_page			= itdtimage_inquiry_page,
	.test_unit_ready		= itdtimage_test_unit_ready,
	.read					= itdtimage_read,
	.write					= itdtimage_write,
	.writefm				= itdtimage_writefm,
	.rewind					= itdtimage_rewind,
	.locate					= itdtimage_locate,
	.space					= itdtimage_space,
	.erase					= itdtimage_erase,
	.load					= itdtimage_load,
	.unload					= itdtimage_unload,
	.readpos				= itdtimage_readpos,
	.setcap					= itdtimage_setcap,
	.format					= itdtimage_format,
	.remaining_capacity		= itdtimage_remaining_capacity,
	.logsense				= itdtimage_logsense,
	.modesense				= itdtimage_modesense,
	.modeselect				= itdtimage_modeselect,
	.reserve_unit			= itdtimage_reserve_unit,
	.release_unit			= itdtimage_release_unit,
	.prevent_medium_removal = itdtimage_prevent_medium_removal,
	.allow_medium_removal   = itdtimage_allow_medium_removal,
	.write_attribute		= itdtimage_write_attribute,
	.read_attribute			= itdtimage_read_attribute,
	.allow_overwrite		= itdtimage_allow_overwrite,
	.grao 					= itdtimage_grao,
	.rrao					= itdtimage_rrao,
	.set_compression		= itdtimage_set_compression,
	.set_default			= itdtimage_set_default,
	.get_cartridge_health   = itdtimage_get_cartridge_health,
	.get_tape_alert			= itdtimage_get_tape_alert,
	.clear_tape_alert		= itdtimage_clear_tape_alert,
	.get_xattr				= itdtimage_get_xattr,
	.set_xattr				= itdtimage_set_xattr,
	.get_parameters			= itdtimage_get_parameters,
	.get_eod_status			= itdtimage_get_eod_status,
	.get_device_list		= itdtimage_get_device_list,
	.help_message			= itdtimage_help_message,
	.parse_opts				= itdtimage_parse_opts,
	.default_device_name	= itdtimage_default_device_name,
	.set_key				= itdtimage_set_key,
	.get_keyalias			= itdtimage_get_keyalias,
	.takedump_drive			= itdtimage_takedump_drive,
	.is_mountable			= itdtimage_is_mountable,
	.get_worm_status		= itdtimage_get_worm_status,
	.get_serialnumber       = itdtimage_get_serialnumber,
	.get_info               = itdtimage_get_info,
	.set_profiler           = itdtimage_set_profiler,
	.get_next_block_to_xfer = itdtimage_get_next_block_to_xfer,
	.is_readonly 			= itdtimage_is_readonly,
};

struct tape_ops *tape_dev_get_ops(void)
{
	return &itdtimage_handler;
}

#ifndef mingw_PLATFORM
extern char tape_generic_itdtimg_dat[];
#endif

const char *tape_dev_get_message_bundle_name(void **message_data)
{
#ifndef mingw_PLATFORM
	*message_data = tape_generic_itdtimg_dat;

#else
	*message_data = NULL;
#endif
	return "tape_generic_itdtimg";
}
