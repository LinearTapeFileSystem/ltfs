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
** FILE NAME:       tape_drivers/generic/file/filedebug_tc.c
**
** DESCRIPTION:     Implements a file-based tape simulator.
**
** AUTHORS:         Atsushi Abe
**                  IBM Yamato, Japan
**                  PISTE@jp.ibm.com
**
**                  Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
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
#include <limits.h>

#include "libltfs/ltfs_fuse_version.h"
#include <fuse.h>

#include "ltfs_copyright.h"
#include "libltfs/ltfslogging.h"
#include "libltfs/ltfs_endian.h"
#include "libltfs/tape_ops.h"
#include "libltfs/ltfs_error.h"

#include "tape_drivers/tape_drivers.h"
#include "tape_drivers/ibm_tape.h"

#include "filedebug_conf_tc.h"

volatile char *copyright = LTFS_COPYRIGHT_0"\n"LTFS_COPYRIGHT_1"\n"LTFS_COPYRIGHT_2"\n" \
	LTFS_COPYRIGHT_3"\n"LTFS_COPYRIGHT_4"\n"LTFS_COPYRIGHT_5"\n";

/* Default directory where the emulated tape contents go to */
#ifdef mingw_PLATFORM
const char *filedebug_default_device = "c:\\tmp\\ltfs\\tape";
#else
const char *filedebug_default_device = "/tmp/ltfs/tape";
#endif

#define MAX_PARTITIONS 2
#define KB   (1024)
#define MB   (KB * 1024)
#define GB   (MB * 1024)
#define FILE_DEBUG_MAX_BLOCK_SIZE (4 * MB)

/* O_BINARY is defined only in MinGW */
#ifndef O_BINARY
#define O_BINARY 0
#endif

#define MISSING_EOD      (0xFFFFFFFFFFFFFFFFLL)
#define CARTRIDGE_CONFIG  "filedebug_tc_conf.xml"

/* For drive link feature */
#ifdef mingw_PLATFORM
#define DRIVE_LIST_DIR    "ltfs"
#else
#define DRIVE_LIST_DIR    "/tmp"
#endif

#define NANOSECONDS(x)  ((x) * 1000000000)
#define MICROSECONDS(x)    ((x) * 1000000)

/**
 * Emulator-specific data structures, used in lieu of a file descriptor
 */
struct filedebug_data {
	int      fd;                           /**< File descriptor to contain the pointer to directory where blocks reside */
	char     *dirbase;                     /**< Base directory for searcing directoty from the pointer */
	char     *dirname;                     /**< Directory where blocks reside */
	bool     device_reserved;              /**< True when the device has been successfully reserved */
	bool     medium_locked;                /**< True when preventing medium removal by the user */
	bool     null_backend;                 /**< True when runiing on null backend_mode */
	struct   tc_position current_position; /**< Current tape position (partition, block) */
	uint32_t max_block_size;               /**< Maximum block size, in bytes */
	bool     ready;                        /**< Is the "tape" loaded? */
	bool     is_readonly;                  /**< Is read-only tape loaded? */
	bool     is_worm;                      /**< Is WORM tape loaded ? */
	bool     unsupported_tape;             /**< Is supported tape in this drive ? */
	bool     unsupported_format;           /**< Is supported format in this drive ? */
	uint64_t last_block[MAX_PARTITIONS];   /**< Last positions for all partitions */
	uint64_t eod[MAX_PARTITIONS];          /**< Append positions (1 + last block) for all partitions */
	int      partitions;                   /**< Number of available partitions */
	uint64_t write_pass_prev;              /**< Previous write Pass */
	uint64_t write_pass;                   /**< Current write Pass of LTO drive for consistency check*/
	struct   timespec accumulated_delay;   /**< How much time has been spent doing seeks */
	unsigned p0_warning;                   /**< Nonzero to provide early warning on partition 0 */
	unsigned p1_warning;                   /**< Nonzero to provide early warning on partition 1 */
	unsigned p0_p_warning;                 /**< Nonzero to provide programmable early warning on partition 0 */
	unsigned p1_p_warning;                 /**< Nonzero to provide programmable early warning on partition 1 */
	bool     clear_by_pc;                  /**< clear pseudo write perm by partition change */
	uint64_t force_writeperm;              /**< pseudo write perm threshold */
	uint64_t force_readperm;               /**< pseudo read perm threashold */
	uint64_t write_counter;                /**< write call counter for pseudo write perm */
	uint64_t read_counter;                 /**< read call counter for pseudo write perm */
	int      force_errortype;              /**< 0 is R/W Perm, otherwise no sense */
	int      drive_type;                   /**< drive type defined by ltfs */
	char     *serial_number;               /**< Serial number of this dummy tape device */
	struct tc_drive_info info;             /**< Device informaton (DUMMY) */
	char     *product_id;                  /**< Product ID of this dummy tape device */
	struct   filedebug_conf_tc conf;       /**< Bahavior option for this instance */
};

struct filedebug_global_data {
	unsigned          strict_drive;      /**< Is bar code length checked strictly? */
};

struct filedebug_global_data global_data;

/* record suffixes for data block, filemark, EOD indicator */
long original_pid = 0;
static const char *rec_suffixes = "RFE";
#define SUFFIX_RECORD   (0)
#define SUFFIX_FILEMARK (1)
#define SUFFIX_EOD      (2)

/* Forward reference */
int filedebug_get_device_list(struct tc_drive_info *buf, int count);

/* local prototypes */
int filedebug_search_eod(struct filedebug_data *state, int partition);
int _filedebug_write_eod(struct filedebug_data *state);
int _filedebug_check_file(const char *fname);
char *_filedebug_make_current_filename(const struct filedebug_data *state, char type);
char *_filedebug_make_filename(const struct filedebug_data *state,
	int part, uint64_t pos, char type);
char *_filedebug_make_attrname(const struct filedebug_data *state, int part, int id);
int _filedebug_remove_current_record(const struct filedebug_data *state);
int _filedebug_remove_record(const struct filedebug_data *state,
	int partition, uint64_t blknum);
int _filedebug_space_fm(struct filedebug_data *state, uint64_t count, bool back);
int _filedebug_space_rec(struct filedebug_data *state, uint64_t count, bool back);
int _get_wp(struct filedebug_data *state, uint64_t *wp);
int _set_wp(struct filedebug_data *state, uint64_t wp);

/* Command-line options recognized by this module */
#define FILEDEBUG_OPT(templ,offset,value) { templ, offsetof(struct filedebug_global_data, offset), value }

static inline uint64_t calc_p0_cap(struct filedebug_data *state)
{
	/* 5% of total capacity */
	return (state->conf.capacity_mb * 5 /100);
}

static inline uint64_t calc_p0_remaining(struct filedebug_data *state)
{
	/* Assume 512KB per 1 record */
	return (calc_p0_cap(state) - state->eod[0] / 2);
}

static inline uint64_t calc_p1_cap(struct filedebug_data *state)
{
	return (state->conf.capacity_mb - calc_p0_cap(state));
}

static inline uint64_t calc_p1_remaining(struct filedebug_data *state)
{
	/* Assume 512KB per 1 record */
	return (calc_p1_cap(state) - state->eod[1] / 2);
}

static struct fuse_opt filedebug_opts[] = {
	FILEDEBUG_OPT("strict_drive",   strict_drive, 1),
	FILEDEBUG_OPT("nostrict_drive", strict_drive, 0),
	FUSE_OPT_END
};

int null_parser(void *state, const char *arg, int key, struct fuse_args *outargs)
{
	return 1;
}

int filedebug_parse_opts(void *device, void *opt_args)
{
	struct filedebug_data *state = (struct filedebug_data *) device;
	struct fuse_args *args = (struct fuse_args *) opt_args;
	int ret;

	/* fuse_opt_parse can handle a NULL device parameter just fine */
	ret = fuse_opt_parse(args, state, filedebug_opts, null_parser);
	if (ret < 0)
		return ret;

	return 0;
}

static void emulate_threading_wait(struct filedebug_data *state)
{
	if (!state->conf.delay_mode)
		return;

	struct timespec t;
	t.tv_sec  = state->conf.threading_sec;
	t.tv_nsec = 0;

	/* TODO: Need to handle interrupted sleep */
	if (state->conf.delay_mode == DELAY_EMULATE)
		nanosleep(&t, NULL);

	state->accumulated_delay.tv_sec += t.tv_sec;
	state->accumulated_delay.tv_nsec += t.tv_nsec;
	if (state->accumulated_delay.tv_nsec > NANOSECONDS(1)) {
		state->accumulated_delay.tv_sec++;
		state->accumulated_delay.tv_nsec -= NANOSECONDS(1);
	}
}

static inline uint64_t calc_wrap(struct filedebug_data *state, struct tc_position *pos)
{
	/* Assume 512KB per 1 record */
	uint64_t blocks_per_wrap = (state->conf.capacity_mb / state->conf.wraps) * 2;
	uint64_t wrap = pos->block / blocks_per_wrap;

	/* 2 wraps for partition 0, 2 guard wraps, other wraps are for partition 1 */
	if (pos->partition)
		wrap += 4;

	return wrap;
}

static void emulate_seek_wait(struct filedebug_data *state, struct tc_position *dest)
{
	if (!state->conf.delay_mode)
		return;

	uint64_t blocks_per_wrap = (state->conf.capacity_mb / state->conf.wraps) * 2;

	uint64_t current_wrap = calc_wrap(state, &state->current_position);
	uint64_t current_dist_from_bot = current_wrap % 2 == 0 ?
		state->current_position.block % blocks_per_wrap :
		blocks_per_wrap - (state->current_position.block % blocks_per_wrap);

	uint64_t target_wrap = calc_wrap(state, dest);
	uint64_t target_dist_from_bot = target_wrap % 2 == 0 ?
		dest->block % blocks_per_wrap :
		blocks_per_wrap - (dest->block % blocks_per_wrap);

	uint64_t distance = llabs(target_dist_from_bot - current_dist_from_bot);
	float cost = ((float) state->conf.eot_to_bot_sec / blocks_per_wrap) * (distance-1.0);
	time_t delay_us = 0;

	if (dest->partition != state->current_position.partition) {
		if (current_wrap == target_wrap) {
			/* Ensure that the cost of moving the head to locate the target wrap
			 * is taken into account. */
			current_wrap += 2;
		}
	}

	if (current_wrap == target_wrap && dest->block > state->current_position.block) {
		/* Same wrap, moving tape forward */
		delay_us = MICROSECONDS(cost);
	} else if (current_wrap == target_wrap && dest->block < state->current_position.block) {
		/* Same wrap, moving tape backward */
		delay_us = MICROSECONDS(cost) + state->conf.change_direction_us;
	} else if (current_wrap % 2 == target_wrap % 2 && dest->block > state->current_position.block) {
		/* Different wraps, same direction, can move tape forward */
		delay_us = MICROSECONDS(cost) + state->conf.change_track_us;
	} else if (current_wrap % 2 == target_wrap % 2 && dest->block < state->current_position.block) {
		/* Different wraps, same direction, must move tape backward */
		delay_us = MICROSECONDS(cost) + state->conf.change_track_us + state->conf.change_direction_us;
	} else if (current_wrap % 2 != target_wrap % 2) {
		/* Different wraps, different direction */
		delay_us = MICROSECONDS(cost) + state->conf.change_track_us + state->conf.change_direction_us;
	}

	if (delay_us) {
		struct timespec t;
		t.tv_sec  = (delay_us / MICROSECONDS(1));
		t.tv_nsec = (delay_us % MICROSECONDS(1)) * 1000;

		/* TODO: Need to handle interrupted sleep */
		if (state->conf.delay_mode == DELAY_EMULATE)
			nanosleep(&t, NULL);

		state->accumulated_delay.tv_sec += t.tv_sec;
		state->accumulated_delay.tv_nsec += t.tv_nsec;
		if (state->accumulated_delay.tv_nsec > NANOSECONDS(1)) {
			state->accumulated_delay.tv_sec++;
			state->accumulated_delay.tv_nsec -= NANOSECONDS(1);
		}
	}
}

static void emulate_load_wait(struct filedebug_data *state)
{
	struct tc_position dest;

	dest.block         = 0;
	dest.filemarks     = 0;
	dest.partition     = 0;
	dest.early_warning = false;
	dest.programmable_early_warning = false;

	emulate_seek_wait(state, &dest);
}

static void emulate_rewind_wait(struct filedebug_data *state)
{
	struct tc_position dest;

	dest.block         = 0;
	dest.filemarks     = 0;
	dest.partition     = state->current_position.partition;
	dest.early_warning = false;
	dest.programmable_early_warning = false;

	emulate_seek_wait(state, &dest);
}

void filedebug_help_message(const char *progname)
{
	ltfsresult(30199I, filedebug_default_device);
}

int filedebug_open(const char *name, void **handle)
{
	struct filedebug_data *state;
	struct stat d;
	char *tmp = NULL;
	char *cur, *p;
	char *pid = NULL, *ser = NULL;
	int ret;
	char *devname = NULL;

	int i, devs = 0, info_devs = 0;
	struct tc_drive_info *buf = NULL;

	ltfsmsg(LTFS_INFO, 30000I, name);

	CHECK_ARG_NULL(handle, -LTFS_NULL_ARG);
	*handle = NULL;

	state = (struct filedebug_data *)calloc(1,sizeof(struct filedebug_data));
	if (!state) {
		ltfsmsg(LTFS_ERR, 10001E, "filedebug_open: private data");
		return -EDEV_NO_MEMORY;
	}

	/* check name is file or dir */
	ret = stat(name, &d);
	if (ret == 0 && S_ISDIR(d.st_mode)) {
		ltfsmsg(LTFS_INFO, 30003I, name);
		state->dirname = strdup(name);
		if (!state->dirname) {
			ltfsmsg(LTFS_ERR, 10001E, "filedebug_open: dirname");
			free(state);
			return -EDEV_NO_MEMORY;
		}
		state->product_id = "ULTRIUM-TD5";
	} else {
		devs = filedebug_get_device_list(NULL, 0);
		if (devs) {
			buf = (struct tc_drive_info *)calloc(devs * 2, sizeof(struct tc_drive_info));
			if (! buf) {
				ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
				return -LTFS_NO_MEMORY;
			}
			info_devs = filedebug_get_device_list(buf, devs * 2);
		}

		for (i = 0; i < info_devs; i++) {
			if (! strncmp(buf[i].serial_number, name, TAPE_SERIAL_LEN_MAX) ) {
				devname = strdup(buf[i].name);
				if (!devname) {
					ltfsmsg(LTFS_ERR, 10001E, "sg_ibmtape_open: devname");
					if (buf) free(buf);
					free(state);
					return -EDEV_NO_MEMORY;
				}
				break;
			}
		}

		if (buf) {
			free(buf);
			buf = NULL;
		}

		/* Run on file mode */
		if (devname == NULL)
			devname = strdup(name);
		ltfsmsg(LTFS_INFO, 30001I, devname);
		state->fd = open(devname, O_RDWR | O_BINARY);
		if (state->fd < 0) {
			ltfsmsg(LTFS_ERR, 30002E, devname);
			return -EDEV_INTERNAL_ERROR;
		}

		/* Parse pid and serial from filename */
		cur = (char*)devname;
		cur += strlen(devname) - 1;
		for (i = 0; i < (int)strlen(devname); i++) {
			if (*cur == '.')
				pid = cur + 1;
			if (*cur == '_') {
				ser = cur + 1;
				break;
			}
			cur --;
		}

		if (pid && ser) {
			ret = asprintf(&state->serial_number, "%s", ser);
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, 10001E, "filedebug_open: serial & pid");
				free(state);
				return -EDEV_NO_MEMORY;
			}

			for (i = 0; i < (int)strlen(state->serial_number); i++) {
				if (state->serial_number[i] == '.') {
					state->serial_number[i] = '\0';
					state->product_id = &(state->serial_number[i+1]);
					break;
				}
			}
		}

		/* Store directory base */
		tmp = strdup(devname);
		if (!tmp) {
			ltfsmsg(LTFS_ERR, 10001E, "filedebug_open: dirbase tmp");
			free(state);
			return -EDEV_NO_MEMORY;
		}

		/* The dirname() function may return a pointer to static storage
		   that may then be overwritten by subsequent calls to dirname(). */
		p = dirname(tmp);
		state->dirbase = (char *) calloc(strlen(p) + 1, sizeof(char));
		if (!state->dirbase) {
			ltfsmsg(LTFS_ERR, 10001E, "filedebug_open: dirbase");
			free(state);
			free(tmp);
			return -EDEV_NO_MEMORY;
		}
		strcpy(state->dirbase, p);
		free(tmp);
		free(devname);
		devname= NULL;
	}

	state->ready          = false;
	state->max_block_size = 16 * MB;

	/* Set default option value */
	state->conf.dummy_io         = false;
	state->conf.emulate_readonly = false;
	state->conf.capacity_mb      = DEFAULT_CAPACITY_MB;
	state->conf.cart_type        = TC_MP_LTO5D_CART;
	state->conf.density_code     = 0x58;

	/* Initial setting of force perm */
	state->clear_by_pc     = false;
	state->force_writeperm = DEFAULT_WRITEPERM;
	state->force_readperm  = DEFAULT_READPERM;
	state->force_errortype = DEFAULT_ERRORTYPE;

	state->conf.delay_mode          = DELAY_NONE;
	state->conf.wraps               = DEFAULT_WRAPS;
	state->conf.eot_to_bot_sec      = DEFAULT_EOT_TO_BOT;
	state->conf.change_direction_us = DEFAULT_CHANGE_DIRECTION;
	state->conf.change_track_us     = DEFAULT_CHANGE_TRACK;

	/* Set drive type if it is provided */
	struct supported_device **d_cur = ibm_supported_drives;
	while(*d_cur) {
		if((! strncmp(IBM_VENDOR_ID, (*d_cur)->vendor_id, strlen((*d_cur)->vendor_id)) ) &&
		   (! strncmp(state->product_id, (*d_cur)->product_id, strlen((*d_cur)->product_id)) ) ) {
			state->drive_type = (*d_cur)->drive_type;
			break;
		}
		d_cur++;
	}

	snprintf(state->info.name, TAPE_DEVNAME_LEN_MAX + 1, "%s", name);
	snprintf(state->info.vendor, TAPE_VENDOR_NAME_LEN_MAX + 1, "%s", "DUMMY");
	snprintf(state->info.model, TAPE_MODEL_NAME_LEN_MAX + 1, "%s", state->product_id);
	snprintf(state->info.serial_number, TAPE_SERIAL_LEN_MAX + 1, "%s", state->serial_number);
	snprintf(state->info.product_rev, PRODUCT_REV_LENGTH + 1, "%s", "REVS");
	snprintf(state->info.product_name, PRODUCT_NAME_LENGTH + 1, "[%s]", state->product_id);

	state->info.host    = 0;
	state->info.channel = 0;
	state->info.target  = 0;
	state->info.lun     = -1;

	*handle = (void *) state;
	return 0;
}

int filedebug_reopen(const char *name, void *device)
{
	/* Do nothing */
	return 0;
}

int filedebug_close(void *device)
{
	struct filedebug_data *state = (struct filedebug_data *)device;

	/* Write EOD of DP here when dummy io mode is enebaled */
	if (state->conf.dummy_io) {
		state->current_position.partition = 1;
		state->current_position.block = state->eod[1];
		_filedebug_write_eod(state);
	}

	if (state) {
		if (state->fd > 0)
			close(state->fd);
		if (state->dirbase)
			free(state->dirbase);
		if (state->dirname)
			free(state->dirname);
		if (state->serial_number)
			free(state->serial_number);
		free(state);
	}

	return 0;
}

int filedebug_close_raw(void *device)
{
	return 0;
}

int filedebug_is_connected(const char *devname)
{
	return 0;
}

int filedebug_inquiry(void *device, struct tc_inq *inq)
{
	memset(inq, 0, sizeof(struct tc_inq));
	memcpy(inq->vid, "DUMMY   ", 8);
	memcpy(inq->pid, "DUMMYDEV        ", 16);
	memcpy(inq->revision, "0000", 4);
	/* Do not fill inq->vendor for vendor specific data */
	return DEVICE_GOOD;
}

int filedebug_inquiry_page(void *device, unsigned char page, struct tc_inq_page *inq)
{
	memset(inq, 0, sizeof(struct tc_inq_page));
	return DEVICE_GOOD;
}

int filedebug_test_unit_ready(void *device)
{
	struct filedebug_data *state = (struct filedebug_data *)device;
	if (!state->ready)
		return -EDEV_NEED_INITIALIZE;
	return DEVICE_GOOD;
}

int filedebug_read(void *device, char *buf, size_t count, struct tc_position *pos,
	const bool unusual_size)
{
	struct filedebug_data *state = (struct filedebug_data *)device;
	char *fname;
	size_t fname_len;
	int ret;
	ssize_t bytes_read;
	int fd;

	ltfsmsg(LTFS_DEBUG, 30005D, (unsigned int)count, state->current_position.partition,
		(unsigned long long)state->current_position.block,
		(unsigned long long)state->current_position.filemarks);

	if (!state->ready) {
		ltfsmsg(LTFS_ERR, 30006E);
		return -EDEV_NOT_READY;
	}

	/* Emulate unsupported cart/format */
	if (state->unsupported_tape || state->unsupported_format) {
		ret = -EDEV_MEDIUM_FORMAT_ERROR;
		return ret;
	}

	if (state->force_readperm) {
		state->read_counter++;
		if (state->read_counter > state->force_readperm) {
			ltfsmsg(LTFS_ERR, 30007E, "read");
			if (state->force_errortype)
				return -EDEV_READ_PERM;
			else
				return -EDEV_NO_SENSE;
		}
	}

	/* check for EOD (reading is an error) */
	if (state->eod[state->current_position.partition] == state->current_position.block) {
		return -EDEV_EOD_DETECTED;
	}

	if (state->conf.dummy_io &&
		state->current_position.partition &&
		state->current_position.block > 6) {

		/*
		 *  Dummy I/O mode
		 *  No actual data is written to partition1 (DP), hence we can simply
		 *  advance the current block address.
		 */

		++state->current_position.block;
		pos->block = state->current_position.block;
		return count;

	} else {
		fname = _filedebug_make_current_filename(state, rec_suffixes[SUFFIX_EOD]);
		if (!fname)
			return -EDEV_NO_MEMORY;
		fname_len = strlen(fname);

		ret = _filedebug_check_file(fname);
		if (ret < 0) {
			free(fname);
			return ret;
		}
		if (ret > 0) {
			ltfsmsg(LTFS_ERR, 30008E);
			free(fname);
			return -EDEV_EOD_NOT_FOUND;
		}

		/* check for filemark (reading returns 0 bytes and advances the position) */
		fname[fname_len - 1] = rec_suffixes[SUFFIX_FILEMARK];
		ret = _filedebug_check_file(fname);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 30009E, ret);
			free(fname);
			return ret;
		}
		if (ret > 0) {
			free(fname);
			++state->current_position.block;
			++state->current_position.filemarks;
			pos->block = state->current_position.block;
			pos->filemarks = state->current_position.filemarks;
			return 0;
		}

		/* check for record */
		fname[fname_len - 1] = rec_suffixes[SUFFIX_RECORD];
		ret = _filedebug_check_file(fname);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 30010E, ret);
			free(fname);
			return ret;
		}
		if (ret > 0) {
			fd = open(fname, O_RDONLY | O_BINARY);
			free(fname);
			if (fd < 0) {
				ltfsmsg(LTFS_ERR, 30011E, errno);
				return -EDEV_RW_PERM;
			}

			/* TODO: return -EDEV_INVALID_ARG if buffer is too small to hold complete record? */
			bytes_read = read(fd, buf, count);
			if (bytes_read < 0) {
				ltfsmsg(LTFS_ERR, 30012E, errno);
				close(fd);
				return -EDEV_RW_PERM;
			}

			ret = close(fd);
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, 30013E, errno);
				return -EDEV_RW_PERM;
			}

			++state->current_position.block;
			pos->block = state->current_position.block;

			ltfsmsg(LTFS_DEBUG, 30014D, bytes_read);
			return bytes_read;
		}

		/* couldn't find any records?! something is corrupted */
		ltfsmsg(LTFS_ERR, 30015E);
		free(fname);
	}

	return -EDEV_RW_PERM;
}

int filedebug_write(void *device, const char *buf, size_t count, struct tc_position *pos)
{
	int ret = -1;
	struct filedebug_data *state = (struct filedebug_data *)device;
	char *fname;
	int fd;
	ssize_t written;

	ltfsmsg(LTFS_DEBUG, 30016D, (unsigned int)count, state->current_position.partition,
		(unsigned long long)state->current_position.block,
		(unsigned long long)state->current_position.filemarks);

	if (!state->ready) {
		ltfsmsg(LTFS_ERR, 30017E);
		ret = -EDEV_NOT_READY;
		return ret;
	}

	/* Emulate WORM */
	if (state->is_worm && state->eod[pos->partition] != pos->block) {
		ret = -EDEV_WRITE_PROTECTED_WORM;
		return ret;
	}

	/* Emulate read-only tape and write protected tape */
	if (state->is_readonly) {
		if (state->conf.emulate_readonly)
			ret = -EDEV_WRITE_PROTECTED; /* Emulate 07/2700 */
		else
			ret = -EDEV_DATA_PROTECT;    /* Emulate 07/3005 */

		ltfsmsg(LTFS_INFO, 30085I, ret, state->serial_number);

		return ret;
	}

	/* Emulate unsupported cart/format */
	if (state->unsupported_tape || state->unsupported_format) {
		ret = -EDEV_MEDIUM_FORMAT_ERROR;
		return ret;
	}

	/* TODO: It is nicer if we have a append only mode support */

	if (! buf && count > 0) {
		ltfsmsg(LTFS_ERR, 30018E);
		ret = -EDEV_INVALID_ARG;
		return ret;
	} else if (count == 0) {
		ret = 0; /* nothing to do */
		return ret;
	}

	if ( state->force_writeperm ) {
		state->write_counter++;
		if ( state->write_counter > state->force_writeperm ) {
			ltfsmsg(LTFS_ERR, 30007E, "write");
			if (state->force_errortype)
				return -EDEV_NO_SENSE;
			else
				return -EDEV_WRITE_PERM;
		} else if ( state->write_counter > (state->force_writeperm - THRESHOLD_FORCE_WRITE_NO_WRITE) ) {
			ltfsmsg(LTFS_INFO, 30019I);
			pos->block++;
			return DEVICE_GOOD;
		}
	}

	if (count > (size_t)state->max_block_size) {
		ltfsmsg(LTFS_ERR, 30020E, (unsigned int)count, state->max_block_size);
		ret = -EDEV_INVALID_ARG;
		return ret;
	}

	if (state->conf.dummy_io &&
		state->current_position.partition &&
		state->current_position.block > 6) {

		/*
		 *  Dummy I/O mode
		 *  Do not write any data on partition1 (DP)
		 */

		++state->current_position.block;
		pos->block = state->current_position.block;
		state->eod[state->current_position.partition] = state->current_position.block;
		written = count;

	} else {
		/* clean up old records at this position */
		ret = _filedebug_remove_current_record(state);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 30021E, ret);
			return ret;
		}

		/* Increment Write Pass for consistency check */
		if(state->write_pass_prev == state->write_pass){
			ret = _set_wp(device, ++(state->write_pass));
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, 30022E, ret);
				return ret;
			}
		}

		/* create the file */
		fname = _filedebug_make_current_filename(state, rec_suffixes[SUFFIX_RECORD]);
		if (!fname) {
			ltfsmsg(LTFS_ERR, 30023E);
			ret = -EDEV_NO_MEMORY;
			return ret;
		}
		fd = open(fname,
				  O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,
				  S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
		if (fd < 0) {
			ltfsmsg(LTFS_ERR, 30024E, fname, errno);
			free(fname);
			return -EDEV_RW_PERM;
		}
		free(fname);

		/* write and close the file */
		written = write(fd, buf, count);
		if (written < 0) {
			ltfsmsg(LTFS_ERR, 30025E, errno);
			close(fd);
			return -EDEV_RW_PERM;
		}
		ret = close(fd);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 30026E, errno);
			return -EDEV_RW_PERM;
		}

		/* clean up old records */
		++state->current_position.block;
		pos->block = state->current_position.block;

		ret = _filedebug_write_eod(state);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 30027E, ret);
			return ret;
		}
	}

	ret = written;
	if (state->p0_warning && state->current_position.partition == 0 &&
		state->current_position.block >= state->p0_warning)
		pos->early_warning = true;
	else if (state->p1_warning && state->current_position.partition == 1 &&
		state->current_position.block >= state->p1_warning)
		pos->early_warning = true;
	/* Programmable early warning is set only when position moves into
	   programmable early warning zone in write() method. */
	if (state->p0_p_warning && state->current_position.partition == 0 &&
		state->current_position.block == state->p0_p_warning)
		pos->programmable_early_warning = true;
	else if (state->p1_p_warning && state->current_position.partition == 1 &&
		state->current_position.block == state->p1_p_warning)
		pos->programmable_early_warning = true;
	return ret;
}

int filedebug_writefm(void *device, size_t count, struct tc_position *pos, bool immed)
{
	int ret = -1;
	struct filedebug_data *state = (struct filedebug_data *)device;
	char *fname;
	int fd;
	size_t i;

	ltfsmsg(LTFS_DEBUG, 30028D, (unsigned int)count, state->current_position.partition,
		(unsigned long long)state->current_position.block,
		(unsigned long long)state->current_position.filemarks);

	if (!state->ready) {
		ltfsmsg(LTFS_ERR, 30029E);
		ret = -EDEV_NOT_READY;
		return ret;
	}

	/* Do nothing in case of WFM 0 */
	if (count == 0) {
		return DEVICE_GOOD;
	}

	if (state->conf.dummy_io &&
		state->current_position.partition &&
		state->current_position.block > 6) {

		/*
		 *  Dummy I/O mode
		 *  Do not write any data on partition1 (DP)
		 */

		for (i=0; i<count; ++i) {
			++state->current_position.block;
			++state->current_position.filemarks;
			pos->block = state->current_position.block;
			pos->filemarks = state->current_position.filemarks;
		}
		state->eod[state->current_position.partition] = state->current_position.block;
		ret = DEVICE_GOOD;

	} else {
		/* Increment Write Pass for consistency check */
		if(state->write_pass_prev == state->write_pass){
			ret = _set_wp(device, ++(state->write_pass));
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, 30030E, ret);
				return ret;
			}
		}

		for (i=0; i<count; ++i) {
			ret = _filedebug_remove_current_record(state);
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, 30031E, ret);
				return ret;
			}

			fname = _filedebug_make_current_filename(state, rec_suffixes[SUFFIX_FILEMARK]);
			if (!fname) {
				ltfsmsg(LTFS_ERR, 30032E);
				ret = -EDEV_NO_MEMORY;
				return ret;
			}

			fd = open(fname,
					  O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,
					  S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
			if (fd < 0) {
				ltfsmsg(LTFS_ERR, 30033E, fname, errno);
				free(fname);
				return -EDEV_RW_PERM;
			}
			free(fname);

			ret = close(fd);
			if (ret < 0) {
			ltfsmsg(LTFS_ERR, 30034E, errno);
			return -EDEV_RW_PERM;
			}

			++state->current_position.block;
			++state->current_position.filemarks;
			pos->block = state->current_position.block;
			pos->filemarks = state->current_position.filemarks;

			ret = _filedebug_write_eod(state);
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, 30035E, ret);
				return ret;
			}
		}
	}

	if (state->p0_warning && state->current_position.partition == 0 &&
		state->current_position.block >= state->p0_warning)
		pos->early_warning = true;
	else if (state->p1_warning && state->current_position.partition == 1 &&
		state->current_position.block >= state->p1_warning)
		pos->early_warning = true;
	if (state->p0_p_warning && state->current_position.partition == 0 &&
		state->current_position.block >= state->p0_p_warning)
		pos->programmable_early_warning = true;
	else if (state->p1_p_warning && state->current_position.partition == 1 &&
		state->current_position.block >= state->p1_p_warning)
		pos->programmable_early_warning = true;
	return ret;
}

int filedebug_rewind(void *device, struct tc_position *pos)
{
	struct filedebug_data *state = (struct filedebug_data *)device;

	if (!state->ready) {
		ltfsmsg(LTFS_ERR, 30036E);
		return -EDEV_NOT_READY;
	}

	/* Emulate unsupported cart */
	if (state->unsupported_tape) {
		return -EDEV_MEDIUM_FORMAT_ERROR;
	}

	emulate_rewind_wait(state);

	/* Does rewinding reset the partition? */
	state->current_position.block     = 0;
	state->current_position.filemarks = 0;
	state->clear_by_pc                = false;
	state->force_writeperm            = DEFAULT_WRITEPERM;
	state->force_readperm             = DEFAULT_READPERM;
	state->write_counter              = 0;
	state->read_counter               = 0;
	pos->block = state->current_position.block;
	pos->filemarks = 0;
	pos->early_warning = false;
	pos->programmable_early_warning = false;

	return DEVICE_GOOD;
}

int filedebug_locate(void *device, struct tc_position dest, struct tc_position *pos)
{
	int ret = 0;
	struct filedebug_data *state = (struct filedebug_data *)device;
	tape_filemarks_t count_fm = 0;
	tape_block_t     i;

	ltfsmsg(LTFS_DEBUG, 30197D, "locate", (unsigned long long)dest.partition,
		(unsigned long long)dest.block);

	if (!state->ready) {
		ltfsmsg(LTFS_ERR, 30037E);
		ret = -EDEV_NOT_READY;
		return ret;
	}

	/* Emulate unsupported cart/format */
	if (state->unsupported_tape || state->unsupported_format) {
		ret = -EDEV_MEDIUM_FORMAT_ERROR;
		return ret;
	}

	if (dest.partition >= MAX_PARTITIONS) {
		ltfsmsg(LTFS_ERR, 30038E, (unsigned long)dest.partition);
		ret = -EDEV_INVALID_ARG;
		return ret;
	}

	if (state->current_position.partition != dest.partition) {
		if (state->clear_by_pc) {
			state->clear_by_pc     = false;
			state->force_writeperm = DEFAULT_WRITEPERM;
			state->force_readperm  = DEFAULT_READPERM;
			state->force_errortype = DEFAULT_ERRORTYPE;
		}
	}

	emulate_seek_wait(state, &dest);

	state->current_position.partition = dest.partition;
	if (state->eod[dest.partition] == MISSING_EOD &&
		state->last_block[dest.partition] < dest.block)
			state->current_position.block = state->last_block[dest.partition] + 1;
	else if (state->eod[dest.partition] < dest.block)
		state->current_position.block = state->eod[dest.partition];
	else
		state->current_position.block = dest.block;
	pos->partition = state->current_position.partition;
	pos->block     = state->current_position.block;

	for(i = 0; i < state->current_position.block; ++i) {
		char *fname;

		fname = _filedebug_make_filename(state, state->current_position.partition,
										 i, rec_suffixes[SUFFIX_FILEMARK]);
		if (!fname) {
			ltfsmsg(LTFS_ERR, 30039E);
			ret = -EDEV_NO_MEMORY;
			return ret;
		}

		ret = _filedebug_check_file(fname);
		if (ret == 1)
			++count_fm;
		free(fname);
	}

	ret = 0;
	state->current_position.filemarks = count_fm;
	pos->filemarks = state->current_position.filemarks;

	if (state->p0_warning && state->current_position.partition == 0 &&
		state->current_position.block >= state->p0_warning)
		pos->early_warning = true;
	else if (state->p1_warning && state->current_position.partition == 1 &&
		state->current_position.block >= state->p1_warning)
		pos->early_warning = true;
	if (state->p0_p_warning && state->current_position.partition == 0 &&
		state->current_position.block >= state->p0_p_warning)
		pos->programmable_early_warning = true;
	else if (state->p1_p_warning && state->current_position.partition == 1 &&
		state->current_position.block >= state->p1_p_warning)
		pos->programmable_early_warning = true;
	return ret;
}

int filedebug_space(void *device, size_t count, TC_SPACE_TYPE type, struct tc_position *pos)
{
	int ret = 0;
	struct filedebug_data *state = (struct filedebug_data *)device;
	int ret_fm;
	tape_filemarks_t count_fm = 0;
	tape_block_t     i;

	if (!state->ready) {
		ltfsmsg(LTFS_ERR, 30040E);
		ret = -EDEV_NOT_READY;
		return ret;
	}

	/* Emulate unsupported cart/format */
	if (state->unsupported_tape || state->unsupported_format) {
		ret = -EDEV_MEDIUM_FORMAT_ERROR;
		return ret;
	}

	switch(type) {
		case TC_SPACE_EOD:
			ltfsmsg(LTFS_DEBUG, 30195D, "space to EOD");
			state->current_position.block = state->eod[state->current_position.partition];
			if(state->current_position.block == MISSING_EOD) {
				ret = -EDEV_RW_PERM;
				return ret;
			} else
				ret = 0;
			break;
		case TC_SPACE_FM_F:
			ltfsmsg(LTFS_DEBUG, 30196D, "space forward file marks", (unsigned long long)count);
			if(state->current_position.block == MISSING_EOD) {
				ret = -EDEV_RW_PERM;
				return ret;
			} else
				ret = _filedebug_space_fm(state, count, false);
			break;
		case TC_SPACE_FM_B:
			ltfsmsg(LTFS_DEBUG, 30196D, "space back file marks", (unsigned long long)count);
			if(state->current_position.block == MISSING_EOD) {
				ret = -EDEV_RW_PERM;
				return ret;
			} else
				ret = _filedebug_space_fm(state, count, true);
			break;
		case TC_SPACE_F:
			ltfsmsg(LTFS_DEBUG, 30196D, "space forward records", (unsigned long long)count);
			if(state->current_position.block == MISSING_EOD) {
				ret = -EDEV_RW_PERM;
				return ret;
			} else
				ret = _filedebug_space_rec(state, count, false);
			break;
		case TC_SPACE_B:
			ltfsmsg(LTFS_DEBUG, 30196D, "space back records", (unsigned long long)count);
			if(state->current_position.block == MISSING_EOD) {
				ret = -EDEV_RW_PERM;
				return ret;
			} else
				ret = _filedebug_space_rec(state, count, true);
			break;
		default:
			ltfsmsg(LTFS_ERR, 30041E);
			ret = -EDEV_INVALID_ARG;
			return ret;
	}

	pos->block = state->current_position.block;

	for(i = 0; i < state->current_position.block; ++i) {
		char *fname;

		fname = _filedebug_make_filename(state, state->current_position.partition,
										 i, rec_suffixes[SUFFIX_FILEMARK]);
		if (!fname) {
			ltfsmsg(LTFS_ERR, 30042E);
			ret = -EDEV_NO_MEMORY;
			return ret;
		}

		ret_fm = _filedebug_check_file(fname);
		if (ret_fm == 1)
			++count_fm;
		free(fname);
	}

	state->current_position.filemarks = count_fm;
	pos->filemarks = state->current_position.filemarks;

	if (state->p0_warning && state->current_position.partition == 0 &&
		state->current_position.block >= state->p0_warning)
		pos->early_warning = true;
	else if (state->p1_warning && state->current_position.partition == 1 &&
		state->current_position.block >= state->p1_warning)
		pos->early_warning = true;
	if (state->p0_p_warning && state->current_position.partition == 0 &&
		state->current_position.block >= state->p0_p_warning)
		pos->programmable_early_warning = true;
	else if (state->p1_p_warning && state->current_position.partition == 1 &&
		state->current_position.block >= state->p1_p_warning)
		pos->programmable_early_warning = true;
	return ret;
}

/**
 * NOTE: real tape drives erase from the current position. This function erases the entire
 * partition. The erase function is unused externally, but this implementation will need to be
 * fixed if it is ever needed.
 */
int filedebug_erase(void *device, struct tc_position *pos, bool long_erase)
{
	int ret;
	struct filedebug_data *state = (struct filedebug_data *)device;

	if (!state->ready) {
		ltfsmsg(LTFS_ERR, 30043E);
		return -EDEV_NOT_READY;
	}

	ltfsmsg(LTFS_DEBUG, 30044D, (unsigned long)state->current_position.partition);
	pos->block     = state->current_position.block;
	pos->filemarks = state->current_position.filemarks;

	ret = _filedebug_write_eod(state);
	return ret;
}

static inline int _sanitize_tape(struct filedebug_data *state)
{
	int ret = 0;
	int gen = DRIVE_FAMILY_GEN(state->drive_type);

	state->unsupported_tape = false;

	if (state->drive_type == 0) {
		state->unsupported_tape = true;
	} else if (gen == DRIVE_GEN_LTO5) {
		switch (state->conf.cart_type) {
			case TC_MP_LTO5D_CART:
				/* Do nothing */
				break;
			default:
				ltfsmsg(LTFS_INFO, 30086I, "LTO5", state->conf.cart_type);
				state->unsupported_tape = true;
				ret = -EDEV_MEDIUM_FORMAT_ERROR;
				break;
		}
	} else if (gen == DRIVE_GEN_LTO6) {
		switch (state->conf.cart_type) {
			case TC_MP_LTO5D_CART:
			case TC_MP_LTO6D_CART:
				/* Do nothing */
				break;
			default:
				ltfsmsg(LTFS_INFO, 30086I, "LTO6", state->conf.cart_type);
				state->unsupported_tape = true;
				ret = -EDEV_MEDIUM_FORMAT_ERROR;
				break;
		}
	} else if (gen == DRIVE_GEN_LTO7) {
		switch (state->conf.cart_type) {
			case TC_MP_LTO5D_CART:
			case TC_MP_LTO6D_CART:
			case TC_MP_LTO7D_CART:
				/* Do nothing */
				break;
			default:
				ltfsmsg(LTFS_INFO, 30086I, "LTO7", state->conf.cart_type);
				state->unsupported_tape = true;
				ret = -EDEV_MEDIUM_FORMAT_ERROR;
				break;
		}
	} else if (gen == DRIVE_GEN_LTO8) {
		switch (state->conf.cart_type) {
			case TC_MP_LTO6D_CART:
			case TC_MP_LTO7D_CART:
			case TC_MP_LTO8D_CART:
				/* Do nothing */
				break;
			default:
				ltfsmsg(LTFS_INFO, 30086I, "LTO8", state->conf.cart_type);
				state->unsupported_tape = true;
				ret = -EDEV_MEDIUM_FORMAT_ERROR;
				break;
		}
	} else if (gen == DRIVE_GEN_JAG4) {
		switch (state->conf.cart_type) {
			case TC_MP_JB:
			case TC_MP_JC:
			case TC_MP_JK:
				state->is_worm = false;
				break;
			case TC_MP_JX:
			case TC_MP_JY:
				state->is_worm = true;
				break;
			default:
				ltfsmsg(LTFS_INFO, 30086I, "TS1140", state->conf.cart_type);
				state->is_worm = false;
				state->unsupported_tape = true;
				ret = -EDEV_MEDIUM_FORMAT_ERROR;
				break;
		}
	} else if (gen == DRIVE_GEN_JAG5) {
		switch (state->conf.cart_type) {
			case TC_MP_JC:
			case TC_MP_JK:
			case TC_MP_JD:
			case TC_MP_JL:
				state->is_worm = false;
				break;
			case TC_MP_JY:
			case TC_MP_JZ:
				state->is_worm = true;
				break;
			default:
				ltfsmsg(LTFS_INFO, 30086I, "TS1150", state->conf.cart_type);
				state->is_worm = false;
				state->unsupported_tape = true;
				ret = -EDEV_MEDIUM_FORMAT_ERROR;
				break;
		}
	} else if (gen == DRIVE_GEN_JAG5A) {
		switch (state->conf.cart_type) {
			case TC_MP_JC:
			case TC_MP_JK:
			case TC_MP_JD:
			case TC_MP_JL:
				state->is_worm = false;
				break;
			case TC_MP_JY:
			case TC_MP_JZ:
				state->is_worm = true;
				break;
			default:
				ltfsmsg(LTFS_INFO, 30086I, "TS1155", state->conf.cart_type);
				state->is_worm = false;
				state->unsupported_tape = true;
				ret = -EDEV_MEDIUM_FORMAT_ERROR;
				break;
		}
	} else if (gen == DRIVE_GEN_JAG6) {
		switch (state->conf.cart_type) {
			case TC_MP_JC:
			case TC_MP_JK:
			case TC_MP_JD:
			case TC_MP_JL:
			case TC_MP_JE:
			case TC_MP_JM:
				state->is_worm = false;
				break;
			case TC_MP_JY:
			case TC_MP_JZ:
			case TC_MP_JV:
				state->is_worm = true;
				break;
			default:
				ltfsmsg(LTFS_INFO, 30086I, "TS1160", state->conf.cart_type);
				state->is_worm = false;
				state->unsupported_tape = true;
				ret = -EDEV_MEDIUM_FORMAT_ERROR;
				break;
		}
	} else {
		ltfsmsg(LTFS_INFO, 30086I, "Unexpected Drive", state->conf.cart_type);
		state->is_worm = false;
		state->unsupported_tape = true;
		ret = -EDEV_MEDIUM_FORMAT_ERROR;
	}

	return ret;
}

#define BARCODE_SIZE (36)

int filedebug_load(void *device, struct tc_position *pos)
{
	struct filedebug_data *state = (struct filedebug_data *)device;
	int ret;
	unsigned int i;
	uint64_t wp;
	char buf[BARCODE_SIZE], *dirlink, *config_file;
	struct stat d;

	if (state->ready) {
		emulate_load_wait(state);
		state->current_position.partition = 0;
		state->current_position.block     = 0;
		state->current_position.filemarks = 0;
		state->clear_by_pc                = false;
		state->force_writeperm            = DEFAULT_WRITEPERM;
		state->force_readperm             = DEFAULT_READPERM;
		state->write_counter              = 0;
		state->read_counter               = 0;
		return DEVICE_GOOD; /* already loaded the tape */
	}

	if (state->fd > 0) {
		memset(buf, 0, sizeof(buf));
		ret = lseek(state->fd, 0, SEEK_SET);
		if (ret < 0)
			return -EDEV_HARDWARE_ERROR;

		ret = read(state->fd, buf, sizeof(buf));
		if (ret != sizeof(buf)) {
			ltfsmsg(LTFS_ERR, 30045E, "");
			return -EDEV_HARDWARE_ERROR;
		}

		dirlink = buf;

		if(dirlink[strlen(dirlink) - 1] == '\n')
			dirlink[strlen(dirlink) - 1] = '\0';

		if(!strcmp(dirlink, "empty")) {
			ltfsmsg(LTFS_INFO, 30046I, "");
			return -EDEV_NO_MEDIUM;
		}

		if(state->dirname) {
			free(state->dirname);
			state->dirname = NULL;
		}

		ret = asprintf(&state->dirname, "%s/%s", state->dirbase, dirlink);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 10001E, "Directory name pointed by redirecting file");
			return -EDEV_INTERNAL_ERROR;
		}

		/* make sure directory exists */
		ret = stat(state->dirname, &d);
		if (ret || !S_ISDIR(d.st_mode)) {
			ltfsmsg(LTFS_ERR, 30047E, state->dirname);
			return -EDEV_NO_MEDIUM;
		}
	}

	ltfsmsg(LTFS_INFO, 30048I, state->dirname);

	/* Load configuration of cartridge */
	ret = asprintf(&config_file, "%s/%s", state->dirname, CARTRIDGE_CONFIG );
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 30049E, ret);
		return -EDEV_INTERNAL_ERROR;
	}

	ret = stat(config_file, &d);
	if (! ret && S_ISDIR(d.st_mode)) {
		ltfsmsg(LTFS_ERR, 30050E, ret);
		free(config_file);
		return -EDEV_INTERNAL_ERROR;
	}

	if (ret < 0 && errno == ENOENT)
		filedebug_conf_tc_write_xml(config_file, &state->conf);
	else if (! ret)
		filedebug_conf_tc_read_xml(config_file, &state->conf);
	else {
		ltfsmsg(LTFS_ERR, 30051E, ret);
		free(config_file);
		return -EDEV_INTERNAL_ERROR;
	}

	free(config_file);
	state->ready = true;

	state->unsupported_tape   = false;
	state->unsupported_format = false;

	/* Sanitize by cartridge type and configure WORM emulation flag */
	ret = _sanitize_tape(state);
	if (ret < 0) {
		return ret;
	}

	/* Configure internal read_only flag */
	ret = ibm_tape_is_mountable( state->drive_type,
								NULL,
								state->conf.cart_type,
								state->conf.density_code,
								false);
	switch(ret) {
		case MEDIUM_PERFECT_MATCH:
		case MEDIUM_WRITABLE:
			if (state->conf.emulate_readonly)
				state->is_readonly = true;
			else
				state->is_readonly = false;
			break;
		case MEDIUM_READONLY:
			state->is_readonly = true;
			break;
		case MEDIUM_CANNOT_ACCESS:
			ltfsmsg(LTFS_INFO, 30088I, state->drive_type, state->conf.density_code);
			state->unsupported_format = true;
			if (IS_LTO(state->drive_type))
				return -EDEV_MEDIUM_FORMAT_ERROR;
			break;
		case MEDIUM_UNKNOWN:
		case MEDIUM_PROBABLY_WRITABLE:
		default:
			/* Unexpected condition */
			return -LTFS_UNEXPECTED_VALUE;
			break;
	}

	for (i=0; i<MAX_PARTITIONS; ++i) {
		ret = filedebug_search_eod(state, i);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 30052E, i, ret);
			return -EDEV_INTERNAL_ERROR;
		}
	}

	if (state->conf.dummy_io)
		_filedebug_remove_record(state, 1, state->eod[1]);

	state->current_position.partition = 0;
	state->current_position.block     = 0;
	state->current_position.filemarks = 0;
	if (state->eod[1] == 0)
		state->partitions = 1;
	else
		state->partitions = MAX_PARTITIONS;

	pos->partition = state->current_position.partition;
	pos->block     = state->current_position.block;
	pos->filemarks = state->current_position.filemarks;

	wp = 0;
	if(_get_wp(device, &wp) != 0) {
		ltfsmsg(LTFS_ERR, 30053E);
		return -EDEV_INTERNAL_ERROR;
	}

	state->write_pass_prev = wp;
	state->write_pass = wp;

	/* Calculate early warning thresholds */
	if (state->partitions == 2) {
		/* Assume 512KB per 1 record */
		state->p0_warning = calc_p0_cap(state) * 2;
		state->p1_warning = calc_p1_cap(state) * 2;
		state->p0_p_warning = state->p0_warning / 2;
		state->p1_p_warning = state->p1_warning - state->p0_p_warning;
	} else {
		state->p0_warning = calc_p0_cap(state) * 2;
		state->p1_warning = 0;
		state->p0_p_warning = state->p0_warning * 2;
		state->p1_p_warning = 0;
	}

	emulate_threading_wait(state);

	return DEVICE_GOOD;
}

int filedebug_unload(void *device, struct tc_position *pos)
{
	struct filedebug_data *state = (struct filedebug_data *)device;
	char *config_file;
	int ret;

	/* Write EOD of DP here when dummy io mode is enabled */
	if (state->conf.dummy_io) {
		state->current_position.partition = 1;
		state->current_position.block = state->eod[1];
		_filedebug_write_eod(state);
	}

	emulate_load_wait(state);

	state->ready = false;
	state->current_position.partition = 0;
	state->current_position.block     = 0;
	state->current_position.filemarks = 0;
	state->clear_by_pc                = false;
	state->force_writeperm            = DEFAULT_WRITEPERM;
	state->force_readperm             = DEFAULT_READPERM;
	state->write_counter              = 0;
	state->read_counter               = 0;

	pos->partition = state->current_position.partition;
	pos->block     = state->current_position.block;
	pos->filemarks = state->current_position.filemarks;

	/* Save configuration of cartridge */
	ret = asprintf(&config_file, "%s/%s", state->dirname, CARTRIDGE_CONFIG );
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 30049E, ret);
		return -EDEV_INTERNAL_ERROR;
	}

	filedebug_conf_tc_write_xml(config_file, &state->conf);

	if (config_file)
		free(config_file);

	emulate_threading_wait(state);

	return DEVICE_GOOD;
}

int filedebug_readpos(void *device, struct tc_position *pos)
{
	struct filedebug_data *state = (struct filedebug_data *)device;

	if (!state->ready) {
		ltfsmsg(LTFS_ERR, 30054E);
		return -EDEV_NOT_READY;
	}

	pos->partition = state->current_position.partition;
	pos->block     = state->current_position.block;
	pos->filemarks = state->current_position.filemarks;

	ltfsmsg(LTFS_DEBUG, 30198D, "readpos", (unsigned long long)state->current_position.partition,
		(unsigned long long)state->current_position.block,
		(unsigned long long)state->current_position.filemarks);
	return DEVICE_GOOD;
}

int filedebug_setcap(void *device, uint16_t proportion)
{
	struct filedebug_data *state = (struct filedebug_data *)device;
	struct tc_position pos;

	if(state->current_position.partition != 0 ||
		state->current_position.block != 0)
	{
		ltfsmsg(LTFS_ERR, 30055E);
		return -EDEV_ILLEGAL_REQUEST;
	}

	state->partitions = 1;

	/* erase all partitions */
	state->current_position.partition = 1;
	state->current_position.block = 0;
	filedebug_erase(state, &pos, false);
	state->current_position.partition = 0;
	state->current_position.block = 0;
	filedebug_erase(state, &pos, false);

	return DEVICE_GOOD;
}

int filedebug_format(void *device, TC_FORMAT_TYPE format, const char *vol_name, const char *barcode_name, const char *vol_mam_uuid)
{
	struct filedebug_data *state = (struct filedebug_data *)device;
	struct tc_position pos;
	int ret = 0;

	if(state->current_position.partition != 0 ||
		state->current_position.block != 0)
	{
		ltfsmsg(LTFS_ERR, 30056E);
		return -EDEV_ILLEGAL_REQUEST;
	}

	/* Emulate WORM */
	if (state->is_worm &&
		(state->eod[0] != 0 || state->eod[1] != 0) ) {
		return -EDEV_WRITE_PROTECTED_WORM;;
	}

	/* Emulate read-only tape and write protected tape */
	if (state->is_readonly) {
		if (state->conf.emulate_readonly)
			ret = -EDEV_WRITE_PROTECTED; /* Emulate 07/2700 */
		else
			ret = -EDEV_DATA_PROTECT;    /* Emulate 07/3005 */

		ltfsmsg(LTFS_INFO, 30085I, ret, state->serial_number);

		return ret;
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
			ltfsmsg(LTFS_ERR, 30057E);
			return -EDEV_INVALID_ARG;
	}

	/* erase all partitions */
	state->current_position.partition = 1;
	state->current_position.block = 0;
	filedebug_erase(state, &pos, false);
	state->current_position.partition = 0;
	state->current_position.block = 0;
	filedebug_erase(state, &pos, false);

	/* Calculate early warning thresholds */
	if (state->partitions == 2) {
		/* Assume 512KB per 1 record */
		state->p0_warning = calc_p0_cap(state) * 2;
		state->p1_warning = calc_p1_cap(state) * 2;
		state->p0_p_warning = state->p0_warning / 2;
		state->p1_p_warning = state->p1_warning - state->p0_p_warning;
	} else {
		state->p0_warning = calc_p0_cap(state) * 2;
		state->p1_warning = 0;
		state->p0_p_warning = state->p0_warning * 2;
		state->p1_p_warning = 0;
	}

	return DEVICE_GOOD;
}

int filedebug_remaining_capacity(void *device, struct tc_remaining_cap *cap)
{
	struct filedebug_data *state = (struct filedebug_data *)device;

	if (!state->ready) {
		ltfsmsg(LTFS_ERR, 30058E);
		return DEVICE_GOOD;
	}

	if(state->partitions == 2) {
		cap->max_p0       = calc_p0_cap(state);
		cap->remaining_p0 = calc_p0_remaining(state);
		cap->max_p1       = calc_p1_cap(state);
		cap->remaining_p1 = calc_p1_remaining(state);
	} else {
		cap->max_p0       = state->conf.capacity_mb;
		cap->remaining_p0 = 0;
		cap->max_p1       = 0;
		cap->remaining_p1 = 0;
	}

	return DEVICE_GOOD;
}

int filedebug_get_cartridge_health(void *device, struct tc_cartridge_health *cart_health)
{
	cart_health->mounts           = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->written_ds       = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->write_temps      = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->write_perms      = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_ds          = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_temps       = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_perms       = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->write_perms_prev = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_perms_prev  = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->written_mbytes   = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_mbytes      = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->passes_begin     = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->passes_middle    = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->tape_efficiency  = UNSUPPORTED_CARTRIDGE_HEALTH;

	return DEVICE_GOOD;
}

int filedebug_get_tape_alert(void *device, uint64_t *tape_alert)
{
	*tape_alert = 0;
	return DEVICE_GOOD;
}

int filedebug_clear_tape_alert(void *device, uint64_t tape_alert)
{
	return DEVICE_GOOD;
}

int filedebug_get_xattr(void *device, const char *name, char **buf)
{
	struct filedebug_data *state = (struct filedebug_data *) device;
	int ret = -LTFS_NO_XATTR;

	if (!strcmp(name, "ltfs.vendor.IBM.seekLatency")) {
		ret = asprintf(buf, "%lds%ldns",
			state->accumulated_delay.tv_sec,
			state->accumulated_delay.tv_nsec);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 10001E, "get_xattr buffer");
			ret = -LTFS_NO_MEMORY;
		} else
			ret = DEVICE_GOOD;
	}

	return ret;
}

int filedebug_set_xattr(void *device, const char *name, const char *buf, size_t size)
{
	struct filedebug_data *state = (struct filedebug_data *) device;
	int ret = -LTFS_NO_XATTR;
	uint64_t attr_val;
	char *null_terminated;
	int64_t perm_count = 0;

	if (!size)
		return -LTFS_BAD_ARG;

	null_terminated = calloc(1, size + 1);
	if (! null_terminated) {
		ltfsmsg(LTFS_ERR, 10001E, "ibmtape_set_xattr: null_term");
		return -LTFS_NO_MEMORY;
	}
	memcpy(null_terminated, buf, size);

	if (! strcmp(name, "ltfs.vendor.IBM.forceErrorWrite")) {
		perm_count = strtoll(null_terminated, NULL, 0);
		if (perm_count < 0) {
			state->force_writeperm = -perm_count;
			state->clear_by_pc     = true;
		} else {
			state->force_writeperm = perm_count;
			state->clear_by_pc     = false;
		}
		if (state->force_writeperm && state->force_writeperm < THRESHOLD_FORCE_WRITE_NO_WRITE)
			state->force_writeperm = THRESHOLD_FORCE_WRITE_NO_WRITE;
		state->write_counter = 0;
		ret = DEVICE_GOOD;
	} else if (! strcmp(name, "ltfs.vendor.IBM.forceErrorType")) {
		state->force_errortype = strtol(null_terminated, NULL, 0);
		ret = DEVICE_GOOD;
	} else if (! strcmp(name, "ltfs.vendor.IBM.forceErrorRead")) {
		perm_count = strtoll(null_terminated, NULL, 0);
		if (perm_count < 0) {
			state->force_readperm = -perm_count;
			state->clear_by_pc    = true;
		} else {
			state->force_readperm = perm_count;
			state->clear_by_pc    = false;
		}
		state->read_counter = 0;
		ret = DEVICE_GOOD;
	} else if (!strcmp(name, "ltfs.vendor.IBM.seekLatency")) {
		attr_val = strtoull(null_terminated, NULL, 0);
		if ((attr_val == ULLONG_MAX && errno) || attr_val > 0)
			ret = -EDEV_INVALID_ARG;
		else {
			state->accumulated_delay.tv_sec = 0;
			state->accumulated_delay.tv_nsec = 0;
			ret = DEVICE_GOOD;
		}
	}

	free(null_terminated);

	return ret;
}

int filedebug_logsense(void *device, const uint8_t page, unsigned char *buf, const size_t size)
{
	ltfsmsg(LTFS_ERR, 10007E, __FUNCTION__);
	return -EDEV_UNSUPPORTED_FUNCTION;
}

int filedebug_modesense(void *device, const uint8_t page, const TC_MP_PC_TYPE pc, const uint8_t subpage, unsigned char *buf, const size_t size)
{
	uint16_t pews = 0;
	struct filedebug_data *state = (struct filedebug_data *)device;

	memset(buf, 0, size);

	buf[16] = page;

	/* Return density code or cart type, if specific value is set */
	if (page == TC_MP_SUPPORTEDPAGE && pc == TC_MP_PC_CURRENT && subpage == 0x00)
		buf[8] = state->conf.density_code;
	else if (page == TC_MP_MEDIUM_PARTITION && pc == TC_MP_PC_CURRENT && subpage == 0x00)
		buf[2] = state->conf.cart_type;
	else if (page == TC_MP_DEV_CONFIG_EXT && pc == TC_MP_PC_CURRENT && subpage == 0x01) {
		pews = calc_p0_cap(state) / 2;
		buf[17] = subpage;
		buf[22] = (uint8_t)(pews >> 8 & 0xFF);
		buf[23] = (uint8_t)(pews      & 0xFF);
	}

	return DEVICE_GOOD;
}

int filedebug_modeselect(void *device, unsigned char *buf, const size_t size)
{
	int ret = 0;
	struct filedebug_data *state = (struct filedebug_data *)device;

	if (buf[16] == TC_MP_READ_WRITE_CTRL && buf[26] != 0) {
		/* Update density code, if specific value is set */
		state->conf.density_code = buf[26];

		/* TODO: Create a function to update state for read-only handling */
		/* Recalculate read-only condition */
		state->unsupported_format = false;
		ret = ibm_tape_is_mountable( state->drive_type,
									 NULL,
									 state->conf.cart_type,
									 state->conf.density_code,
									 false);
		switch(ret) {
			case MEDIUM_PERFECT_MATCH:
			case MEDIUM_WRITABLE:
				if (state->conf.emulate_readonly)
					state->is_readonly = true;
				else
					state->is_readonly = false;
				break;
			case MEDIUM_READONLY:
				state->is_readonly = true;
				break;
			case MEDIUM_CANNOT_ACCESS:
				ltfsmsg(LTFS_INFO, 30088I, state->drive_type, state->conf.density_code);
				state->unsupported_format = true;
				if (IS_LTO(state->drive_type))
					return -EDEV_MEDIUM_FORMAT_ERROR;
				break;
			case MEDIUM_UNKNOWN:
			case MEDIUM_PROBABLY_WRITABLE:
			default:
				/* Unexpected condition */
				return -LTFS_UNEXPECTED_VALUE;
				break;
		}
	}

	return DEVICE_GOOD;
}

int filedebug_reserve_unit(void *device)
{
	struct filedebug_data *state = (struct filedebug_data *)device;
	if (state->device_reserved) {
		ltfsmsg(LTFS_ERR, 30059E);
		return -EDEV_ILLEGAL_REQUEST;
	}
	state->device_reserved = true;
	return DEVICE_GOOD;
}

int filedebug_release_unit(void *device)
{
	struct filedebug_data *state = (struct filedebug_data *)device;
	state->device_reserved = false;
	return DEVICE_GOOD;
}

int filedebug_prevent_medium_removal(void *device)
{
	struct filedebug_data *state = (struct filedebug_data *)device;
	if (!state->ready) {
		ltfsmsg(LTFS_ERR, 30060E);
		return -EDEV_NOT_READY;
	}
	state->medium_locked = true; /* TODO: fail if medium is already locked? */
	return DEVICE_GOOD;
}

int filedebug_allow_medium_removal(void *device)
{
	struct filedebug_data *state = (struct filedebug_data *)device;
	if (!state->ready) {
		ltfsmsg(LTFS_ERR, 30061E);
		return -EDEV_NOT_READY;
	}
	state->medium_locked = false;
	return DEVICE_GOOD;
}

int filedebug_read_attribute(void *device, const tape_partition_t part, const uint16_t id
							 , unsigned char *buf, const size_t size)
{
	struct filedebug_data *state = (struct filedebug_data *)device;
	char *fname;
	int fd;
	ssize_t bytes_read;

	ltfsmsg(LTFS_DEBUG, 30197D, "readattr", (unsigned long long)part, (unsigned long long)id);

	/* Open attribute record */
	fname = _filedebug_make_attrname(state, part, id);
	if (!fname)
		return -EDEV_NO_MEMORY;
	fd = open(fname, O_RDONLY | O_BINARY);
	free(fname);
	if (fd < 0) {
		if (errno == ENOENT) {
			return -EDEV_INVALID_FIELD_CDB;
		} else {
			ltfsmsg(LTFS_WARN, 30062W, errno);
			return -EDEV_CM_PERM;
		}
	}

	/* TODO: return -EDEV_INVALID_ARG if buffer is too small to hold complete record? */
	bytes_read = read(fd, buf, size);
	if(bytes_read == -1) {
		ltfsmsg(LTFS_WARN, 30063W, errno);
		close(fd);
		return -EDEV_CM_PERM;
	}
	close(fd);

	return DEVICE_GOOD;
}

int filedebug_write_attribute(void *device, const tape_partition_t part
							  , const unsigned char *buf, const size_t size)
{
	struct filedebug_data *state = (struct filedebug_data *)device;
	char *fname;
	int fd;
	ssize_t written;
	uint16_t id, attr_size;
	size_t i = 0;

	while(size > i)
	{
		id = ltfs_betou16(buf + i);
		attr_size = ltfs_betou16(buf + (i + 3));

		ltfsmsg(LTFS_DEBUG, 30197D, "writeattr", (unsigned long long)part, (unsigned long long)id);

		/* Create attribute record */
		fname = _filedebug_make_attrname(state, part, id);
		if (!fname) {
			ltfsmsg(LTFS_ERR, 30064E);
			return -EDEV_NO_MEMORY;
		}
		fd = open(fname,
				  O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,
				  S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
		free(fname);
		if (fd < 0) {
			ltfsmsg(LTFS_ERR, 30065E, errno);
			return -EDEV_CM_PERM;
		}

		/* write and close the file */
		written = write(fd, buf, size);
		if (written < 0) {
			ltfsmsg(LTFS_ERR, 30066E, errno);
			close(fd);
			return -EDEV_CM_PERM;
		}
		close(fd);

		i += (attr_size + 5); /* Add header size of an attribute */
	}

	return DEVICE_GOOD;
}

int filedebug_allow_overwrite(void *device, const struct tc_position pos)
{
	return DEVICE_GOOD;
}

int filedebug_get_eod_status(void *device, int partition)
{
	struct filedebug_data *state = (struct filedebug_data *)device;

	if(state->eod[partition] == MISSING_EOD)
		return EOD_MISSING;
	else
		return EOD_GOOD;
}

int filedebug_set_compression(void *device, bool enable_compression, struct tc_position *pos)
{
	struct filedebug_data *state = (struct filedebug_data *)device;
	if (!state->ready) {
		ltfsmsg(LTFS_ERR, 30067E);
		return -EDEV_NOT_READY;
	}
	pos->block = state->current_position.block;
	pos->filemarks = state->current_position.filemarks;
	return DEVICE_GOOD;
}

int filedebug_set_default(void *device)
{
	return DEVICE_GOOD;
}

int filedebug_get_parameters(void *device, struct tc_drive_param *params)
{
	struct filedebug_data *state = (struct filedebug_data *)device;

	params->max_blksize           = FILE_DEBUG_MAX_BLOCK_SIZE;

	params->cart_type             = state->conf.cart_type;
	params->density               = state->conf.density_code;

	params->write_protect       = 0;
	if ( state->conf.emulate_readonly )
			params->write_protect |= VOL_PHYSICAL_WP;

	/* TODO: Following field shall be implemented in the future */
	//params->is_encrypted          = false;
	//params->is_worm               = state->is_worm;

	return DEVICE_GOOD;
}

const char *filedebug_default_device_name(void)
{
	return filedebug_default_device;
}

/**
 * examine given directory to find EOD for a partition.
 * returns 0 on success, negative value on error
 * on success, sets the tape position to EOD on the given partition.
 */
int filedebug_search_eod(struct filedebug_data *state, int partition)
{
	char *fname;
	size_t fname_len;
	int ret;
	int i;
	int f[3] = { 1, 1, 0 };
	DIR *dp;
	int p;
	tape_block_t b;
	struct dirent *entry;

	state->current_position.partition = partition;
	state->current_position.block     = 0;

	/* loop until an EOD mark is found or no record is found */
	while ((f[0] || f[1]) && !f[2]) {
		/* check for a record */
		fname = _filedebug_make_current_filename(state, '.');
		if (!fname) {
			ltfsmsg(LTFS_ERR, 30068E);
			return -EDEV_NO_MEMORY;
		}
		fname_len = strlen(fname);

		for (i=0; i<3; ++i) {
			fname[fname_len-1] = rec_suffixes[i];
			f[i] = _filedebug_check_file(fname);
			if (f[i] < 0) {
				ltfsmsg(LTFS_ERR, 30069E, f[i]);
				free(fname);
				return f[i];
			}
		}

		free(fname);
		++state->current_position.block;
	}
	--state->current_position.block;

	if(!f[2] && state->current_position.block != 0) {
		state->last_block[state->current_position.partition] = state->current_position.block;
		state->eod[state->current_position.partition] = MISSING_EOD;
		if (state->conf.dummy_io) {
			dp = opendir(state->dirname);
			if (! dp) {
				ltfsmsg(LTFS_ERR, 30004E, state->dirname);
				return 0;
			}

			while ((entry = readdir(dp))) {
				if ( entry->d_name[strlen(entry->d_name) - 1] == 'E') {
					entry->d_name[strlen(entry->d_name) - 2] = '\0';
					entry->d_name[1] = '\0';
					p = atoi(entry->d_name);
					b = atoll(&entry->d_name[2]);
					if (p == partition) {
						state->current_position.block = state->last_block[partition] = --b;
						state->eod[partition] = 0;
						ret = _filedebug_write_eod(state);
						if (ret < 0) {
							ltfsmsg(LTFS_ERR, 30070E, ret);
							closedir(dp);
							return ret;
						}
						break;
					}
				}
			}
			closedir(dp);
		}
	} else {
		ret = _filedebug_write_eod(state);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 30070E, ret);
			return ret;
		}
	}

	return DEVICE_GOOD;
}

/**
 * Write an EOD mark at the current tape position, remove extra records, and
 * update the EOD in the state variable.
 * Returns 0 on success, negative value on failure.
 */
int _filedebug_write_eod(struct filedebug_data *state)
{
	char *fname;
	int fd;
	int ret;
	uint64_t i;
	bool remove_extra_rec = true;

	if(state->eod[state->current_position.partition] == MISSING_EOD)
		remove_extra_rec = false;

	/* remove any existing record at this position */
	ret = _filedebug_remove_current_record(state);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 30071E, ret);
		return ret;
	}

	/* create EOD record */
	fname = _filedebug_make_current_filename(state, 'E');
	if (!fname) {
		ltfsmsg(LTFS_ERR, 30072E);
		return -EDEV_NO_MEMORY;
	}
	fd = open(fname,
			  O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,
			  S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
	free(fname);
	if (fd < 0 || close(fd) < 0) {
		ltfsmsg(LTFS_ERR, 30073E, errno);
		return -EDEV_RW_PERM;
	}

	if(remove_extra_rec) {
		/* remove records following this position */
		for (i=state->current_position.block+1; i<=state->eod[state->current_position.partition]; ++i) {
			ret = _filedebug_remove_record(state, state->current_position.partition, i);
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, 30074E, ret);
				return ret;
			}
		}
	}

	state->last_block[state->current_position.partition] = state->current_position.block - 1;
	state->eod[state->current_position.partition] = state->current_position.block;
	return DEVICE_GOOD;
}

/**
 * Delete the file associated with the current tape position.
 */
int _filedebug_remove_current_record(const struct filedebug_data *state)
{
	return _filedebug_remove_record(state
									, state->current_position.partition
									, state->current_position.block);
}

/**
 * Delete the file associated with a given tape position.
 * @return 1 on successful delete, 0 if no file found, negative on error.
 */
int _filedebug_remove_record(const struct filedebug_data *state,
	int partition, uint64_t blknum)
{
	char *fname;
	size_t fname_len;
	int i;
	int ret;

	fname = _filedebug_make_filename(state, partition, blknum, '.');
	if (!fname) {
		ltfsmsg(LTFS_ERR, 30075E);
		return -EDEV_NO_MEMORY;
	}
	fname_len = strlen(fname);

	for (i=0; i<3; ++i) {
		fname[fname_len-1] = rec_suffixes[i];
		ret = unlink(fname);
		if (ret < 0 && errno != ENOENT) {
			ltfsmsg(LTFS_ERR, 30076E, errno);
			free(fname);
			return -EDEV_RW_PERM;
		}
	}

	free(fname);
	return DEVICE_GOOD;
}

/**
 * Check for the existence and writability of a file.
 * This function is silent: callers are expected to report errors for themselves.
 * @return 1 on success, 0 if file does not exist, and -errno on error
 */
int _filedebug_check_file(const char *fname)
{
	int fd;
	int ret;

	fd = open(fname, O_RDWR | O_BINARY);
	if (fd < 0) {
		if (errno == ENOENT)
			return 0;
		else
			return -EDEV_RW_PERM;
	} else {
		ret = close(fd);
		if (ret < 0)
			return -EDEV_RW_PERM;
		else
			return 1;
	}
}

/**
 * Call _filedebug_make_filename with the current tape position
 */
char *_filedebug_make_current_filename(const struct filedebug_data *state, char type)
{
	return _filedebug_make_filename(state
									, state->current_position.partition
									, state->current_position.block
									, type);
}

/**
 * Make filename for a record.
 * Returns a string on success or NULL on failure. The caller is responsible for freeing
 * the returned memory. Failure probably means asprintf couldn't allocate memory.
 */
char *_filedebug_make_filename(const struct filedebug_data *state,
	int part, uint64_t pos, char type)
{
	char *fname;
	int ret;
	ret = asprintf(&fname, "%s/%d_%"PRIu64"_%c", state->dirname, part, pos, type);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return NULL;
	}
	return fname;
}

/**
 * Make filename for a Attribute.
 * Returns a string on success or NULL on failure. The caller is responsible for freeing
 * the returned memory. Failure probably means asprintf couldn't allocate memory.
 */
char *_filedebug_make_attrname(const struct filedebug_data *state, int part, int id)
{
	char *fname;
	int ret;
	ret = asprintf(&fname, "%s/attr_%d_%x", state->dirname, part, id);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return NULL;
	}
	return fname;
}

/**
 * Space over filemarks. Position immediately after the FM if spacing forwards, or
 * immediately before it if spacing backwards.
 * @param state the tape state
 * @param count number of filemarks to skip
 * @param back true to skip backwards, false to skip forwards
 * @return 0 on success or a negative value on error
 */
int _filedebug_space_fm(struct filedebug_data *state, uint64_t count, bool back)
{
	char *fname;
	uint64_t fm_count = 0;
	int ret;

	if (count == 0)
		return DEVICE_GOOD;

	if (back && state->current_position.block > 0)
		--state->current_position.block;

	while (1) {
		if (!back &&
			state->current_position.block == state->eod[state->current_position.partition]) {
			ltfsmsg(LTFS_ERR, 30077E);
			return -EDEV_EOD_DETECTED;
		}

		if (!back &&
			state->current_position.block == state->last_block[state->current_position.partition] + 1) {
			return -EDEV_RW_PERM;
		}

		fname = _filedebug_make_current_filename(state, rec_suffixes[SUFFIX_FILEMARK]);
		if (!fname) {
			ltfsmsg(LTFS_ERR, 30078E);
			return -EDEV_NO_MEMORY;
		}
		ret = _filedebug_check_file(fname);
		free(fname);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 30079E, ret);
			return ret;
		} else if (ret > 0) {
			++fm_count;
			if (fm_count == count) {
				if (!back)
					++state->current_position.block;
				return DEVICE_GOOD;
			}
		}

		if (back) {
			if (state->current_position.block == 0) {
				ltfsmsg(LTFS_ERR, 30080E);
				return -EDEV_BOP_DETECTED;
			}
			--state->current_position.block;
		} else {
			++state->current_position.block;
		}
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
int _filedebug_space_rec(struct filedebug_data *state, uint64_t count, bool back)
{
	char *fname;
	uint64_t rec_count = 0;
	int ret;

	if (count == 0)
		return DEVICE_GOOD;

	while (1) {
		if (!back &&
			state->current_position.block == state->eod[state->current_position.partition]) {
			return -EDEV_EOD_DETECTED;
		}

		if (!back &&
			state->current_position.block == state->last_block[state->current_position.partition] + 1) {
			return -EDEV_RW_PERM;
		}

		/* check for filemark */
		fname = _filedebug_make_current_filename(state, rec_suffixes[SUFFIX_FILEMARK]);
		if (!fname)
			return -EDEV_NO_MEMORY;
		ret = _filedebug_check_file(fname);
		free(fname);
		if (ret < 0)
			return ret;
		if (ret > 0 && (!back || rec_count > 0)) {
			if (!back)
				++state->current_position.block;
			return -EDEV_RW_PERM;
		}

		if (back) {
			if (state->current_position.block == 0) {
				return -EDEV_BOP_DETECTED;
			}
			--state->current_position.block;
		} else {
			++state->current_position.block;
		}

		++rec_count;
		if (rec_count == count) {
			return DEVICE_GOOD;
		}
	}
}

int _get_wp(struct filedebug_data *device, uint64_t *wp)
{
	int ret;
	unsigned char wp_data[TC_MAM_PAGE_VCR_SIZE + TC_MAM_PAGE_HEADER_SIZE];

	memset(wp_data, 0, sizeof(wp_data));

	*wp = 0;
	ret = filedebug_read_attribute(device, 0, TC_MAM_PAGE_VCR
								   , wp_data, sizeof(wp_data));
	if(ret == 0)
		*wp = ltfs_betou32(wp_data + 5);
	else
		ret = _set_wp(device, (uint64_t)1);

	return ret;
}

int _set_wp(struct filedebug_data *device, uint64_t wp)
{
	int ret;
	unsigned char wp_data[TC_MAM_PAGE_VCR_SIZE + TC_MAM_PAGE_HEADER_SIZE];

	ltfs_u16tobe(wp_data, TC_MAM_PAGE_VCR);
	wp_data[2] = 0;
	ltfs_u16tobe(wp_data + 3, TC_MAM_PAGE_VCR_SIZE);
	ltfs_u32tobe(wp_data + 5, (uint32_t)wp);

	ret = filedebug_write_attribute(device, 0, wp_data, sizeof(wp_data));

	return ret;
}

/**
 * Get valid device list. Returns an empty list because there's no way to enumerate
 * all the possible valid devices for this backend.
 */
#define DRIVE_FILE_PREFIX "Drive_"

int filedebug_get_device_list(struct tc_drive_info *buf, int count)
{
	char *filename, *devdir, line[1024];
	FILE *infile;
	DIR *dp;
	struct dirent *entry;
	int deventries = 0;
	char *ser= NULL, *pid = NULL, *tmp;
	int i;

	if (! original_pid) {
		original_pid = (long)getpid();
	}

	/* Create a file to indicate current directory of drive link (for tape file backend) */
	asprintf(&filename, "%s/ltfs%ld", DRIVE_LIST_DIR, original_pid);
	if (!filename) {
		ltfsmsg(LTFS_ERR, 10001E, "filechanger_data drive file name");
		return -LTFS_NO_MEMORY;
	}
	ltfsmsg(LTFS_INFO, 30081I, filename);
	infile = fopen(filename, "r");
	if (!infile) {
		ltfsmsg(LTFS_INFO, 30082I, filename);
		return 0;
	} else {
		devdir = fgets(line, sizeof(line), infile);
		if(devdir[strlen(devdir) - 1] == '\n')
			devdir[strlen(devdir) - 1] = '\0';
		fclose(infile);
		free(filename);
	}

	ltfsmsg(LTFS_INFO, 30083I, devdir);
	dp = opendir(devdir);
	if (! dp) {
		ltfsmsg(LTFS_ERR, 30004E, devdir);
		return 0;
	}

	while ((entry = readdir(dp))) {
		if (strncmp(entry->d_name, DRIVE_FILE_PREFIX, strlen(DRIVE_FILE_PREFIX)))
			continue;

		if (buf && deventries < count) {
			tmp = strdup(entry->d_name);
			if (! *tmp) {
				ltfsmsg(LTFS_ERR, 10001E, "filedebug_get_device_list");
				return -ENOMEM;
			}

			for (i = strlen(tmp) - 1; i > 0; --i) {
				if (tmp[i] == '.') {
					tmp[i] = '\0';
					pid = &tmp[i + 1];
				}
				if (tmp[i] == '_') {
					tmp[i] = '\0';
					ser = &tmp[i + 1];
					break;
				}
			}

			snprintf(buf[deventries].name, TAPE_DEVNAME_LEN_MAX, "%s/%s", devdir, entry->d_name);
			snprintf(buf[deventries].vendor, TAPE_VENDOR_NAME_LEN_MAX, "DUMMY");
			snprintf(buf[deventries].model, TAPE_MODEL_NAME_LEN_MAX, "%s", pid);
			snprintf(buf[deventries].serial_number, TAPE_SERIAL_LEN_MAX, "%s", ser);
			snprintf(buf[deventries].product_name, PRODUCT_NAME_LENGTH, "[%s]", pid);

			buf[deventries].host    = 0;
			buf[deventries].channel = 0;
			buf[deventries].target  = 0;
			buf[deventries].lun     = -1;

			ltfsmsg(LTFS_DEBUG, 30084D, buf[deventries].name, buf[deventries].vendor,
					buf[deventries].model, buf[deventries].serial_number);

			free(tmp);
		}

		deventries++;
	}

	closedir(dp);

	return deventries;
}

int filedebug_set_key(void *device, const unsigned char *keyalias, const unsigned char *key)
{
	return -EDEV_UNSUPPORTED_FUNCTION;
}

int filedebug_get_keyalias(void *device, unsigned char **keyalias)
{
	return -EDEV_UNSUPPORTED_FUNCTION;
}

int filedebug_takedump_drive(void *device, bool nonforced_dump)
{
	/* Do nothing */
	return DEVICE_GOOD;
}

int filedebug_is_mountable(void *device, const char *barcode, const unsigned char cart_type,
							const unsigned char density)
{
	int ret;
	struct filedebug_data *state = (struct filedebug_data *)device;

	ret = ibm_tape_is_mountable( state->drive_type,
								barcode,
								cart_type,
								density,
								global_data.strict_drive);

	return ret;
}

bool filedebug_is_readonly(void *device)
{
	int ret;
	struct filedebug_data *state = (struct filedebug_data *)device;

	ret = ibm_tape_is_mountable( state->drive_type,
								NULL,
								state->conf.cart_type,
								state->conf.density_code,
								global_data.strict_drive);

	if (ret == MEDIUM_READONLY)
		return true;
	else
		return false;
}

int filedebug_get_worm_status(void *device, bool *is_worm)
{
	struct filedebug_data *state = (struct filedebug_data *)device;

	*is_worm = state->is_worm;
	return DEVICE_GOOD;
}

int filedebug_get_serialnumber(void *device, char **result)
{
	struct filedebug_data *state = (struct filedebug_data *)device;

	CHECK_ARG_NULL(device, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(result, -LTFS_NULL_ARG);

	if (state->serial_number)
		*result = strdup((const char *) state->serial_number);
	else
		*result = strdup("DUMMY");

	if (! *result)
		return -EDEV_NO_MEMORY;

	return DEVICE_GOOD;
}

int filedebug_get_info(void *device, struct tc_drive_info *info)
{
	struct filedebug_data *state = (struct filedebug_data *)device;

	memcpy(info, &state->info, sizeof(struct tc_drive_info));

	return 0;
}

int filedebug_set_profiler(void *device, char *work_dir, bool enable)
{
	/* Do nohting: file backend does not support profiler */
	return 0;
}

int filedebug_get_block_in_buffer(void *device, unsigned int *block)
{
	*block = 0;
	return 0;
}

struct tape_ops filedebug_handler = {
	.open                   = filedebug_open,
	.reopen                 = filedebug_reopen,
	.close                  = filedebug_close,
	.close_raw              = filedebug_close_raw,
	.is_connected           = filedebug_is_connected,
	.inquiry                = filedebug_inquiry,
	.inquiry_page           = filedebug_inquiry_page,
	.test_unit_ready        = filedebug_test_unit_ready,
	.read                   = filedebug_read,
	.write                  = filedebug_write,
	.writefm                = filedebug_writefm,
	.rewind                 = filedebug_rewind,
	.locate                 = filedebug_locate,
	.space                  = filedebug_space,
	.erase                  = filedebug_erase,
	.load                   = filedebug_load,
	.unload                 = filedebug_unload,
	.readpos                = filedebug_readpos,
	.setcap                 = filedebug_setcap,
	.format                 = filedebug_format,
	.remaining_capacity     = filedebug_remaining_capacity,
	.logsense               = filedebug_logsense,
	.modesense              = filedebug_modesense,
	.modeselect             = filedebug_modeselect,
	.reserve_unit           = filedebug_reserve_unit,
	.release_unit           = filedebug_release_unit,
	.prevent_medium_removal = filedebug_prevent_medium_removal,
	.allow_medium_removal   = filedebug_allow_medium_removal,
	.write_attribute        = filedebug_write_attribute,
	.read_attribute         = filedebug_read_attribute,
	.allow_overwrite        = filedebug_allow_overwrite,
	.set_compression        = filedebug_set_compression,
	.set_default            = filedebug_set_default,
	.get_cartridge_health   = filedebug_get_cartridge_health,
	.get_tape_alert         = filedebug_get_tape_alert,
	.clear_tape_alert       = filedebug_clear_tape_alert,
	.get_xattr              = filedebug_get_xattr,
	.set_xattr              = filedebug_set_xattr,
	.get_parameters         = filedebug_get_parameters,
	.get_eod_status         = filedebug_get_eod_status,
	.get_device_list        = filedebug_get_device_list,
	.help_message           = filedebug_help_message,
	.parse_opts             = filedebug_parse_opts,
	.default_device_name    = filedebug_default_device_name,
	.set_key                = filedebug_set_key,
	.get_keyalias           = filedebug_get_keyalias,
	.takedump_drive         = filedebug_takedump_drive,
	.is_mountable           = filedebug_is_mountable,
	.get_worm_status        = filedebug_get_worm_status,
	.get_serialnumber       = filedebug_get_serialnumber,
	.get_info               = filedebug_get_info,
	.set_profiler           = filedebug_set_profiler,
	.get_block_in_buffer    = filedebug_get_block_in_buffer,
	.is_readonly            = filedebug_is_readonly,
};

struct tape_ops *tape_dev_get_ops(void)
{
	return &filedebug_handler;
}

#ifndef mingw_PLATFORM
extern char tape_generic_file_dat[];
#endif

const char *tape_dev_get_message_bundle_name(void **message_data)
{
#ifndef mingw_PLATFORM
	*message_data = tape_generic_file_dat;
#else
	*message_data = NULL;
#endif
	return "tape_generic_file";
}
