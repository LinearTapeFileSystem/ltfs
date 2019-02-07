/************************************************************************************
**
**  Hewlett Packard LTFS backend for HP LTO and DAT tape drives
**
** FILE:            ltotape.c
**
** CONTENTS:        Main body of ltotape LTFS backend
**
** (C) Copyright 2015 - 2017 Hewlett Packard Enterprise Development LP
**
** This program is free software; you can redistribute it and/or modify it
**  under the terms of version 2.1 of the GNU Lesser General Public License
**  as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful, but 
**  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
**  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
**  License for more details.
**
** You should have received a copy of the GNU General Public License along
**  with this program; if not, write to:
**    Free Software Foundation, Inc.
**    51 Franklin Street, Fifth Floor
**    Boston, MA 02110-1301, USA.
**
**   26 April 2010
**
*************************************************************************************
**
**  10/13/17 Added support for LTO8 media
**
*************************************************************************************
**
** Copyright (C) 2012 OSR Open Systems Resources, Inc.
** 
************************************************************************************* 
*/

#define __ltotape_c

#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "libltfs/ltfs_fuse_version.h"
#include "ltfsprintf.h"
#include "ltotape.h"
#include "ltotape_compat.h"
#include "ltotape_diag.h"
#include "libltfs/ltfs.h"

#include <fuse.h>
#include <fuse_opt.h>


/*
 * Prototype declarations for functions exposed by the backend :
 */
int ltotape_is_connected(const char *devname);
int ltotape_parse_opts(void *device, void *opt_args);
int ltotape_test_unit_ready (void *device);
int ltotape_inquiry(void *device, struct tc_inq *inq);
int ltotape_read(void *device, char *buf, size_t count, struct tc_position *pos,
		const bool unusual_size);
int ltotape_write(void *device, const char *buf, size_t count,
		struct tc_position *pos);
int ltotape_writefm(void *device, size_t count, struct tc_position *pos, bool immed);
int ltotape_locate(void *device, struct tc_position dest,
		struct tc_position *pos);
int ltotape_space(void *device, size_t count, TC_SPACE_TYPE type,
		struct tc_position *pos);
int ltotape_rewind(void *device, struct tc_position *pos);
int ltotape_erase(void *device, struct tc_position *pos, bool ltotape_erase);
int ltotape_load(void *device, struct tc_position *pos);
int ltotape_unload(void *device, struct tc_position *pos);
int ltotape_readposition (void *device, struct tc_position *pos);
int ltotape_format(void *device, TC_FORMAT_TYPE format);
int ltotape_logsense(void *device, const uint8_t page, unsigned char *buf,
		const size_t size);
int ltotape_remaining_capacity(void *device, struct tc_remaining_cap *cap);
int ltotape_modesense(void *device, const uint8_t page, const TC_MP_PC_TYPE pc,
		const uint8_t subpage, unsigned char *buf, const size_t size);
int ltotape_modeselect(void *device, unsigned char *buf, const size_t size);
int ltotape_reserve_unit(void *device);
int ltotape_release_unit(void *device);
int ltotape_prevent_medium_removal(void *device);
int ltotape_allow_medium_removal(void *device);
int ltotape_read_attribute(void *device, const tape_partition_t part,
		const uint16_t id, unsigned char *buf, const size_t size);
int ltotape_write_attribute(void *device, const tape_partition_t part,
		const unsigned char *buf, const size_t size);
int ltotape_report_density(void *device, struct tc_density_report *rep,
		bool medium);
int ltotape_set_compression(void *device, const bool enable_compression,
		struct tc_position *pos);
int ltotape_set_default(void *device);
int ltotape_get_cartridge_health(void *device,
		struct tc_cartridge_health *cart_health);
int ltotape_get_tape_alert (void *device, uint64_t* taflags);
int ltotape_get_eod_status(void *device, int part);
int ltotape_get_parameters(void *device, struct tc_current_param *drive_param);
int ltotape_update_mam_attr(void *device, TC_FORMAT_TYPE FORMAT, const char *vol_name,
							unsigned int attribute_id, const char *barcode_name, mam_lockval lockbit);
int ltotape_get_worm_status(void *device, bool *is_worm);
int ltotape_get_serialnumber(void *device, char **result);
int ltotape_set_profiler(void *device, char *work_dir, bool enable);
int ltotape_get_block_in_buffer(void *device, uint32_t *block);
int ltotape_is_readonly(void *device);
void ltotape_help_message(void);

/*
 * Platform-specific implementations from another source file:
 */
extern int ltotape_scsiexec (ltotape_scsi_io_type *scsi_io);
extern int ltotape_open(const char *devname, void **handle);
extern int ltotape_reopen(const char *devname, void *handle);
extern int ltotape_close(void *device);
extern int ltotape_close_raw(void *device);
extern const char *ltotape_default_device;

/*
 * Not part of the backend interface but used by the platform-specific functions:
 */
int ltotape_evpd_inquiry(void *device, int vpdpage, unsigned char* idata, int ilen);

/*
 * The following are "internal" private functions only
 */
static int ltotape_loadunload(void *device, int do_load,
		struct tc_position *pos);
static int ltotape_prevent_allow_medium_removal(void *device, int prevent);
static int _cdb_read(void *device, char *buf, size_t count, bool silion);
static int _cdb_write(void *device, const char *buf, size_t count);
static int parse_logPage(const unsigned char *logdata, const uint16_t param,
		int *param_size, unsigned char *buf, const size_t bufsize);
static int null_parser(void *priv, const char *arg, int key,
		struct fuse_args *outargs);
static int ltotape_set_MAMattributes (void* device, TC_FORMAT_TYPE format, const char *vol_name,
									  unsigned int attribute_id, const char *barcode_name,
									  mam_lockval lockbit, const char *vol_mam_uuid);

/*
 * Declare a (static) array to maintain volume statistics.  Used below in
 *  ltotape_get_cartridge_health().
 */
static uint16_t volstats[] = {
		VOLSTATS_MOUNTS,
		VOLSTATS_WRITTEN_DS,
		VOLSTATS_WRITE_TEMPS,
		VOLSTATS_WRITE_PERMS,
		VOLSTATS_READ_DS,
		VOLSTATS_READ_TEMPS,
		VOLSTATS_READ_PERMS,
		VOLSTATS_WRITE_PERMS_PREV,
		VOLSTATS_READ_PERMS_PREV,
		VOLSTATS_WRITE_MB,
		VOLSTATS_READ_MB,
		VOLSTATS_PASSES_BEGIN,
		VOLSTATS_PASSES_MIDDLE,
};

/*
 * Declare an array of ltotape-specific options for the FUSE option parser:
 */
static struct fuse_opt ltotape_opts[] = {
		{ "log_directory=%s",  offsetof(ltotape_scsi_io_type, logdir), 0 },
		{ "nosizelimit",       offsetof(ltotape_scsi_io_type, unlimited_blocksize), 1 },
		FUSE_OPT_END
};

/**
 * Parse log page contents.
 * @param logdata Pointer to the log buffer to parse
 * @param param Parameter id to extract
 * @param param_size Size of value to extract
 * @param buf Pointer to the buffer to receive the extracted value
 * @param bufsize Size of the buffer
 * @return 0 on success, -1 if param not found, -2 if found but too big to fit
 * in buffer
 */
static int parse_logPage(const unsigned char *logdata, const uint16_t param,
		int *param_size, unsigned char *buf, const size_t bufsize)
{
	uint16_t page_len = 0, param_code = 0, param_len = 0;
	long i = 0;

	page_len = ((uint16_t) logdata[2] << 8) + (uint16_t) logdata[3];
	i = LOG_PAGE_HEADER_SIZE;

	while (i < (long) page_len) {
		param_code = ((uint16_t) logdata[i] << 8) + (uint16_t) logdata[i + 1];
		param_len = (uint16_t) logdata[i + LOG_PAGE_PARAMSIZE_OFFSET];
		if (param_code == param) {
			*param_size = param_len;
			if (bufsize < param_len) {
				ltfsmsg(LTFS_ERR, 20036E, bufsize, i + LOG_PAGE_PARAM_OFFSET);
				memcpy(buf, &logdata[i + LOG_PAGE_PARAM_OFFSET], bufsize);
				return -2;
			} else {
				memcpy(buf, &logdata[i + LOG_PAGE_PARAM_OFFSET], param_len);
				return 0;
			}
		}
		i += param_len + LOG_PAGE_PARAM_OFFSET;
	}

	return -1;
}

/**
 * A null parser (for the fuse parser to reference as needed)
 * @param priv
 * @param arg
 * @param key
 * @param outargs
 * @return
 */
static int null_parser(void *priv, const char *arg, int key,
		struct fuse_args *outargs)
{
	return 1;
}

/**
 * Returns if a given device with name `devname' is connected to the host or not
 *
 * @param devname The name of the device (For example: /dev/sg0)
 * @return int 0 if a device with that name is connected else a negative value
 * upon an error.
 */
int ltotape_is_connected(const char *devname)
{
	struct stat statbuf;

	/*
	 * We assume that /dev is handled by a daemon such as Udev and that
	 * device entries are automatically removed and added upon hotplug events.
	 */
	return stat(devname, &statbuf);
}

/**
 * This function parses the arguments supplied to the `ltotape' backend.
 *
 * @param device The device that's mounted.
 * @param opt_args The arguments supplied by the user to the backend.
 * @return int DEVICE_GOOD on success, a -ve value on error.
 */
int ltotape_parse_opts(void *device, void *opt_args)
{
	int					ret = DEVICE_GOOD;
	struct fuse_args	*args = (struct fuse_args *) opt_args;
	struct stat 		statbuf;

	CHECK_ARG_NULL(device, -EDEV_INVALID_ARG);

	/* Initialize to our default place */
	((ltotape_scsi_io_type*)device)->logdir = ltotape_get_default_snapshotdir();

	/* By default we WILL limit blocksize (see ltotape_get_params) */
	((ltotape_scsi_io_type*)device)->unlimited_blocksize = 0;

	ret = fuse_opt_parse(args, device, ltotape_opts, null_parser);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 20037E, ret);
		return ret;
	}

	/* Check for a valid log-directory path (if set through fuse-parse options */
	ret = stat(((ltotape_scsi_io_type*)device)->logdir, &statbuf);
	if (ret < 0 || ! S_ISDIR(statbuf.st_mode)) {
		/* Invalid log-directory path, setting back to default log-directory */
		ltfsmsg(LTFS_WARN, 20104W, ((ltotape_scsi_io_type*)device)->logdir);
		((ltotape_scsi_io_type*)device)->logdir = ltotape_get_default_snapshotdir();
		ret = 0;
	}

	return ret;
}

/**
 * Test Unit Ready
 * @param device A pointer to the ltotape backend.
 * @return 0 on success or negative value on error.
 */
int ltotape_test_unit_ready (void *device)
{
	int retval;
	ltotape_scsi_io_type *sio = (ltotape_scsi_io_type*)device;

	/* Set up the cdb for TUR: */
	sio->cdb[0] = CMDtest_unit_ready;
	sio->cdb[1] = 0;
	sio->cdb[2] = 0;
	sio->cdb[3] = 0;
	sio->cdb[4] = 0;
	sio->cdb[5] = 0;

	sio->cdb_length = 6;		/* six-byte cdb */

	/* Set up the data part: */
	sio->data = (unsigned char *) NULL;
	sio->data_length = 0;
	sio->data_direction = NO_TRANSFER;

	/* Set the timeout then execute: */
	sio->timeout_ms = (sio->family == drivefamily_lto) ?
			LTO_TESTUNITREADY_TIMEOUT : DAT_TESTUNITREADY_TIMEOUT;
	
	/* If it failed, and the sense data implies no medium present, adjust return value accordingly: */
	retval = ltotape_scsiexec(sio);
	if ((retval == -1) && (sio->sense_length > 0) && (SENSE_IS_NO_MEDIA(sio->sensedata))) {
		retval = -EDEV_NO_MEDIUM;
	}

	return retval;
}

/**
 * Get inquiry data.
 * @param device A pointer to the ltotape backend.
 * @param inq Pointer to the inquiry data. This function will update this value.
 * @return 0 on success or negative value on error.
 */
int ltotape_inquiry(void *device, struct tc_inq *inq)
{
	unsigned char			inqbuffer[240];
	int						status;
	ltotape_scsi_io_type	*sio = (ltotape_scsi_io_type*)device;

	memset(inqbuffer, 0, sizeof(inqbuffer));

	/* Set up the cdb for Inquiry: */
	sio->cdb[0] = CMDinquiry;
	sio->cdb[1] = 0;
	sio->cdb[2] = 0;
	sio->cdb[3] = 0;
	sio->cdb[4] = (unsigned char) sizeof(inqbuffer);
	sio->cdb[5] = 0;

	sio->cdb_length = 6;		/* six-byte cdb */

	/* Set up the data part: */
	sio->data = inqbuffer;
	sio->data_length = sizeof(inqbuffer);
	sio->data_direction = HOST_READ;

	/* Set the timeout then execute: */
	sio->timeout_ms = (sio->family == drivefamily_lto) ?
			LTO_INQUIRY_TIMEOUT : DAT_INQUIRY_TIMEOUT;
	status = ltotape_scsiexec (sio);

	if (status == 0) {
		inq->devicetype =  inqbuffer[0] & 0x1F;
		inq->cmdque     = (inqbuffer[7] & 0x02) >> 1;

		strncpy ((char *) inq->vid, (char *) inqbuffer+8, 8);
		inq->vid[8] = '\0';

		strncpy((char *) inq->pid, (char *) inqbuffer+16, 16);
		inq->pid[16] = '\0';

		strncpy((char *) inq->revision, (char *) inqbuffer+32, 4);
		inq->revision[4] = '\0';

		strncpy((char *) inq->vendor, (char *) inqbuffer+36, 20);
		inq->vendor[20] = '\0';
	}

	return status;
}

/**
 * Request a specific inquiry page.
 *
 * @param device The device that's mounted.
 * @param page The page code.
 * @param inq The inquiry data.
 * @return int DEVICE_GOOD on success, a -ve error on failure.
 */
int ltotape_inquiry_page(void *device, unsigned char page,
		struct tc_inq_page *inq)
{
	int						rc = DEVICE_GOOD;
	ltotape_scsi_io_type	*sio = (ltotape_scsi_io_type *) device;

	CHECK_ARG_NULL(sio, -EDEV_INVALID_ARG);

	return rc;
}

/**
 * Get VPD inquiry page data.
 * @param device A pointer to the ltotape backend.
 * @param vpdpage The page to be fetched.
 * @param idata Pointer to the inquiry data to be filled by this function.
 * @param ilen Size of data storage available at idata.
 * @return 0 on success or negative value on error.
 */
int ltotape_evpd_inquiry(void *device, int vpdpage, unsigned char* idata,
		int ilen)
{
	int						status = 0;
	ltotape_scsi_io_type	*sio = (ltotape_scsi_io_type*)device;

	memset(idata, 0, ilen);

	/* Set up the cdb for Inquiry: */
	sio->cdb[0] = CMDinquiry;
	sio->cdb[1] = 0x01;           /* set EVPD bit */
	sio->cdb[2] = (unsigned char)vpdpage;
	sio->cdb[3] = (unsigned char)(ilen >> 8);
	sio->cdb[4] = (unsigned char)(ilen & 0xFF);
	sio->cdb[5] = 0;

	sio->cdb_length = 6;		/* six-byte cdb */

	/* Set up the data part: */
	sio->data = idata;
	sio->data_length = ilen;
	sio->data_direction = HOST_READ;

	/* Set the timeout then execute: */
	sio->timeout_ms = (sio->family == drivefamily_lto) ?
			LTO_INQUIRY_TIMEOUT : DAT_INQUIRY_TIMEOUT;
	status = ltotape_scsiexec (sio);
	return status;
}

/**
 * Internal function to perform a SCSI read command.
 * @param device A pointer to the ltotape backend.
 * @param buf Buffer to hold the data read.
 * @param count Amount of data to be read.
 * @param silion Flag set to true if the requested count is greated than the
 * block size.
 * @return The numbe of bites read on success, a negative value on error. If a
 * file mark is detected, the function returns 0 and positions the tape after
 * the file mark.
 */
static int _cdb_read(void *device, char *buf, size_t count, bool silion)
{
	int						status = 0;
	ltotape_scsi_io_type	*sio = (ltotape_scsi_io_type*)device;

	/* Set up the cdb: */
	sio->cdb[0] = CMDread;
	sio->cdb[1] = (silion) ? 0x02 : 0x00;
	sio->cdb[2] = (unsigned char) (count >> 16 );
	sio->cdb[3] = (unsigned char) (count >>  8 );
	sio->cdb[4] = (unsigned char) (count & 0xFF);
	sio->cdb[5] = 0;

	sio->cdb_length = 6;		/* six-byte cdb */

	/* Set up the data part: */
	sio->data = (unsigned char*)buf;
	sio->data_length = count;
	sio->data_direction = HOST_READ;

	/* Set the timeout then execute:  */
	sio->timeout_ms = (sio->family == drivefamily_lto) ?
			LTO_READ_TIMEOUT : DAT_READ_TIMEOUT;
	status = ltotape_scsiexec (sio);

	/* If we failed, check for a few specific conditions and possibly alter
	 * the outcome: */
	if (status == -1) {
		if (SENSE_IS_FILEMARK_DETECTED(sio->sensedata)) {
			ltfsmsg(LTFS_DEBUG, 20038D);
			status = 0;

		} else {
			errno = EIO;
		}
	}

	return status;
}

/**------------------------------------------------------------------------**
 * Internal function to perform a SCSI Write command
 *  Used by ltotape_write() below
 */
/**
 * Internal functon to perform a SCSI write command.
 * @param device A pointer to the ltotape backend.
 * @param buf The data to be written.
 * @param count Amount of data to be written.
 * @return 0 on success, a negative value on error.
 */
static int _cdb_write(void *device, const char *buf, size_t count)
{
	int						status = 0;
	ltotape_scsi_io_type	*sio = (ltotape_scsi_io_type*)device;

	/* Set up the cdb: */
	sio->cdb[0] = CMDwrite;
	sio->cdb[1] = 0;
	sio->cdb[2] = (unsigned char) (count >> 16 );
	sio->cdb[3] = (unsigned char) (count >>  8 );
	sio->cdb[4] = (unsigned char) (count & 0xFF);
	sio->cdb[5] = 0;

	sio->cdb_length = 6;		/* six-byte cdb */

	/* Set up the data part: */
	sio->data = (unsigned char*)buf;
	sio->data_length = count;
	sio->data_direction = HOST_WRITE;

	/* Set the timeout then execute: */
	sio->timeout_ms = (sio->family == drivefamily_lto) ?
			LTO_WRITE_TIMEOUT : DAT_WRITE_TIMEOUT;
	status = ltotape_scsiexec(sio);

	return status;
}

/**------------------------------------------------------------------------**
 * Read a record from tape
 * @param device a pointer to the ltotape backend
 * @param buf a pointer to read buffer
 * @param count read size
 * @param pos a pointer to position data. This function will update position infomation.
 * @param unusual_size a flag specified unusual size or not
 * @return read length on success or a negative value on error
 */
/**
 * Read a block of data from the given device of at most 'count' amount of data
 * @param device A pointer to the ltotape backend.
 * @param buf Buffer holding the data read.
 * @param count Amount of data to be read.
 * @param pos A pointer to the position data. After a successful write, the
 * position data is updated.
 * @param unusual_size True if the specified size is unusual with respect to the
 * block size set for the device.
 * @return Read length on success or a negative value on error. 0 if a file mark
 * was encountered.
 */
int ltotape_read(void *device, char *buf, size_t count, struct tc_position *pos, const bool unusual_size)
{
	int rc = 0;

	ltfsmsg(LTFS_DEBUG, 20039D, "read", count);

	rc = _cdb_read(device, buf, count, unusual_size);
	if (rc < 0) {
		rc = (errno == 0) ? -EIO : -errno; // Force an errorcode if none is set..
		switch (rc) {
				// General errors
			case -EBUSY:
				ltfsmsg(LTFS_ERR, 20040E, "read");
				break;
			case -EFAULT:
				ltfsmsg(LTFS_ERR, 20041E, "read");
				ltotape_log_snapshot (device, FALSE);
				break;
			case -EIO:
				ltfsmsg(LTFS_ERR, 20042E, "read");
				ltotape_log_snapshot (device, FALSE);
				break;
			case -ENOMEM:
				ltfsmsg(LTFS_ERR, 20043E, "read");
				break;
			case -ENXIO:
				ltfsmsg(LTFS_ERR, 20044E, "read");
				break;
			case -EPERM:
				ltfsmsg(LTFS_ERR, 20045E, "read");
				ltotape_log_snapshot (device, FALSE);
				break;
			case -ETIMEDOUT:
				ltfsmsg(LTFS_ERR, 20046E, "read");
				ltotape_log_snapshot (device, FALSE);
				break;
				// read specific errors
			case -EINVAL:
				ltfsmsg(LTFS_ERR, 20047E, "read");
				ltotape_log_snapshot (device, FALSE);
				break;
			case -EAGAIN:
				ltfsmsg(LTFS_ERR, 20055E, "read");
				ltotape_log_snapshot (device, FALSE);
				break;
			default:
				ltfsmsg(LTFS_ERR, 20054E, "read", -rc);
				break;
		}
	}
	else {
		pos->block++;
	}

	return rc;
}

/**
 * Write a record to tape
 *
 * @param device a pointer to the ltotape backend
 * @param buf a pointer to read buffer
 * @param count read size
 * @param pos a pointer to position data. This function will update position infomation.
 * @param unusual_size a flag specified unusual size or not
 * @return struct .rc = 0 for success, negative value else; .early_warning set as appropriate
 */
int ltotape_write(void *device, const char *buf, size_t count, struct tc_position *pos)
{
	int						rc = 0;
	ltotape_scsi_io_type	*sio = (ltotape_scsi_io_type*)device;

	ltfsmsg(LTFS_DEBUG, 20039D, "write", count);

	rc = _cdb_write (device, buf, count);

	if (rc < 0) {
		rc = (errno == 0) ? -EIO : -errno; // Force an errorcode if none is set..

		switch (rc) {
				// General errors
			case -EBUSY:
				ltfsmsg(LTFS_ERR, 20040E, "write");
				ltotape_log_snapshot (device, FALSE);
				break;
			case -EFAULT:
				ltfsmsg(LTFS_ERR, 20041E, "write");
				ltotape_log_snapshot (device, FALSE);
				break;
			case -EIO:
				ltfsmsg(LTFS_ERR, 20042E, "write");
				ltotape_log_snapshot (device, FALSE);
				break;
			case -ENOMEM:
				ltfsmsg(LTFS_ERR, 20043E, "write");
				break;
			case -ENXIO:
				ltfsmsg(LTFS_ERR, 20044E, "write");
				break;
			case -EPERM:
				ltfsmsg(LTFS_ERR, 20045E, "write");
				break;
			case -ETIMEDOUT:
				ltfsmsg(LTFS_ERR, 20046E, "write");
				ltotape_log_snapshot (device, FALSE);
				break;
				// write specific errors
			case -EINVAL:
				ltfsmsg(LTFS_ERR, 20047E, "write");
				ltotape_log_snapshot (device, FALSE);
				break;
			case -ENOSPC:
				ltfsmsg(LTFS_WARN, 20048W, "write");
				pos->early_warning = true;
				break;
			default:
				ltfsmsg(LTFS_ERR, 20054E, "write", -rc);
				break;
		}

	} else {
		pos->block++;
		/*
		 * If we have just reached the EWEOM point, we need to report it now.
		 *  We also modify the flag to indicate that we have reported it and
		 *  are now writing "in the zone"..
		 */
		if (sio->eweomstate == report_eweom) {
			ltfsmsg(LTFS_WARN, 20048W, "write");
			pos->early_warning = true;
			sio->eweomstate = after_eweom;
		}
	}

	return rc;
}

/**
 * Write filemark(s) to tape
 *
 * @param device a pointer to the ltotape backend
 * @param count count to write filemark. If 0 only flush.
 * @param pos a pointer to position data. This function will update position
 * infomation.
 * @return 0 on success or a negative value on error
 */
int ltotape_writefm(void *device, size_t count, struct tc_position *pos, bool immed)
{
	int						rc = 0;
	ltotape_scsi_io_type 	*sio = (ltotape_scsi_io_type*)device;

	ltfsmsg(LTFS_DEBUG, 20056D, "write file marks", count);

	
	/* HPE MD 24/11/2017 Have seen issues with an index overwriting the vol label at BOP 
	   The following read position is to try and avoid that happening. */
	   
	rc = ltotape_readposition (device, pos);   
	
	if (rc < 0)
	{
	   return rc;
	}
	else if (pos->block == 0 && pos->filemarks == 0)
	{
	   ltfsmsg(LTFS_ERR, 20105E);
	   return -LTFS_POS_SUSPECT_BOP;
	}   
	
	/* Set up the cdb: */
	sio->cdb[0] = CMDwrite_filemarks;
	sio->cdb[1] = (count == 0) ? 0:1;
	sio->cdb[2] = (unsigned char) ((count & 0xFF0000) >> 16);
	sio->cdb[3] = (unsigned char) ((count & 0xFF00)    >> 8);
	sio->cdb[4] = (unsigned char)  (count & 0xFF           );
	sio->cdb[5] = (unsigned char) 0;

	sio->cdb_length = 6;		/* six-byte cdb */

	/* Set up the data part: */
	sio->data = (unsigned char *) NULL;
	sio->data_length = 0;
	sio->data_direction = NO_TRANSFER;

	/* Set the timeout then execute: */
	sio->timeout_ms = (sio->family == drivefamily_lto) ?
			LTO_WRITEFILEMARK_TIMEOUT : DAT_WRITEFILEMARK_TIMEOUT;
	rc = ltotape_scsiexec (sio);

	/*
	 * Finally try to update the position data:
	 */
	ltotape_readposition (device, pos);

	return rc;
}

/**------------------------------------------------------------------------**
 * Rewind tape
 * @param device a pointer to the ltotape backend
 * @param pos a pointer to position data. This function will update position infomation.
 * @return 0 on success or a negative value on error
 */
int ltotape_rewind(void *device, struct tc_position *pos)
{
	int						status = 0;
	ltotape_scsi_io_type	*sio = (ltotape_scsi_io_type*)device;

	/*
	 * Set up the cdb for rewind:
	 */
	sio->cdb[0] = CMDrewind;
	sio->cdb[1] = 0;
	sio->cdb[2] = 0;
	sio->cdb[3] = 0;
	sio->cdb[4] = 0;
	sio->cdb[5] = 0;
	sio->cdb_length = 6;		/* six-byte cdb */

	/*
	 * Set up the data part:
	 */
	sio->data = (unsigned char *) NULL;
	sio->data_length = 0;
	sio->data_direction = NO_TRANSFER;

	/*
	 * And execute it:
	 */
	sio->timeout_ms = (sio->family == drivefamily_lto) ? LTO_REWIND_TIMEOUT : DAT_REWIND_TIMEOUT;
	status = ltotape_scsiexec (sio);

	/*
	 * Finally try to update the position data:
	 */
	ltotape_readposition (device, pos);

	return status;
}

/**
 * Locate to position on tape
 *
 * @param device a pointer to the ltotape backend
 * @param dest a position data of destination.
 * @param pos a pointer to position data. This function will update position infomation.
 * @return 0 on success or a negative value on error
 */
int ltotape_locate(void *device, struct tc_position dest, struct tc_position *pos)
{
	int						rc = 0;
	int						status = 0;
	ltotape_scsi_io_type	*sio = (ltotape_scsi_io_type*)device;

	ltfsmsg(LTFS_DEBUG, 20057D, "locate", (unsigned long long)dest.partition,
			(unsigned long long)dest.block);

	/*
	 * Set up the cdb:
	 */
	if (sio->family == drivefamily_lto) {
		sio->cdb[0]  = CMDlocate16;
		sio->cdb[1]  = (pos->partition == dest.partition) ? 0x00 : 0x02; /* set CP (Change Partition) if necessary */
		sio->cdb[2]  = 0;
		sio->cdb[3]  = (unsigned char) (dest.partition & 0xFF);
		sio->cdb[4]  = (unsigned char) (dest.block >> 56);
		sio->cdb[5]  = (unsigned char) (dest.block >> 48);
		sio->cdb[6]  = (unsigned char) (dest.block >> 40);
		sio->cdb[7]  = (unsigned char) (dest.block >> 32);
		sio->cdb[8]  = (unsigned char) (dest.block >> 24);
		sio->cdb[9]  = (unsigned char) (dest.block >> 16);
		sio->cdb[10] = (unsigned char) (dest.block >>  8);
		sio->cdb[11] = (unsigned char) (dest.block & 0xFF);
		sio->cdb[12] = 0;
		sio->cdb[13] = 0;
		sio->cdb[14] = 0;
		sio->cdb[15] = 0;

		sio->cdb_length = 16;		/* sixteen-byte cdb */

	} else {  /* not lto, must be dat */
		sio->cdb[0]  = CMDlocate;
		sio->cdb[1]  = (pos->partition == dest.partition) ? 0x00 : 0x02; /* set CP (Change Partition) if necessary */
		sio->cdb[2]  = 0;
		sio->cdb[3]  = (unsigned char) (dest.block >> 24);
		sio->cdb[4]  = (unsigned char) (dest.block >> 16);
		sio->cdb[5]  = (unsigned char) (dest.block >>  8);
		sio->cdb[6]  = (unsigned char) (dest.block & 0xFF);
		sio->cdb[7]  = 0;
		sio->cdb[8]  = (unsigned char) (dest.partition & 0xFF);
		sio->cdb[9]  = 0;

		sio->cdb_length = 10;		/* ten-byte cdb */
	}

	/*
	 * Set up the data part:
	 */
	sio->data = (unsigned char *) NULL;
	sio->data_length = 0;
	sio->data_direction = NO_TRANSFER;

	/*
	 * Set the timeout then execute:
	 */
	sio->timeout_ms = (sio->family == drivefamily_lto) ? LTO_LOCATE_TIMEOUT : DAT_LOCATE_TIMEOUT;
	status = ltotape_scsiexec (sio);

	/*
	 * Handle a couple of unusual "not really an error" circumstances:
	 *  - encountering EOD (BLANK CHECK status) when spacing to 'max block'
	 *  - encountering NoEOD (BLANK CHECK status) when spacing to '0' on virgin media
	 */
	if (status == -1) {
		if ((dest.block == TAPE_BLOCK_MAX) && (SENSE_IS_BLANK_CHECK_EOD(sio->sensedata))) {
			ltfsmsg(LTFS_DEBUG, 20063D);
			status = 0;

		} else if ((dest.block == 0) && (SENSE_IS_BLANK_CHECK_NOEOD(sio->sensedata))) {
			ltfsmsg(LTFS_DEBUG, 20021D);
			status = 0;

		} else {
			ltfsmsg(LTFS_ERR, 20064E, status);
			ltotape_log_snapshot (device, FALSE);
		}
	}

	ltotape_readposition (device, pos);

	rc = status;
	return rc;
}

/**
 * Space to position on tape
 *
 * @param device a pointer to the ltotape backend
 * @param count specify record or fm count to move
 * @param type specify type of move
 * @param pos a pointer to position data. This function will update position
 * infomation.
 * @return 0 on success or a negative value on error
 */
int ltotape_space(void *device, size_t count, TC_SPACE_TYPE type, struct tc_position *pos)
{
	int						rc = 0;
	int						status = 0;
	int						spacecount = 0;
	ltotape_scsi_io_type	*sio = (ltotape_scsi_io_type*)device;

	/*
	 * Set up the cdb:
	 */
	sio->cdb[0] = CMDspace;
	sio->cdb[5] = 0;

	sio->cdb_length = 6;		/* six-byte cdb */

	spacecount = (int) count;

	switch (type) {
	case TC_SPACE_EOD:
		ltfsmsg(LTFS_DEBUG, 20058D, "Space to EOD");
		sio->cdb[1] = 0x03;
		break;
	case TC_SPACE_FM_F:
		ltfsmsg(LTFS_DEBUG, 20059D, "space forward file marks", (unsigned long long)count);
		sio->cdb[1] = 0x01;
		break;
	case TC_SPACE_FM_B:
		ltfsmsg(LTFS_DEBUG, 20059D, "space back file marks", (unsigned long long)count);
		spacecount = -spacecount;
		sio->cdb[1] = 0x01;
		break;
	case TC_SPACE_F:
		ltfsmsg(LTFS_DEBUG, 20059D, "space forward records", (unsigned long long)count);
		sio->cdb[1] = 0x0;
		break;
	case TC_SPACE_B:
		ltfsmsg(LTFS_DEBUG, 20059D, "space back records", (unsigned long long)count);
		spacecount = -spacecount;
		sio->cdb[1] = 0x0;
		break;
	default:
		ltfsmsg(LTFS_ERR, 20065E, type);	/* unexpected space type */
		rc = -EINVAL;
		return rc;
	}

	sio->cdb[2] = (unsigned char) (spacecount >> 16);
	sio->cdb[3] = (unsigned char) (spacecount >>  8);
	sio->cdb[4] = (unsigned char) (spacecount & 0xFF);

	/*
	 * Set up the data part:
	 */
	sio->data = (unsigned char *) NULL;
	sio->data_length = 0;
	sio->data_direction = NO_TRANSFER;

	/*
	 * Set the timeout then execute:
	 */
	sio->timeout_ms = (sio->family == drivefamily_lto) ? LTO_SPACE_TIMEOUT : DAT_SPACE_TIMEOUT;
	status = ltotape_scsiexec (sio);

	/*
	 * Finally try to update the position data:
	 */
	ltotape_readposition (device, pos);

	rc = status;
	return rc;
}

/**
 * Erase tape from current position
 *
 * @param device a pointer to the ltotape backend
 * @param pos a pointer to position data. This function will update position infomation.
 * @return 0 on success or a negative value on error
 */
int ltotape_erase(void *device, struct tc_position *pos, bool long_erase)
{
	int						status = 0;
	ltotape_scsi_io_type	*sio = (ltotape_scsi_io_type*)device;

	/*
	 * Set up the cdb:
	 */
	sio->cdb[0] = CMDerase;
	sio->cdb[1] = 0; // SHORT erase
	sio->cdb[2] = 0;
	sio->cdb[3] = 0;
	sio->cdb[4] = 0;
	sio->cdb[5] = 0;

	sio->cdb_length = 6;		/* six-byte cdb */

	/*
	 * Set up the data part:
	 */
	sio->data = (unsigned char *) NULL;
	sio->data_length = 0;
	sio->data_direction = NO_TRANSFER;

	/*
	 * Set the timeout then execute:
	 */
	sio->timeout_ms = (sio->family == drivefamily_lto) ? LTO_ERASE_TIMEOUT : DAT_ERASE_TIMEOUT;
	status = ltotape_scsiexec (sio);

	/*
	 * Finally try to update the position data:
	 */
	ltotape_readposition (device, pos);

	return status;
}

/**
 * Load or unload tape
 *
 * @param device a pointer to the ltotape backend
 * @param do_load specifies whether to load (!=0) or unload (==0)
 * @param pos a pointer to position data. This function will update position infomation.
 * @return 0 on success or a negative value on error
 */
static int ltotape_loadunload(void *device, int do_load, struct tc_position *pos)
{
	ltotape_scsi_io_type *sio = (ltotape_scsi_io_type*)device;

	/*
	 * Set up the cdb:
	 */
	sio->cdb[0] = CMDload;		/* also does unloads! */
	sio->cdb[1] = 0;
	sio->cdb[2] = 0;
	sio->cdb[3] = 0;
	sio->cdb[4] = (do_load) ? 1 : 0;
	sio->cdb[5] = 0;

	sio->cdb_length = 6;		/* six-byte cdb */

	/*
	 * Set up the data part:
	 */
	sio->data = (unsigned char *) NULL;
	sio->data_length = 0;
	sio->data_direction = NO_TRANSFER;

	/*
	 * Set the timeout then execute:
	 */
	if (do_load) {
		sio->timeout_ms = (sio->family == drivefamily_lto) ? LTO_LOAD_TIMEOUT : DAT_LOAD_TIMEOUT;
	} else {
		sio->timeout_ms = (sio->family == drivefamily_lto) ? LTO_UNLOAD_TIMEOUT : DAT_UNLOAD_TIMEOUT;
	}
	return (ltotape_scsiexec (sio));
}

/**------------------------------------------------------------------------**
 * Load tape or rewind when a tape is already loaded
 * @param device a pointer to the ltotape backend
 * @param pos a pointer to position data. This function will update position infomation.
 * @return 0 on success or a negative value on error
 */
int ltotape_load(void *device, struct tc_position *pos)
{
   int           status;
   unsigned char buf [64];
   int           mediatype;
   const char*   pMediaName = "";
   ltotape_scsi_io_type *sio = (ltotape_scsi_io_type*)device;

   /*
    * OSR
    *
    * We need a quick way to determine if there isn't a tape in the
    * drive to avoid long wait times when navigating an empty drive
    * in Explorer. We do that by sending a readposition and
      checking for a no media error, which is pretty quck
   */
#ifdef HPE_mingw_BUILD
   int           read_pos_status;
   /* Read the position */
   read_pos_status = ltotape_readposition (device, pos);
   
   /* Check for ENOMEDIUM, in which case we'll get out! */
   if (read_pos_status == -ENOMEDIUM) 
       return read_pos_status;
#endif   

   status = ltotape_loadunload (device, 1, pos);

   ltotape_readposition (device, pos);

   if (status < 0) {
      return status;

/*
 * All DAT media supports partitioning so don't need to do the next check..
 */
   } else if (sio->family == drivefamily_dat) {
      return status;

   } else {
      status = ltotape_modesense (device, MODE_PAGE_MEDIUM_CONFIGURATION, TC_MP_PC_CURRENT, 0x00, buf, sizeof(buf));
      if (status < 0) {
         return status;

      } else {
         /* media type comprises density code from the block descriptor + WORMM bit from mode data */
         mediatype = (int)buf[8] + ((int)(buf[18] & 0x01) << 8);
         switch (mediatype) 
         {
            case LTOMEDIATYPE_LTO8RW    : pMediaName = "LTO8RW";    status = 0;  break;
            case LTOMEDIATYPE_LTO8WORM  : pMediaName = "LTO8WORM";  status = -1; break;
            case LTOMEDIATYPE_LTO8TYPEM : pMediaName = "LTO8TYPEM"; status = 0;  break;   
            case LTOMEDIATYPE_LTO7RW    : pMediaName = "LTO7RW";    status = 0;  break;
            case LTOMEDIATYPE_LTO7WORM  : pMediaName = "LTO7WORM";  status = -1; break;
	         case LTOMEDIATYPE_LTO6RW    : pMediaName = "LTO6RW";    status = 0;  break; 
	         case LTOMEDIATYPE_LTO6WORM  : pMediaName = "LTO6WORM";  status = -1; break;
	         case LTOMEDIATYPE_LTO5RW    : pMediaName = "LTO5RW";    status = 0;  break; 
	         case LTOMEDIATYPE_LTO5WORM  : pMediaName = "LTO5WORM";  status = -1; break;
	         case LTOMEDIATYPE_LTO4RW    : pMediaName = "LTO4RW";    status = -1; break;
	         case LTOMEDIATYPE_LTO4WORM  : pMediaName = "LTO4WORM";  status = -1; break;
	         case LTOMEDIATYPE_LTO3RW    : pMediaName = "LTO3RW";    status = -1; break;
	         case LTOMEDIATYPE_LTO3WORM  : pMediaName = "LTO3WORM";  status = -1; break;
	         default:                      pMediaName = "Unknown";   status = -1; break;
	      }
         if (status < 0) {
            ltfsmsg(LTFS_ERR, 20062E, pMediaName);
            return -LTFS_UNSUPPORTED_MEDIUM;
         }
         return status;
      }
   }
}

/**------------------------------------------------------------------------**
 * Unload tape
 * @param device a pointer to the ltotape backend
 * @param pos a pointer to position data. This function will update position infomation.
 * @return 0 on success or a negative value on error
 */
int ltotape_unload(void *device, struct tc_position *pos)
{
   int status = ltotape_loadunload (device, 0, pos);

   ltotape_readposition (device, pos);

   return status;
}

/**------------------------------------------------------------------------**
 * Tell a current position
 * @param device a pointer to the ltotape backend
 * @param pos a pointer to position data. This function will update position infomation.
 * @return 0 on success or a negative value on error
 */
int ltotape_readposition (void *device, struct tc_position *pos)
{
  ltotape_scsi_io_type *sio = (ltotape_scsi_io_type*)device;
  unsigned char        buf[32];
  int                  status;

  memset(buf, 0, sizeof(buf));

/*
 * Set up the cdb:
 */
  sio->cdb[0] = CMDread_position;
  sio->cdb[1] = 0x06;            /* Service Action 0x06: Long form */
  sio->cdb[2] = 0;
  sio->cdb[3] = 0;
  sio->cdb[4] = 0;
  sio->cdb[5] = 0;
  sio->cdb[6] = 0;
  sio->cdb[7] = 0;
  sio->cdb[8] = 0;
  sio->cdb[9] = 0;

  sio->cdb_length = 10;		/* ten-byte cdb */

/*
 * Set up the data part:
 */
  sio->data = buf;
  sio->data_length = sizeof(buf);
  sio->data_direction = HOST_READ;

/*
 * Set the timeout then execute:
 */
  sio->timeout_ms = (sio->family == drivefamily_lto) ? LTO_READPOSITION_TIMEOUT : DAT_READPOSITION_TIMEOUT;
  status = ltotape_scsiexec (sio);

  if (status == 0) {
     pos->partition = ((tape_partition_t) buf[4] << 24) + ((tape_partition_t) buf[5] << 16) +
		      ((tape_partition_t) buf[6] <<  8) +  (tape_partition_t) buf[7];

     pos->block =     ((tape_block_t) buf[8]  << 56)    + ((tape_block_t) buf[9]  << 48) +
                      ((tape_block_t) buf[10] << 40)    + ((tape_block_t) buf[11] << 32) +
		      ((tape_block_t) buf[12] << 24)    + ((tape_block_t) buf[13] << 16) + 
                      ((tape_block_t) buf[14] <<  8)    +  (tape_block_t) buf[15];

     pos->filemarks = ((tape_block_t) buf[16] << 56)    + ((tape_block_t) buf[17] << 48) +
                      ((tape_block_t) buf[18] << 40)    + ((tape_block_t) buf[19] << 32) +
                      ((tape_block_t) buf[20] << 24)    + ((tape_block_t) buf[21] << 16) +
                      ((tape_block_t) buf[22] <<  8)    +  (tape_block_t) buf[23];
		
     ltfsmsg(LTFS_DEBUG, 20060D, 
	     (unsigned long long)pos->partition, (unsigned long long)pos->block, (unsigned long long)pos->filemarks);
  
  } else {
     if (SENSE_IS_NO_MEDIA(sio->sensedata)) {
#ifdef linux
	 status = -ENOMEDIUM;
#else
	 status = -EAGAIN;
#endif

     } else {   
            ltfsmsg(LTFS_ERR, 20066E, status);
	         ltotape_log_snapshot (device, FALSE);
     }
  }  
  
  return (status);
}

/**
 * Set the capacity proprotion of the medium
 *
 * @param device A pointer to the ltotape backend.
 * @param proportion Specify the proportion.
 * @return int DEVICE_GOOD on success, a -ve value on error.
 */
int ltotape_setcap(void *device, uint16_t proportion)
{
	int						rc = DEVICE_GOOD;
	ltotape_scsi_io_type	*sio = (ltotape_scsi_io_type *) device;

	CHECK_ARG_NULL(sio, -EDEV_INVALID_ARG);

	return rc;
}

/**
 * Create or destroy partition on the tape.
 * @param device Pointer to the ltotape backend.
 * @param format The type of format to be performed.
 * @param vol_name If LTFS format then an optional volume name.
 * @param barcode_name If LTFS format then an optional barcode name.
 * @return 0 on success, a negative value on error.
 */
int ltotape_format(void *device, TC_FORMAT_TYPE format)
{
	int						status = 0;
	ltotape_scsi_io_type	*sio = (ltotape_scsi_io_type*)device;

	if ((unsigned char) format >= (unsigned char) TC_FORMAT_MAX) {
		ltfsmsg(LTFS_ERR, 20067E, format);
		return -1;
	}

	/* For DAT drives, the partition will have been created during the Mode Select
	 * because they don't support FORMAT MEDIUM.  Therefore return immediately. */
	if (sio->family == drivefamily_dat) {
		return 0;
	}

	/* Set up the cdb: */
	sio->cdb[0] = CMDformat;
	sio->cdb[1] = 0;
	sio->cdb[2] = (unsigned char)format;
	sio->cdb[3] = 0;
	sio->cdb[4] = 0;
	sio->cdb[5] = 0;

	sio->cdb_length = 6;		/* six-byte cdb */

	/* Set up the data part: */
	sio->data = (unsigned char *) NULL;
	sio->data_length = 0;
	sio->data_direction = NO_TRANSFER;

	/* Set the timeout then execute: */
	sio->timeout_ms = LTO_FORMAT_TIMEOUT;
	status = ltotape_scsiexec (sio);

	/* If it failed, take a log snapshot: */
	if (status == -1) {
		ltfsmsg(LTFS_ERR, 20068E, status);
		ltotape_log_snapshot (device, FALSE);
	}

	return status;
}

/**------------------------------------------------------------------------**
 * Retrieve log data from the drive
 * @param device a pointer to the ltotape backend
 * @param page page code of log sense
 * @param buf pointer to buffer to store log data
 * @param size length of the buffer
 * @return 0 on success or a negative value on error
 */
int ltotape_logsense(void *device, const uint8_t page, unsigned char *buf, const size_t size)
{
  ltotape_scsi_io_type *sio = (ltotape_scsi_io_type*)device;
  int          status;

  ltfsmsg(LTFS_DEBUG, 20061D, "logsense", page);

/*
 * Set up the cdb:
 */
  sio->cdb[0] = CMDlog_sense;
  sio->cdb[1] = 0;
  sio->cdb[2] = (unsigned char) (0x40 | (page & 0x3F)); /* set PC=01b for current values */
  sio->cdb[3] = 0;
  sio->cdb[4] = 0;
  sio->cdb[5] = 0;
  sio->cdb[6] = 0;
  sio->cdb[7] = (unsigned char) ((size & 0xFF00) >>  8);
  sio->cdb[8] = (unsigned char)  (size & 0xFF);
  sio->cdb[9] = 0;

  sio->cdb_length = 10;		/* ten-byte cdb */

/*
 * Set up the data part:
 */
  sio->data = buf;
  sio->data_length = size;
  sio->data_direction = HOST_READ;

/*
 * Set the timeout then execute:
 */
  sio->timeout_ms = (sio->family == drivefamily_lto) ? LTO_LOGSENSE_TIMEOUT : DAT_LOGSENSE_TIMEOUT;
  status = ltotape_scsiexec (sio);

  return status;
}

/**------------------------------------------------------------------------**
 * Tell the remaining capacity
 * @param device a pointer to the ltotape backend
 * @param cap pointer to teh capasity data. This function will update capasity infomation.
 * @return 0 on success or a negative value on error
 */
#define LOG_TAPECAPACITY         (0x31)

enum {
	TAPECAP_REMAIN_0 = 0x0001,	/* < Partition0 Remaining Capacity */
	TAPECAP_REMAIN_1 = 0x0002,	/* < Partition1 Remaining Capacity */
	TAPECAP_MAX_0 = 0x0003,		/* < Partition0 MAX Capacity */
	TAPECAP_MAX_1 = 0x0004,		/* < Partition1 MAX Capacity */
	TAPECAP_SIZE = 0x0005
};

int ltotape_remaining_capacity(void *device, struct tc_remaining_cap *cap)
{
	unsigned char logdata[LOGSENSEPAGE];
	unsigned char buf[16];
	int param_size = 0, i = 0, status = 0;
	uint32_t logcap = 0;
	ltotape_scsi_io_type *sio = (ltotape_scsi_io_type*) device;

	status = ltotape_logsense(device, LOG_TAPECAPACITY, logdata, LOGSENSEPAGE);
	if (status < 0) {
		ltfsmsg(LTFS_ERR, 20069E, LOG_TAPECAPACITY, status);
		return status;
	}

	for (i = TAPECAP_REMAIN_0; i < TAPECAP_SIZE; i++) {
		if ((parse_logPage(logdata, (uint16_t) i, &param_size, buf, 16) != 0)
				|| (param_size != sizeof(uint32_t))) {
			ltfsmsg(LTFS_ERR, 20070E);
			return -ENOBUFS;
		}

		logcap = ((uint32_t) buf[0] << 24) + ((uint32_t) buf[1] << 16)
				+ ((uint32_t) buf[2] << 8) + (uint32_t) buf[3];

		/*
		 * DAT drives return values in units of kB not MB, so need to scale them back:
		 */
		if (sio->family == drivefamily_dat) {
			logcap /= 1024;
		}

		switch (i) {
		case TAPECAP_REMAIN_0:
			cap->remaining_p0 = logcap;
			break;
		case TAPECAP_REMAIN_1:
			cap->remaining_p1 = logcap;
			break;
		case TAPECAP_MAX_0:
			cap->max_p0 = logcap;
			break;
		case TAPECAP_MAX_1:
			cap->max_p1 = logcap;
			break;
		default:
			ltfsmsg(LTFS_ERR, 20071E, i);
			return -EINVAL;
			break;
		}
	}

	ltfsmsg(LTFS_DEBUG, 20057D,
			"capacity part0", (unsigned long long)cap->remaining_p0, (unsigned long long)cap->max_p0);
	ltfsmsg(LTFS_DEBUG, 20057D,
			"capacity part1", (unsigned long long)cap->remaining_p1, (unsigned long long)cap->max_p1);

	return 0;
}

/**------------------------------------------------------------------------**
 * Get mode data
 * @param device a pointer to the ltotape backend
 * @param page a page id of mode data
 * @param pc page control value for mode sense command
 * @param subpage the desired subpage code
 * @param buf pointer to mode page data. this function will update this data
 * @param size length of buf
 * @return 0 on success or a negative value on error
 */
int ltotape_modesense(void *device, const uint8_t page, const TC_MP_PC_TYPE pc, 
                      const uint8_t subpage, unsigned char *buf, const size_t size)
{
  ltotape_scsi_io_type *sio = (ltotape_scsi_io_type*)device;
  uint16_t     length;
  int          status;

  ltfsmsg(LTFS_DEBUG, 20061D, "modesense", page);

/*
 * DAT drives don't support the Device Configuration mode page so fudge a return value..
 */
  if ((sio->family == drivefamily_dat) && (page == TC_MP_DEV_CONFIG_EXT)) {
     return 0;
  }

  length  = (size > MAX_UINT16) ? MAX_UINT16 : (uint16_t)size;

/*
 * Set up the cdb:
 */
  sio->cdb[0] = CMDmode_sense10;
  sio->cdb[1] = 0;
  sio->cdb[2] = (unsigned char) pc | (page & 0x3F);
  sio->cdb[3] = subpage;
  sio->cdb[4] = 0;
  sio->cdb[5] = 0;
  sio->cdb[6] = 0;
  sio->cdb[7] = (unsigned char)(length >> 8);
  sio->cdb[8] = (unsigned char)(length & 0xFF);
  sio->cdb[9] = 0;

  sio->cdb_length = 10;		/* ten-byte cdb */

/*
 * Set up the data part:
 */
  sio->data = buf;
  sio->data_length = length;
  sio->data_direction = HOST_READ;

/*
 * Set the timeout then execute:
 */
  sio->timeout_ms = (sio->family == drivefamily_lto) ? LTO_MODESENSE_TIMEOUT : DAT_MODESENSE_TIMEOUT;
  status = ltotape_scsiexec (sio);

  if (status == -1) {
     ltfsmsg(LTFS_ERR, 20072E, status);
     ltotape_log_snapshot (device, FALSE);
  }

  return status;
}

/**------------------------------------------------------------------------**
 * Set mode data
 * @param device a pointer to the ltotape backend
 * @param buf pointer to mode page data. This data will be sent to the drive
 * @param size length of buf
 * @return 0 on success or a negative value on error
 */
int ltotape_modeselect(void *device, unsigned char *buf, const size_t size)
{
  ltotape_scsi_io_type *sio = (ltotape_scsi_io_type*)device;
  int          status;
  size_t       mysize;

  if (size > MAX_UINT16) {
     ltfsmsg(LTFS_ERR, 20019E, size, "modeselect");
     return (-1);
  }

/*
 * Try to prevent mode select to DAT sending too many bytes for the medium partitions page...
 *  Not the best place to do this, nor the most elegant solution...
 */
  mysize = size;
  if (sio->family == drivefamily_dat) {
     if ((size == 28) && (*(buf+16) == 0x11)) {
        mysize -= 2;       /* Reduce length since cannot specify P0 size  */
        *(buf+24) = 0x10;  /* change P1 size to 0x1000 = 4GB, since for   */
        *(buf+25) = 0x00;  /*  DAT P1 is the only size you can specify... */
     }
  }

/*
 * Set up the cdb:
 */
  sio->cdb[0] = CMDmode_select10;
  sio->cdb[1] = 0x10;  /* must set PF bit */
  sio->cdb[2] = 0;
  sio->cdb[3] = 0;
  sio->cdb[4] = 0;
  sio->cdb[5] = 0;
  sio->cdb[6] = 0;
  sio->cdb[7] = (unsigned char)(mysize >> 8);
  sio->cdb[8] = (unsigned char)(mysize & 0xFF);
  sio->cdb[9] = 0;

  sio->cdb_length = 10;		/* ten-byte cdb */

/*
 * Set up the data part:
 */
  sio->data = buf;
  sio->data_length = mysize;
  sio->data_direction = HOST_WRITE;

/*
 * Set the timeout then execute:
 */
  sio->timeout_ms = (sio->family == drivefamily_lto) ? LTO_MODESELECT_TIMEOUT : DAT_MODESELECT_TIMEOUT;
  status = ltotape_scsiexec (sio);

  /* 01/3700 Mode select parameter is rounded by the drive (Should be ignored)*/
  if (((sio->type == drive_lto7)||(sio->type == drive_lto8)) && (status == -EDEV_MODE_PARAMETER_ROUNDED)) {
	  status = 0;
  }else if(status == -EDEV_MODE_PARAMETER_ROUNDED) {
	  status = -1;
  }

  if (status == -1) {
     ltfsmsg(LTFS_ERR, 20073E, status);
	 ltotape_log_snapshot (device, FALSE);
  }

  return status;
}

/**------------------------------------------------------------------------**
 * Reserve the drive
 * @param device a pointer to the ltotape backend
 * @return 0 on success or a negative value on error
 */
int ltotape_reserve_unit(void *device)
{
  ltotape_scsi_io_type *sio = (ltotape_scsi_io_type*)device;
  int          status;

/*
 * Set up the cdb:
 */
  sio->cdb[0] = CMDreserve;
  sio->cdb[1] = 0;
  sio->cdb[2] = 0;
  sio->cdb[3] = 0;
  sio->cdb[4] = 0;
  sio->cdb[5] = 0;

  sio->cdb_length = 6;		/* six-byte cdb */

/*
 * Set up the data part:
 */
  sio->data = (unsigned char *) NULL;
  sio->data_length = 0;
  sio->data_direction = NO_TRANSFER;

/*
 * Set the timeout then execute:
 */
  sio->timeout_ms = (sio->family == drivefamily_lto) ? LTO_RESERVE_TIMEOUT : DAT_RESERVE_TIMEOUT;
  status = ltotape_scsiexec (sio);

  return status;
}

/**------------------------------------------------------------------------**
 * Release the drive
 * @param device a pointer to the ltotape backend
 * @return 0 on success or a negative value on error
 */
int ltotape_release_unit(void *device)
{
  ltotape_scsi_io_type *sio = (ltotape_scsi_io_type*)device;
  int          status;

/*
 * Set up the cdb:
 */
  sio->cdb[0] = CMDrelease;
  sio->cdb[1] = 0;
  sio->cdb[2] = 0;
  sio->cdb[3] = 0;
  sio->cdb[4] = 0;
  sio->cdb[5] = 0;

  sio->cdb_length = 6;		/* six-byte cdb */

/*
 * Set up the data part:
 */
  sio->data = (unsigned char *) NULL;
  sio->data_length = 0;
  sio->data_direction = NO_TRANSFER;

/*
 * Set the timeout then execute:
 */
  sio->timeout_ms = (sio->family == drivefamily_lto) ? LTO_RELEASE_TIMEOUT : DAT_RELEASE_TIMEOUT;
  status = ltotape_scsiexec (sio);

  return status;
}

/**------------------------------------------------------------------------**
 * Prevent or allow medium removal
 * @param device a pointer to the ltotape backend
 * @param prevent - allow (==0) or prevent (!=0) removal
 * @return 0 on success or a negative value on error
 */
static int ltotape_prevent_allow_medium_removal(void *device, int prevent)
{
  ltotape_scsi_io_type *sio = (ltotape_scsi_io_type*)device;
  
/*
 * Set up the cdb:
 */
  sio->cdb[0] = CMDprevent_allow_media;
  sio->cdb[1] = 0;
  sio->cdb[2] = 0;
  sio->cdb[3] = 0;
  sio->cdb[4] = (prevent)? 1:0;
  sio->cdb[5] = 0;

  sio->cdb_length = 6;		/* six-byte cdb */

/*
 * Set up the data part:
 */
  sio->data = (unsigned char *) NULL;
  sio->data_length = 0;
  sio->data_direction = NO_TRANSFER;

/*
 * Set the timeout then execute:
 */
  sio->timeout_ms = (sio->family == drivefamily_lto) ? LTO_PREVENTALLOWMEDIA_TIMEOUT : DAT_PREVENTALLOWMEDIA_TIMEOUT;
  return (ltotape_scsiexec (sio));
}

/**------------------------------------------------------------------------**
 * Prevent medium removal
 * @param device a pointer to the ltotape backend
 * @return 0 on success or a negative value on error
 */
int ltotape_prevent_medium_removal(void *device)
{
   return (ltotape_prevent_allow_medium_removal (device, 1)); /* 1 = PREVENT */
}

/**------------------------------------------------------------------------**
 * Allow medium removal
 * @param device a pointer to the ltotape backend
 * @return 0 on success or a negative value on error
 */
int ltotape_allow_medium_removal(void *device)
{
  int status;
  status = ltotape_prevent_allow_medium_removal (device, 0); /* 0 = ALLOW */

  ltotape_log_snapshot (device, TRUE); /* sneak in to grab a log snapshot */

  return status;
}

/**------------------------------------------------------------------------**
 * Read attribute
 * @param device a pointer to the ltotape backend
 * @param part partition to read attribute
 * @param id attribute id to get
 * @param buf pointer to the attribute buffer. This function will update this value.
 * @param size length of the buffer
 * @return 0 on success or a negative value on error
 */
int ltotape_read_attribute(void *device, const tape_partition_t part, const uint16_t id,
                          unsigned char *buf, const size_t size)
{
  ltotape_scsi_io_type *sio = (ltotape_scsi_io_type*)device;
  int          length;
  int          status;
  unsigned char      *pRawData;

  ltfsmsg(LTFS_DEBUG, 20057D, "readattr", (unsigned long long)part, (unsigned long long)id);

/*
 * DAT drives will not support the required attributes (and some transports like USB
 *  don't allow 16-byte cdbs either) so return an error without attempting the command:
 */
  if (sio->family == drivefamily_dat) {
     return -1;
  }

/*
 * Prepare a data buffer with space for the Available Data field as well:
 */
   length = size + 4;
   pRawData = (unsigned char*) calloc(1, length);
   if (pRawData == NULL) {
      ltfsmsg(LTFS_ERR, 10001E, "ltotape_read_attribute: data buffer");
      return -ENOMEM;
   }
  
/*
 * Set up the cdb:
 */
  sio->cdb[0]  = CMDread_attribute;
  sio->cdb[1]  = 0; /* Service Action 0x00 = Return Value */
  sio->cdb[2]  = 0;
  sio->cdb[3]  = 0;
  sio->cdb[4]  = 0;
  sio->cdb[5]  = 0;
  sio->cdb[6]  = 0;
  sio->cdb[7]  = (unsigned char) part;
  sio->cdb[8]  = (unsigned char) (id >> 8);
  sio->cdb[9]  = (unsigned char) (id & 0xFF);
  sio->cdb[10] = (unsigned char) ((length & 0xFF000000)  >> 24);
  sio->cdb[11] = (unsigned char) ((length & 0xFF0000)    >> 16);
  sio->cdb[12] = (unsigned char) ((length & 0xFF00)      >> 8 );
  sio->cdb[13] = (unsigned char) ((length & 0xFF)             );
  sio->cdb[14] = 0;
  sio->cdb[15] = 0;

  sio->cdb_length = 16;		/* sixteen-byte cdb */

/*
 * Set up the data part:
 */
  sio->data = pRawData;
  sio->data_length = length;
  sio->data_direction = HOST_READ;

/*
 * Set the timeout then execute:
 */
  sio->timeout_ms = (sio->family == drivefamily_lto) ? LTO_READATTRIB_TIMEOUT : DAT_READATTRIB_TIMEOUT;
  status = ltotape_scsiexec (sio);

  if (status == 0) {
     memcpy (buf, (pRawData + 4), size);

  } else if (SENSE_IS_BAD_ATTRIBID(sio->sensedata)) {
     ltfsmsg(LTFS_DEBUG, 20098D, id);
    
  } else {
     ltfsmsg(LTFS_ERR, 20074E, id, status);
  }
  
  free (pRawData);
  return (status);
}

/**------------------------------------------------------------------------**
 * Write attribute
 * @param device a pointer to the ltotape backend
 * @param part partition to read attribute
 * @param id attribute id to get
 * @param buf pointer to the attribute buffer. This function will send this value to the tape.
 *      This function assumes that this buffer does not contain a parameter data length field.
 * @param size length of the buffer
 * @return 0 on success or a negative value on error
 */
/**
 *
 * @param device
 * @param part
 * @param buf
 * @param size
 * @return
 */
int ltotape_write_attribute(void *device, const tape_partition_t part, 
		const unsigned char *buf, const size_t size)
{
	int						length = 0, status = DEVICE_GOOD;
	unsigned char			*raw_data = NULL;
	ltotape_scsi_io_type	*sio = (ltotape_scsi_io_type*)device;

	ltfsmsg(LTFS_DEBUG, 20059D, "writeattr", (unsigned long long)part);

	/*
	 * DAT drives will not support the required attributes (and some transports
	 * like USB don't allow 16-byte cdbs either) so no point in continuing.
	 */
	if (sio->family == drivefamily_dat) {
		return -1;
	}

	length = size + 4;
	raw_data = (unsigned char*) calloc (1, length);
	if (raw_data == NULL) {
		ltfsmsg(LTFS_ERR, 10001E, "ltotape_write_attribute: data buffer");
		return -EDEV_NO_MEMORY;
	}

	*raw_data     = (unsigned char)(size >> 24);
	*(raw_data+1) = (unsigned char)(size >> 16);
	*(raw_data+2) = (unsigned char)(size >>  8);
	*(raw_data+3) = (unsigned char)(size & 0xFF);
	memcpy (raw_data+4, buf, size);

	/* Set up the cdb. */
	sio->cdb[0] = CMDwrite_attribute;
	sio->cdb[1] = 0;		/*Could set WTC bit but not necessary*/
	sio->cdb[2] = 0;
	sio->cdb[3] = 0;
	sio->cdb[4] = 0;
	sio->cdb[5] = 0;
	sio->cdb[6] = 0;
	sio->cdb[7] = (unsigned char) part;
	sio->cdb[8] = 0;
	sio->cdb[9] = 0;
	sio->cdb[10] = (unsigned char) ((length & 0xFF000000)  >> 24);
	sio->cdb[11] = (unsigned char) ((length & 0xFF0000)    >> 16);
	sio->cdb[12] = (unsigned char) ((length & 0xFF00)      >> 8 );
	sio->cdb[13] = (unsigned char) ((length & 0xFF)             );
	sio->cdb[14] = 0;
	sio->cdb[15] = 0;

	sio->cdb_length = 16;		/* sixteen-byte cdb */

	/* Setup the data part */
	sio->data = raw_data;
	sio->data_length = length;
	sio->data_direction = HOST_WRITE;

	/* Set the timeout then execute */
	sio->timeout_ms = (sio->family == drivefamily_lto) ?
			LTO_WRITEATTRIB_TIMEOUT : DAT_WRITEATTRIB_TIMEOUT;
	status = ltotape_scsiexec(sio);

	if (status == -1) {
		ltfsmsg(LTFS_ERR, 20075E, status);
		ltotape_log_snapshot(device, FALSE);
	}

	free (raw_data);
	return status;
}

/**
 * Set append point to the device.
 * The device will accept write commmand only on specified position or EOD, if the dvice
 * supports this feature.
 * @param device A pointer to the ltotape backend.
 * @param pos A specific append position.
 * @return int DEVICE_GOOD on success, a -ve value on failure.
 */
int ltotape_allow_overwrite(void *device, const struct tc_position pos)
{
	int						rc = DEVICE_GOOD;
	ltotape_scsi_io_type	*sio = (ltotape_scsi_io_type *) device;

	CHECK_ARG_NULL(sio, -EDEV_INVALID_ARG);

	return rc;
}

/**
 * Reports the density information requested by libltfs for a given tape.
 * This command is not currently used by libltfs.
 *
 * @param device A pointer to the ltotape backend.
 * @param rep Pointer to the requested density information.
 * @param medium set medium bit on.
 * @return DEVICE_GOOD on success, else one of the error codes.
 */
int ltotape_report_density(void *device, struct tc_density_report *rep, bool medium)
{
	unsigned char			density_buffer[64];
	uint16_t				length = 0;
	int						status = DEVICE_GOOD;
	ltotape_scsi_io_type	*sio = (ltotape_scsi_io_type*)device;

	length = sizeof(density_buffer);
	memset(sio->cdb, 0, sizeof(sio->cdb));

	/* Set up the cdb */
	sio->cdb[0] = CMDreport_density_support;
	if (medium) {
		sio->cdb[1] = CURRENT_MEDIA_DENSITY;
	} else {
		sio->cdb[1] = ALL_MEDIA_DENSITY;
	}
	sio->cdb[2] = 0;
	sio->cdb[3] = 0;
	sio->cdb[4] = 0;
	sio->cdb[5] = 0;
	sio->cdb[6] = 0;
	sio->cdb[7] = (unsigned char) ((length & 0xFF00) >>  8);
	sio->cdb[8] = (unsigned char) (length & 0xFF);
	sio->cdb[9] = 0;

	sio->cdb_length = 10;		/* ten-byte cdb */

	/* Set up the data part: */
	sio->data = density_buffer;
	sio->data_length = length;
	sio->data_direction = HOST_READ;

	/* Set the timeout then execute: */
	sio->timeout_ms = (sio->family == drivefamily_lto) ?
			LTO_REPORTDENSITY_TIMEOUT : DAT_REPORTDENSITY_TIMEOUT;
	status = ltotape_scsiexec(sio);

	if (status == 0) {
		rep->size = 1;
		rep->density[0].primary = density_buffer[4];
		rep->density[0].secondary = density_buffer[5];
		status = DEVICE_GOOD;
	} else {
		rep->size = 0;
	}

	return status;
}

/**------------------------------------------------------------------------**
 * Set compression setting
 * @param device a pointer to the ltotape backend
 * @param enable_compression enable: true, disable: false
 * @param pos a pointer to position data. This function will update position infomation.
 * @return 0 on success or a negative value on error
 */
int ltotape_set_compression(void *device, const bool enable_compression, struct tc_position *pos)
{
  unsigned char modepage[32];
  int		status;

/*
 * First, fetch the mode page from the drive (subpage code is 0)
 *  bomb out if that failed:
 */
  status = ltotape_modesense (device, MODE_PAGE_DATA_COMPRESSION, TC_MP_PC_CURRENT, 0, modepage, sizeof(modepage));

/*
 * If that worked, twiddle the bits and send it back:
 */
  if (status == 0) {
    modepage[0] = 0;		/* set mode data length to 0 for mode select  */
    modepage[1] = 0;		/*  (two-byte field for ModeSelect10)         */

    if (enable_compression) {
      modepage[18] |= 0x80;	/* Turn ON DCE bit			      */
    } else {
      modepage[18] &= 0x7F;	/* Clear DCE bit			      */
    }

    status = ltotape_modeselect (device, modepage, sizeof(modepage));
  }

  return (status);
}

/**------------------------------------------------------------------------**
 * Return drive setting in default
 * @param device a pointer to the ltotape backend
 * @return 0 on success or a negative value on error
 */
int ltotape_set_default(void *device)
{
  unsigned char modepage[16];
  int		status;

/*
 * First, fetch the mode block descriptor from the drive; bomb out if that failed:
 */
  status = ltotape_modesense (device, 0, TC_MP_PC_CURRENT, 0, modepage, sizeof(modepage));

/*
 * If that worked, twiddle the bits and send it back:
 */
  if (status == 0) {
    modepage[0]  = 0;		/* set mode data length to 0 for mode select  */
    modepage[1]  = 0;		/*  (two-byte field for ModeSelect10)         */
    modepage[13] = 0;           /* set fixed block size to 0 (three bytes)    */
    modepage[14] = 0;
    modepage[15] = 0;

    status = ltotape_modeselect (device, modepage, sizeof(modepage));
  }

  return (status);
}

/**------------------------------------------------------------------------**
 * Get drive parameter
 * @param device a pointer to the ltotape backend
 * @param drive_param pointer to the drive parameter infomation. This function will update this value.
 * @return 0 on success or a negative value on error
 */
int ltotape_get_parameters(void *device, struct tc_current_param *drive_param)
{
  unsigned char modeheader[8];
  unsigned char blocklimits[6];
  int		status;
  unsigned char buf [64];
  int           mediatype;
  const char*   pMediaName = "";
  ltotape_scsi_io_type  *sio = (ltotape_scsi_io_type*)device;

/*
 * First, fetch the mode block descriptor from the drive to find the Write Protect state:
 */
  status = ltotape_modesense (device, 0, TC_MP_PC_CURRENT, 0, modeheader, sizeof(modeheader));

  if (status < 0) 
  {
    return (status);
  }

  drive_param->write_protected = ((modeheader[3] & 0x80) == 0x80) ? true : false;

/* Since LTO7 and LTO8 drive can not write data into LTO5RW media,
 * Set logical_write_protect to 1 if an LTO5RW tape inserted into an LTO7 or LTO7 drive
 * and logical_write_protect to 1 if an LTO6RW tape inserted into an LTO8 drive
 */
  if ((drive_param->write_protected == false) && ((sio->type == drive_lto7)||(sio->type == drive_lto8))) 
  {
    status = ltotape_modesense(device, MODE_PAGE_MEDIUM_CONFIGURATION,
  			TC_MP_PC_CURRENT, 0x00, buf, sizeof(buf));
 	 if (status == 0) 
 	 {
 		/* media type comprises density code from the block descriptor + WORMM bit from mode data */
  		mediatype = (int) buf[8] + ((int) (buf[18] & 0x01) << 8);
  		switch (mediatype) 
  		{
  		  case LTOMEDIATYPE_LTO6RW:
  		    if (sio->type == drive_lto8)
  		    {
  		      pMediaName = "LTO6RW";
  			   drive_param->write_protected = 1;
  			 }  
  			 break;  
  		  case LTOMEDIATYPE_LTO5RW:
  			 pMediaName = "LTO5RW";
  			 drive_param->write_protected = 1;
  			 break;
  		  default:
  			 pMediaName = "Unknown";
  			 break;
  		}
  	 }
  }
/*
 * Then issue Read Block Limits to determine max block size - unless it's a DAT
 *  drive, in which case we'll limit it to 64kB to avoid transport issues..
 */

  if (sio->family == drivefamily_dat) {
     drive_param->max_blksize = 65536;
  
  } else {
/*
 * Set up the cdb:
 */
     sio->cdb[0] = CMDread_block_limits;
     sio->cdb[1] = 0;
     sio->cdb[2] = 0;
     sio->cdb[3] = 0;
     sio->cdb[4] = 0;
     sio->cdb[5] = 0;

     sio->cdb_length = 6;		/* six-byte cdb */

/*
 * Set up the data part:
 */
     sio->data = blocklimits;
     sio->data_length = sizeof(blocklimits);
     sio->data_direction = HOST_READ;

/*
 * Set the timeout then execute:
 */
     sio->timeout_ms = (sio->family == drivefamily_lto) ? LTO_READBLOCKLIMITS_TIMEOUT : DAT_READBLOCKLIMITS_TIMEOUT;
     status = ltotape_scsiexec (sio);

     if (status == 0) {
        drive_param->max_blksize = ((uint)blocklimits[1] << 16) + 
                                   ((uint)blocklimits[2] <<  8) +
                                    (uint)blocklimits[3];
/*
 * Normally we'll limit the "max blocksize" to the preferred size; however
 *  the user can pass the fuse option "-o nosizelimit" in which case we'll
 *  go up to the maximum practical size (which is called unlimited but isn't
 *  really because the OS will limit it, so we use our best guess of the OS
 *  limit value):
 */
        if (sio->unlimited_blocksize == 0) {
	   if (drive_param->max_blksize > LTOTAPE_MAX_TRANSFER_SIZE) {
	     drive_param->max_blksize = LTOTAPE_MAX_TRANSFER_SIZE;
	   }
	} else {
	   if (drive_param->max_blksize > LTOTAPE_OS_LIMITED_SIZE) {
	     drive_param->max_blksize = LTOTAPE_OS_LIMITED_SIZE;
	   }
	}
     }
  }

  return (status);
}

/**
 * Get a list of available tape devices for LTFS found in the host.
 * When buf is NULL, this function just returns an available tape device count.
 * @param[out] buf Pointer to tc_drive_info structure array.
 * The backend must fill this structure when this paramater is not NULL.
 * @param count size of array in buf.
 * @return on success, available device count on this system or a negative
 * value on error.
 */
int ltotape_get_device_list(struct tc_drive_info *buf, int count)
{
	return 0;
}

/**------------------------------------------------------------------------**
 * Set various MAM attributes to label this as a fresh LTFS tape, or to
 *  undo all attributes (if 'unformatting' a volume)
 * @param device a pointer to the ltotape backend
 * @param format the type of format being done, so we can clear on 'unformat'
 * @return 0 if set ok, negative value on error
 */
static int ltotape_set_MAMattributes (void* device, TC_FORMAT_TYPE format,
									  const char *vol_name, unsigned int attribute_id,
									  const char *barcode_name, mam_lockval lockbit,
									  const char *vol_mam_uuid)
{
	unsigned char	*buf = NULL;
	char			*volume_name = NULL, *barcode = NULL, *volume_mam_uuid = NULL;
	int				status = 0, len = 0, srclen = 0, ret = 0;

	switch (attribute_id) {
	case LTOATTRIBID_APPLICATION_VENDOR:
		/* Set Application Vendor: */
		buf = (unsigned char *) calloc(1, 40);
		memset(buf, (int) 0x20, 40);
		buf[0] = (unsigned char) (LTOATTRIBID_APPLICATION_VENDOR >> 8);
		buf[1] = (unsigned char) (LTOATTRIBID_APPLICATION_VENDOR & 0xFF);
		buf[2] = 1; /* format = ascii (01b) */
		buf[3] = 0; /* attrib len is two bytes but only need one */

		if (format == TC_FORMAT_DEFAULT) {
			buf[4] = 0; /* set length to zero to delete the attribute */
			len = ATTRIB_HEADER_LEN;

		} else {
			buf[4] = LTOATTRIB_APPLICATION_VENDOR_LEN;
#ifdef HPE_BUILD
			buf[5] = (unsigned char) 'H';
			buf[6] = (unsigned char) 'P';
			buf[7] = (unsigned char) 'E';
#elif defined QUANTUM_BUILD
			buf[5] = (unsigned char) 'Q';
			buf[6] = (unsigned char) 'U';
			buf[7] = (unsigned char) 'A';
			buf[8] = (unsigned char) 'N';
			buf[9] = (unsigned char) 'T';
			buf[10] = (unsigned char) 'U';
			buf[11] = (unsigned char) 'M';
#elif defined GENERIC_OEM_BUILD
			buf[5] = (unsigned char) 'L';
			buf[6] = (unsigned char) 'T';
			buf[7] = (unsigned char) 'F';
			buf[8] = (unsigned char) 'S';
#else
# error "No Application Vendor defined!"
#endif
			len = LTOATTRIB_APPLICATION_VENDOR_LEN + ATTRIB_HEADER_LEN;
		}
		status = ltotape_write_attribute(device, (const tape_partition_t) 0,
				buf, len);

		/* Cleanup. */
		if (buf) {
			free(buf);
			buf = NULL;
		}
		if (status < 0) {
			ltfsmsg(LTFS_WARN, 20024W,
					LTOATTRIBID_APPLICATION_VENDOR, status);
			ret = status;
		}

		break;
	case LTOATTRIBID_APPLICATION_NAME:
		/* Set Application Name: */
		buf = (unsigned char *) calloc(1, 40);
		memset(buf, (int) 0x20, 40);
		buf[0] = (unsigned char) (LTOATTRIBID_APPLICATION_NAME >> 8);
		buf[1] = (unsigned char) (LTOATTRIBID_APPLICATION_NAME & 0xFF);
		buf[2] = 1; /* format = ascii (01b) */
		buf[3] = 0; /* attrib len is two bytes but only need one */

		if (format == TC_FORMAT_DEFAULT) {
			buf[4] = 0; /* set length to zero to delete the attribute */
			len = ATTRIB_HEADER_LEN;

		} else {
			buf[4] = LTOATTRIB_APPLICATION_NAME_LEN;
			srclen = strlen(PACKAGE_NAME);
			len = (srclen > LTOATTRIB_APPLICATION_NAME_LEN) ?
					LTOATTRIB_APPLICATION_NAME_LEN : srclen;
			memcpy((void*) buf + 5, (const void*) PACKAGE_NAME, len);
			len = LTOATTRIB_APPLICATION_NAME_LEN + ATTRIB_HEADER_LEN;
		}

		status = ltotape_write_attribute(device, (const tape_partition_t) 0,
				buf, len);

		/* Cleanup. */
		if (buf) {
			free(buf);
			buf = NULL;
		}
		if (status < 0) {
			ltfsmsg(LTFS_WARN, 20024W, LTOATTRIBID_APPLICATION_NAME, status);
			ret = status;
		}

		break;
	case LTOATTRIBID_APPLICATION_VERSION:
		/* Set Application Version: */
		buf = (unsigned char *) calloc(1, 40);
		memset(buf, (int) 0x20, 40);
		buf[0] = (unsigned char) (LTOATTRIBID_APPLICATION_VERSION >> 8);
		buf[1] = (unsigned char) (LTOATTRIBID_APPLICATION_VERSION & 0xFF);
		buf[2] = 1; /* format = ascii (01b) */
		buf[3] = 0; /* attrib len is two bytes but only need one */

		if (format == TC_FORMAT_DEFAULT) {
			buf[4] = 0; /* set length to zero to delete the attribute */
			len = ATTRIB_HEADER_LEN;

		} else {
			buf[4] = LTOATTRIB_APPLICATION_VERSION_LEN;
			srclen = strlen(PACKAGE_VERSION);
			len = (srclen > LTOATTRIB_APPLICATION_VERSION_LEN) ?
					LTOATTRIB_APPLICATION_VERSION_LEN : srclen;
			strncpy((char*) buf + 5, PACKAGE_VERSION, len);
			len = LTOATTRIB_APPLICATION_VERSION_LEN + ATTRIB_HEADER_LEN;
		}

		status = ltotape_write_attribute(device, (const tape_partition_t) 0,
				buf, len);

		/* Cleanup. */
		if (buf) {
			free(buf);
			buf = NULL;
		}
		if (status < 0) {
			ltfsmsg(LTFS_WARN, 20024W,
					LTOATTRIBID_APPLICATION_VERSION, status);
			ret = status;
		}

		break;
	case LTOATTRIBID_APP_FORMAT_VERSION:
		/* Set Application Format Version (to the value used in the format label, not the index -
		 * though they should be the same at format time): */
		buf = (unsigned char *) calloc(1, 40);
		memset(buf, (int) 0x20, 40);
		buf[0] = (unsigned char) (LTOATTRIBID_APP_FORMAT_VERSION >> 8);
		buf[1] = (unsigned char) (LTOATTRIBID_APP_FORMAT_VERSION & 0xFF);
		buf[2] = 1; /* format = ascii (01b) */
		buf[3] = 0; /* attrib len is two bytes but only need one */

		if (format == TC_FORMAT_DEFAULT) {
			buf[4] = 0; /* set length to zero to delete the attribute */
			len = ATTRIB_HEADER_LEN;
		} else {
			buf[4] = LTOATTRIB_APP_FORMAT_VERSION_LEN;
			srclen = strlen(LTFS_LABEL_VERSION_STR);
			len = (srclen > LTOATTRIB_APP_FORMAT_VERSION_LEN) ?
					LTOATTRIB_APP_FORMAT_VERSION_LEN : srclen;
			strncpy((char*) buf + 5, LTFS_LABEL_VERSION_STR, len);
			len = LTOATTRIB_APP_FORMAT_VERSION_LEN + ATTRIB_HEADER_LEN;
		}

		status = ltotape_write_attribute(device, (const tape_partition_t) 0,
				buf, len);

		/* Cleanup. */
		if (buf) {
			free(buf);
			buf = NULL;
		}
		if (status < 0) {
			ltfsmsg(LTFS_WARN, 20024W,
					LTOATTRIBID_APP_FORMAT_VERSION, status);
			ret = status;
		}

		break;
	case LTOATTRIBID_USR_MED_TXT_LABEL:
		/* Set User Medium Text label */

		volume_name = (char *) calloc(1, LTOATTRIBID_USR_MED_TXT_LABEL_LEN);

		if (vol_name && strlen(vol_name)) {
			if (strlen(vol_name) > (LTOATTRIBID_USR_MED_TXT_LABEL_LEN - 1)) {
				strncpy(volume_name, vol_name,
						(LTOATTRIBID_USR_MED_TXT_LABEL_LEN - 1));
			} else {
				strncpy(volume_name, vol_name, strlen(vol_name));
			}
		}

		buf = (unsigned char *) calloc(1,
				(LTOATTRIBID_USR_MED_TXT_LABEL_LEN + ATTRIB_HEADER_LEN));
		buf[0] = (unsigned char) (LTOATTRIBID_USR_MED_TXT_LABEL >> 8);
		buf[1] = (unsigned char) (LTOATTRIBID_USR_MED_TXT_LABEL & 0xFF);
		buf[2] = 2; /* format = text (10b) */
		buf[3] = 0; /* attrib len is two bytes but only need one */

		if (format == TC_FORMAT_DEFAULT) {
			buf[4] = 0; /* set length to zero to delete the attribute */
			len = ATTRIB_HEADER_LEN;
		} else {
			buf[4] = LTOATTRIBID_USR_MED_TXT_LABEL_LEN;
			srclen = strlen(volume_name);
			len = (srclen > LTOATTRIBID_USR_MED_TXT_LABEL_LEN) ?
					LTOATTRIBID_USR_MED_TXT_LABEL_LEN : srclen;
			strncpy((char*) buf + 5, volume_name, len);
			len = LTOATTRIBID_USR_MED_TXT_LABEL_LEN + ATTRIB_HEADER_LEN;
		}

		status = ltotape_write_attribute(device, (const tape_partition_t) 0,
				buf, len);

		/* Cleanup. */
		if (buf) {
			free(buf);
			buf = NULL;
		}

		if (volume_name) {
			free(volume_name);
			volume_name = NULL;
		}

		if (status < 0) {
			ltfsmsg(LTFS_WARN, 20024W,
					LTOATTRIBID_USR_MED_TXT_LABEL, status);
			ret = status;
		}
		break;
	case LTOATTRIBID_BARCODE:
		/* Set Barcode */
		if (barcode_name && strlen(barcode_name)) {
			barcode = (char *) calloc(1, LTOATTRIBID_BARCODE_LEN);

			if (strlen(barcode_name) > (LTOATTRIBID_BARCODE_LEN)) {
				strncpy(barcode, barcode_name,
						(LTOATTRIBID_BARCODE_LEN));
			} else {
				strncpy(barcode, barcode_name, strlen(barcode_name));
			}

			buf = (unsigned char *) calloc(1,
					(LTOATTRIBID_BARCODE_LEN + ATTRIB_HEADER_LEN));
			memset(buf, (int) 0x20, (LTOATTRIBID_BARCODE_LEN + ATTRIB_HEADER_LEN));
			buf[0] = (unsigned char) (LTOATTRIBID_BARCODE >> 8);
			buf[1] = (unsigned char) (LTOATTRIBID_BARCODE & 0xFF);
			buf[2] = 1; /* format = ascii (01b) */
			buf[3] = 0; /* attrib len is two bytes but only need one */

			/* Delete barcode attribute if 6 blank spaces are recieved for barcode_name param */
			if (format == TC_FORMAT_DEFAULT || !strcmp(barcode_name, "      ")) {
				buf[4] = 0; /* set length to zero to delete the attribute */
				len = ATTRIB_HEADER_LEN;
			} else {
				buf[4] = LTOATTRIBID_BARCODE_LEN;
				srclen = strlen(barcode);
				len = (srclen > LTOATTRIBID_BARCODE_LEN) ?
						LTOATTRIBID_BARCODE_LEN : srclen;
				strncpy((char*) buf + 5, barcode, len);
				len = LTOATTRIBID_BARCODE_LEN + ATTRIB_HEADER_LEN;
			}

			status = ltotape_write_attribute(device, (const tape_partition_t) 0,
					buf, len);

			/* Cleanup. */
			if (buf) {
				free(buf);
				buf = NULL;
			}
			if (status < 0) {
				ltfsmsg(LTFS_WARN, 20024W,
						LTOATTRIBID_BARCODE, status);
				ret = status;
			}
		}
		break;
	case LTOATTRIBID_VOL_LOCK_STATE:
		/* Set Volume Lock State: */
		buf = (unsigned char *) calloc(1, LTOATTRIBID_VOL_LOCK_STATE_LEN + ATTRIB_HEADER_LEN);
		memset(buf, (int) 0x20, LTOATTRIBID_VOL_LOCK_STATE_LEN + ATTRIB_HEADER_LEN);
		buf[0] = (unsigned char) (LTOATTRIBID_VOL_LOCK_STATE >> 8);
		buf[1] = (unsigned char) (LTOATTRIBID_VOL_LOCK_STATE & 0xFF);
		buf[2] = 0; /* format = binary */
		buf[3] = 0; /* attrib len is two bytes but only need one */

		if (format == TC_FORMAT_DEFAULT) {
			buf[4] = 0; /* set length to zero to delete the attribute */
			len = ATTRIB_HEADER_LEN;

		} else {
			buf[4] = LTOATTRIBID_VOL_LOCK_STATE_LEN;
			buf[5] = (unsigned char) lockbit;
			len = LTOATTRIBID_VOL_LOCK_STATE_LEN + ATTRIB_HEADER_LEN;
		}

		status = ltotape_write_attribute(device, (const tape_partition_t) 0,
				buf, len);

		/* Cleanup. */
		if (buf) {
			free(buf);
			buf = NULL;
		}

		if (status < 0) {
			ltfsmsg(LTFS_WARN, 20024W, LTOATTRIBID_VOL_LOCK_STATE, status);
			ret = status;
		}

		break;
	default:
		/* Set Application Vendor: */
		buf = (unsigned char *) calloc(1, 40);
		memset(buf, (int) 0x20, 40);
		buf[0] = (unsigned char) (LTOATTRIBID_APPLICATION_VENDOR >> 8);
		buf[1] = (unsigned char) (LTOATTRIBID_APPLICATION_VENDOR & 0xFF);
		buf[2] = 1; /* format = ascii (01b) */
		buf[3] = 0; /* attrib len is two bytes but only need one */

		if (format == TC_FORMAT_DEFAULT) {
			buf[4] = 0; /* set length to zero to delete the attribute */
			len = ATTRIB_HEADER_LEN;

		} else {
			buf[4] = LTOATTRIB_APPLICATION_VENDOR_LEN;
#ifdef HPE_BUILD
			buf[5] = (unsigned char) 'H';
			buf[6] = (unsigned char) 'P';
			buf[7] = (unsigned char) 'E';
#elif defined QUANTUM_BUILD
			buf[5] = (unsigned char) 'Q';
			buf[6] = (unsigned char) 'U';
			buf[7] = (unsigned char) 'A';
			buf[8] = (unsigned char) 'N';
			buf[9] = (unsigned char) 'T';
			buf[10] = (unsigned char) 'U';
			buf[11] = (unsigned char) 'M';
#elif defined GENERIC_OEM_BUILD
			buf[5] = (unsigned char) 'L';
			buf[6] = (unsigned char) 'T';
			buf[7] = (unsigned char) 'F';
			buf[8] = (unsigned char) 'S';
#else
# error "No Application Vendor defined!"
#endif
			len = LTOATTRIB_APPLICATION_VENDOR_LEN + ATTRIB_HEADER_LEN;
		}
		status = ltotape_write_attribute(device, (const tape_partition_t) 0,
				buf, len);

		/* Cleanup. */
		if (buf) {
			free(buf);
			buf = NULL;
		}
		if (status < 0) {
			ltfsmsg(LTFS_WARN, 20024W,
					LTOATTRIBID_APPLICATION_VENDOR, status);
			ret = status;
		}

		/* Set Application Name: */
		buf = (unsigned char *) calloc(1, 40);
		memset(buf, (int) 0x20, 40);
		buf[0] = (unsigned char) (LTOATTRIBID_APPLICATION_NAME >> 8);
		buf[1] = (unsigned char) (LTOATTRIBID_APPLICATION_NAME & 0xFF);
		buf[2] = 1; /* format = ascii (01b) */
		buf[3] = 0; /* attrib len is two bytes but only need one */

		if (format == TC_FORMAT_DEFAULT) {
			buf[4] = 0; /* set length to zero to delete the attribute */
			len = ATTRIB_HEADER_LEN;

		} else {
			buf[4] = LTOATTRIB_APPLICATION_NAME_LEN;
			srclen = strlen(PACKAGE_NAME);
			len = (srclen > LTOATTRIB_APPLICATION_NAME_LEN) ?
					LTOATTRIB_APPLICATION_NAME_LEN : srclen;
			memcpy((void*) buf + 5, (const void*) PACKAGE_NAME, len);
			len = LTOATTRIB_APPLICATION_NAME_LEN + ATTRIB_HEADER_LEN;
		}

		status = ltotape_write_attribute(device, (const tape_partition_t) 0,
				buf, len);

		/* Cleanup. */
		if (buf) {
			free(buf);
			buf = NULL;
		}
		if (status < 0) {
			ltfsmsg(LTFS_WARN, 20024W, LTOATTRIBID_APPLICATION_NAME, status);
			ret = status;
		}

		/* Set Application Version: */
		buf = (unsigned char *) calloc(1, 40);
		memset(buf, (int) 0x20, 40);
		buf[0] = (unsigned char) (LTOATTRIBID_APPLICATION_VERSION >> 8);
		buf[1] = (unsigned char) (LTOATTRIBID_APPLICATION_VERSION & 0xFF);
		buf[2] = 1; /* format = ascii (01b) */
		buf[3] = 0; /* attrib len is two bytes but only need one */

		if (format == TC_FORMAT_DEFAULT) {
			buf[4] = 0; /* set length to zero to delete the attribute */
			len = ATTRIB_HEADER_LEN;

		} else {
			buf[4] = LTOATTRIB_APPLICATION_VERSION_LEN;
			srclen = strlen(PACKAGE_VERSION);
			len = (srclen > LTOATTRIB_APPLICATION_VERSION_LEN) ?
					LTOATTRIB_APPLICATION_VERSION_LEN : srclen;
			strncpy((char*) buf + 5, PACKAGE_VERSION, len);
			len = LTOATTRIB_APPLICATION_VERSION_LEN + ATTRIB_HEADER_LEN;
		}

		status = ltotape_write_attribute(device, (const tape_partition_t) 0,
				buf, len);

		/* Cleanup. */
		if (buf) {
			free(buf);
			buf = NULL;
		}
		if (status < 0) {
			ltfsmsg(LTFS_WARN, 20024W,
					LTOATTRIBID_APPLICATION_VERSION, status);
			ret = status;
		}

		/* Set Application Format Version (to the value used in the format label, not the index -
		 * though they should be the same at format time): */
		buf = (unsigned char *) calloc(1, 40);
		memset(buf, (int) 0x20, 40);
		buf[0] = (unsigned char) (LTOATTRIBID_APP_FORMAT_VERSION >> 8);
		buf[1] = (unsigned char) (LTOATTRIBID_APP_FORMAT_VERSION & 0xFF);
		buf[2] = 1; /* format = ascii (01b) */
		buf[3] = 0; /* attrib len is two bytes but only need one */

		if (format == TC_FORMAT_DEFAULT) {
			buf[4] = 0; /* set length to zero to delete the attribute */
			len = ATTRIB_HEADER_LEN;
		} else {
			buf[4] = LTOATTRIB_APP_FORMAT_VERSION_LEN;
			srclen = strlen(LTFS_LABEL_VERSION_STR);
			len = (srclen > LTOATTRIB_APP_FORMAT_VERSION_LEN) ?
					LTOATTRIB_APP_FORMAT_VERSION_LEN : srclen;
			strncpy((char*) buf + 5, LTFS_LABEL_VERSION_STR, len);
			len = LTOATTRIB_APP_FORMAT_VERSION_LEN + ATTRIB_HEADER_LEN;
		}

		status = ltotape_write_attribute(device, (const tape_partition_t) 0,
				buf, len);

		/* Cleanup. */
		if (buf) {
			free(buf);
			buf = NULL;
		}
		if (status < 0) {
			ltfsmsg(LTFS_WARN, 20024W,
					LTOATTRIBID_APP_FORMAT_VERSION, status);
			ret = status;
		}

		/* Set Barcode */
		if (barcode_name && strlen(barcode_name)) {
			barcode = (char *) calloc(1, LTOATTRIBID_BARCODE_LEN);

			if (strlen(barcode_name) > (LTOATTRIBID_BARCODE_LEN)) {
				strncpy(barcode, barcode_name,
						(LTOATTRIBID_BARCODE_LEN));
			} else {
				strncpy(barcode, barcode_name, strlen(barcode_name));
			}

			buf = (unsigned char *) calloc(1,
					(LTOATTRIBID_BARCODE_LEN + ATTRIB_HEADER_LEN));
			memset(buf, (int) 0x20, (LTOATTRIBID_BARCODE_LEN + ATTRIB_HEADER_LEN));
			buf[0] = (unsigned char) (LTOATTRIBID_BARCODE >> 8);
			buf[1] = (unsigned char) (LTOATTRIBID_BARCODE & 0xFF);
			buf[2] = 1; /* format = ascii (01b) */
			buf[3] = 0; /* attrib len is two bytes but only need one */

			/* Delete barcode attribute if 6 blank spaces are recieved for barcode_name param */
			if (format == TC_FORMAT_DEFAULT || !strcmp(barcode_name, "      ")) {
				buf[4] = 0; /* set length to zero to delete the attribute */
				len = ATTRIB_HEADER_LEN;
			} else {
				buf[4] = LTOATTRIBID_BARCODE_LEN;
				srclen = strlen(barcode);
				len = (srclen > LTOATTRIBID_BARCODE_LEN) ?
						LTOATTRIBID_BARCODE_LEN : srclen;
				strncpy((char*) buf + 5, barcode, len);
				len = LTOATTRIBID_BARCODE_LEN + ATTRIB_HEADER_LEN;
			}

			status = ltotape_write_attribute(device, (const tape_partition_t) 0,
					buf, len);

			/* Cleanup. */
			if (buf) {
				free(buf);
				buf = NULL;
			}
			if (status < 0) {
				ltfsmsg(LTFS_WARN, 20024W,
						LTOATTRIBID_BARCODE, status);
				ret = status;
			}
		}

		/* Set User Medium Text label */
		volume_name = (char *) calloc(1, LTOATTRIBID_USR_MED_TXT_LABEL_LEN);

		if (vol_name && strlen(vol_name)) {
			if (strlen(vol_name) > (LTOATTRIBID_USR_MED_TXT_LABEL_LEN - 1)) {
				strncpy(volume_name, vol_name,
						(LTOATTRIBID_USR_MED_TXT_LABEL_LEN - 1));
			} else {
				strncpy(volume_name, vol_name, strlen(vol_name));
			}
		}

		buf = (unsigned char *) calloc(1,
				(LTOATTRIBID_USR_MED_TXT_LABEL_LEN + ATTRIB_HEADER_LEN));
		buf[0] = (unsigned char) (LTOATTRIBID_USR_MED_TXT_LABEL >> 8);
		buf[1] = (unsigned char) (LTOATTRIBID_USR_MED_TXT_LABEL & 0xFF);
		buf[2] = 2; /* format = text (10b) */
		buf[3] = 0; /* attrib len is two bytes but only need one */

		if (format == TC_FORMAT_DEFAULT) {
			buf[4] = 0; /* set length to zero to delete the attribute */
			len = ATTRIB_HEADER_LEN;
		} else {
			buf[4] = LTOATTRIBID_USR_MED_TXT_LABEL_LEN;
			srclen = strlen(volume_name);
			len = (srclen > LTOATTRIBID_USR_MED_TXT_LABEL_LEN) ?
					LTOATTRIBID_USR_MED_TXT_LABEL_LEN : srclen;
			strncpy((char*) buf + 5, volume_name, len);
			len = LTOATTRIBID_USR_MED_TXT_LABEL_LEN + ATTRIB_HEADER_LEN;
		}

		status = ltotape_write_attribute(device, (const tape_partition_t) 0,
				buf, len);

		/* Cleanup. */
		if (buf) {
			free(buf);
			buf = NULL;
		}

		if (volume_name) {
			free(volume_name);
			volume_name = NULL;
		}

		if (status < 0) {
			ltfsmsg(LTFS_WARN, 20024W,
					LTOATTRIBID_USR_MED_TXT_LABEL, status);
			ret = status;
		}

		/* Set Volume Lock State: */
		if (lockbit != NOLOCK_MAM) {

			buf = (unsigned char *) calloc(1,
					(LTOATTRIBID_VOL_LOCK_STATE_LEN + ATTRIB_HEADER_LEN));
			memset(buf, (int) 0x20, (LTOATTRIBID_VOL_LOCK_STATE_LEN + ATTRIB_HEADER_LEN));
			buf[0] = (unsigned char) (LTOATTRIBID_VOL_LOCK_STATE >> 8);
			buf[1] = (unsigned char) (LTOATTRIBID_VOL_LOCK_STATE & 0xFF);
			buf[2] = 0; /* format = binary */
			buf[3] = 0; /* attrib len is two bytes but only need one */

			if (format == TC_FORMAT_DEFAULT) {
				buf[4] = 0; /* set length to zero to delete the attribute */
				len = ATTRIB_HEADER_LEN;

			} else {
				buf[4] = LTOATTRIBID_VOL_LOCK_STATE_LEN;
				buf[5] = (unsigned char) lockbit;
				len = LTOATTRIBID_VOL_LOCK_STATE_LEN + ATTRIB_HEADER_LEN;
			}

			status = ltotape_write_attribute(device, (const tape_partition_t) 0,
					buf, len);

			/* Cleanup. */
			if (buf) {
				free(buf);
				buf = NULL;
			}
			if (status < 0) {
				ltfsmsg(LTFS_WARN, 20024W,
						LTOATTRIBID_VOL_LOCK_STATE, status);
				ret = status;
			}
		}
		/* Set volume uuid */
		if (vol_mam_uuid && strlen(vol_mam_uuid)) {
			volume_mam_uuid = (char *) calloc(1, LTOATTRIBID_VOL_UUID_LEN);

			if (strlen(vol_mam_uuid) > (LTOATTRIBID_VOL_UUID_LEN)) {
				strncpy(volume_mam_uuid, vol_mam_uuid,
						(LTOATTRIBID_VOL_UUID_LEN));
			} else {
				strncpy(volume_mam_uuid, vol_mam_uuid, strlen(vol_mam_uuid));
			}

			buf = (unsigned char *) calloc(1,
					(LTOATTRIBID_VOL_UUID_LEN + ATTRIB_HEADER_LEN));
			memset(buf, (int) 0x20, (LTOATTRIBID_VOL_UUID_LEN + ATTRIB_HEADER_LEN));
			buf[0] = (unsigned char) (LTOATTRIBID_VOL_UUID >> 8);
			buf[1] = (unsigned char) (LTOATTRIBID_VOL_UUID & 0xFF);
			buf[2] = 0; /* format = binary */
			buf[3] = 0; /* attrib len is two bytes but only need one */

			if (format == TC_FORMAT_DEFAULT) {
				buf[4] = 0; /* set length to zero to delete the attribute */
				len = ATTRIB_HEADER_LEN;
			} else {
				buf[4] = LTOATTRIBID_VOL_UUID_LEN;
				srclen = strlen(volume_mam_uuid);
				len = (srclen > LTOATTRIBID_VOL_UUID_LEN) ?
						LTOATTRIBID_VOL_UUID_LEN : srclen;
				strncpy((char*) buf + 5, volume_mam_uuid, len);
				len = LTOATTRIBID_VOL_UUID_LEN + ATTRIB_HEADER_LEN;
			}

			status = ltotape_write_attribute(device, (const tape_partition_t) 0,
					buf, len);

			/* Cleanup. */
			if (buf) {
				free(buf);
				buf = NULL;
			}

			if (volume_mam_uuid) {
				free(volume_mam_uuid);
				volume_mam_uuid = NULL;
			}

			if (status < 0) {
				ltfsmsg(LTFS_WARN, 20024W,
						LTOATTRIBID_VOL_UUID, status);
				/* This attribute is optional (Even if failed to write this
				   should not throw error) hence printing a warning and returning
				   the status as good */
				ret = 0;
			}
		}

		break;
	}
	return ret;
}

/**------------------------------------------------------------------------**
 * Get cartridge health information
 * @param device a pointer to the ltotape backend
 * @param cart_health a pointer to a struct of health-reporting params
 * @return 0 on success or a negative value on error
 */
int ltotape_get_cartridge_health(void *device, struct tc_cartridge_health *cart_health)
{
	unsigned char logdata[LOGSENSEPAGE];
	unsigned char buf[16];
	int param_size = 0, i = 0;
	int rc = 0;
	uint64_t loghlt;

	/* "Tape Efficiency" is not supported: */
	cart_health->tape_efficiency = UNSUPPORTED_CARTRIDGE_HEALTH;

	/* Read the Volume Statistics log page, defaulting everything to unsupported in case the
	 * command fails: */
	cart_health->mounts = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->written_ds = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->write_temps = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->write_perms = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_ds = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_temps = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_perms = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->write_perms_prev = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_perms_prev = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->written_mbytes = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_mbytes = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->passes_begin = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->passes_middle = UNSUPPORTED_CARTRIDGE_HEALTH;

	rc = ltotape_logsense(device, LOG_PAGE_VOLUMESTATS, logdata, LOGSENSEPAGE);
	if (rc) {
		ltfsmsg(LTFS_ERR, 12135E, LOG_PAGE_VOLUMESTATS, rc);

	} else {
		for (i = 0; i < (int) ((sizeof(volstats) / sizeof(volstats[0]))); i++) {
			if (parse_logPage(logdata, volstats[i], &param_size, buf, 16)) {
				ltfsmsg(LTFS_ERR, 12136E);

			} else {
				switch (param_size) {
				case sizeof(uint8_t):
					loghlt = (uint64_t) (buf[0]);
					break;
				case sizeof(uint16_t):
					loghlt = ((uint64_t) buf[0] << 8) + (uint64_t) buf[1];
					break;
				case sizeof(uint32_t):
					loghlt = ((uint64_t) buf[0] << 24)
							+ ((uint64_t) buf[1] << 16)
							+ ((uint64_t) buf[2] << 8) + (uint64_t) buf[3];
					break;
				case sizeof(uint64_t):
					loghlt = ((uint64_t) buf[0] << 56)
							+ ((uint64_t) buf[1] << 48)
							+ ((uint64_t) buf[2] << 40)
							+ ((uint64_t) buf[3] << 32)
							+ ((uint64_t) buf[4] << 24)
							+ ((uint64_t) buf[5] << 16)
							+ ((uint64_t) buf[6] << 8) + (uint64_t) buf[7];
					break;
				default:
					loghlt = UNSUPPORTED_CARTRIDGE_HEALTH;
					break;
				}

				switch (volstats[i]) {
				case VOLSTATS_MOUNTS:
					cart_health->mounts = loghlt;
					break;
				case VOLSTATS_WRITTEN_DS:
					cart_health->written_ds = loghlt;
					break;
				case VOLSTATS_WRITE_TEMPS:
					cart_health->write_temps = loghlt;
					break;
				case VOLSTATS_WRITE_PERMS:
					cart_health->write_perms = loghlt;
					break;
				case VOLSTATS_READ_DS:
					cart_health->read_ds = loghlt;
					break;
				case VOLSTATS_READ_TEMPS:
					cart_health->read_temps = loghlt;
					break;
				case VOLSTATS_READ_PERMS:
					cart_health->read_perms = loghlt;
					break;
				case VOLSTATS_WRITE_PERMS_PREV:
					cart_health->write_perms_prev = loghlt;
					break;
				case VOLSTATS_READ_PERMS_PREV:
					cart_health->read_perms_prev = loghlt;
					break;
				case VOLSTATS_WRITE_MB:
					cart_health->written_mbytes = loghlt;
					break;
				case VOLSTATS_READ_MB:
					cart_health->read_mbytes = loghlt;
					break;
				case VOLSTATS_PASSES_BEGIN:
					cart_health->passes_begin = loghlt;
					break;
				case VOLSTATS_PASSES_MIDDLE:
					cart_health->passes_middle = loghlt;
					break;
				default:
					break;
				}
			}
		}
	}

	return 0;
}

/**------------------------------------------------------------------------**
 * Get tape alert information
 * @param device a pointer to the ltotape backend
 * @param taflags a pointer to a 64-bit uint map of the TapeAlert flags
 * @return 0 on success or a negative value on error
 */
int ltotape_get_tape_alert (void *device, uint64_t* taflags)
{
	unsigned char logdata[LOGSENSEPAGE];
	unsigned char buf[16];
	int param_size, i;
	int rc;

	/*
	 * Read the Tape Alert flags log page:
	 */
	*taflags = 0;

	rc = ltotape_logsense(device, LOG_PAGE_TAPE_ALERT, logdata, LOGSENSEPAGE);
	if (rc) {
		ltfsmsg(LTFS_ERR, 12135E, LOG_PAGE_TAPE_ALERT, rc);

	} else {
		for (i = 1; i <= 64; i++) {
			if (parse_logPage(logdata, (uint16_t) i, &param_size, buf, 16) ||
					(param_size != sizeof(uint8_t))) {
				ltfsmsg(LTFS_ERR, 12136E);
				rc = -2;
			}

			if (buf[0]) {
				*taflags += (uint64_t)(1) << (i - 1);
			}
		}
	}

	return rc;
}

/**
 * clear latched tape alert from the drive.
 *
 * @param device A pointer to the ltotape backend.
 * @param tape_alert The tape alert to be cleared.
 * @return int DEVICE_GOOD on success, a -ve value on error.
 */
int ltotape_clear_tape_alert(void *device, uint64_t tape_alert)
{
	int						rc = DEVICE_GOOD;
	ltotape_scsi_io_type	*sio = (ltotape_scsi_io_type *) device;

	CHECK_ARG_NULL(sio, -EDEV_INVALID_ARG);

	return rc;
}

/**
 * Get vendor unique backend xattr
 * @param device A pointer to the ltotape backend.
 * @param name Name of xattr
 * @param buf On success, fill this value with the pointer of data buffer for
 * xattr
 * @return DEVICE_GOOD on success or a negative value on error.
 */
int ltotape_get_xattr(void *device, const char *name, char **buf)
{
	/* At this time, no vendor unique xattr is supported for read */
	return -LTFS_NO_XATTR;
}

/**
 * Get vendor unique backend xattr
 * @param device Device handle returned by the backend's open().
 * @param name Name of xattr
 * @param buf Data buffer to set the value
 * @param size Length of data buffer
 * @return DEVICE_GOOD on success or a negative value on error.
 */
int ltotape_set_xattr(void *device, const char *name, const char *buf, size_t size)
{
	/* At this time, no vendor unique xattr is supported for write */
	return -LTFS_NO_XATTR;
}

/**------------------------------------------------------------------------**
 * Try to determine the status of EOD in the specified partition
 * @param device a pointer to the ltotape backend
 * @param part which partition to investigate
 * @return EOD_GOOD, EOD_MISSING, or EOD_UNKNOWN if we can't tell
 */
int ltotape_get_eod_status(void *device, int part)
{
	unsigned char logdata[LOGSENSEPAGE];
	unsigned char buf[16];
	int param_size = 0, rc = 0;
	unsigned int i = 0;
	uint32_t part_cap[2] = { EOD_UNKNOWN, EOD_UNKNOWN };
	static int done_report = 0;

	/*
	 * Read the Volume Statistics log page:
	 */
	rc = ltotape_logsense(device, LOG_PAGE_VOLUMESTATS, logdata, LOGSENSEPAGE);
	if (rc) {
		ltfsmsg(LTFS_WARN, 12170W, LOG_PAGE_VOLUMESTATS, rc);
		return EOD_UNKNOWN;
	}

	/*
	 * Check if the drive f/w has been updated to fully support the required param;
	 *  if not, all we know for sure is that we don't know for sure.  But if we report
	 *  EOD_UNKNOWN, the user will be presented with multiple warning messages about
	 *  unable to check EOD status, which in the vast majority of cases will be irrelevant..
	 *  So (for now at least) we'll report EOD_GOOD and hope it works out ok...
	 */
	/* LTO7 and LTO8 drives do not support this log parameter but the firmware supports the required
	 * features so this check is not necessary for LTO7 and LTO8 drives
	 */
	if ((((ltotape_scsi_io_type *)device)->type != drive_lto7) && (((ltotape_scsi_io_type *)device)->type != drive_lto8)) {
		if (parse_logPage(logdata, (uint16_t) VOLSTATS_VU_PGFMTVER, &param_size,
				buf, 2) == -1) {
			if (!done_report) {
				ltfsmsg(LTFS_DEBUG, 20097D);
				done_report = 1;
			}
			return EOD_GOOD;
		}
	}

	/*
	 * Find & extract the "Approximate used native capacity of partitions" param (0x203):
	 */
	if ((parse_logPage(logdata, (uint16_t) VOLSTATS_USED_CAPACITY, &param_size,
			buf, 16)) || (param_size != sizeof(buf))) {
		ltfsmsg(LTFS_WARN, 12171W);
		return EOD_UNKNOWN;
	}

	i = 0;
	while (i < sizeof(buf)) {
		unsigned char len;
		uint16_t part_buf;

		len = buf[i];
		part_buf = (uint16_t) (buf[i + 2] << 8) + (uint16_t) buf[i + 3];

		if (((len - LOG_PAGE_VOL_PART_HEADER_SIZE + 1) == sizeof(uint32_t))
				&& (part_buf < 2)) {
			part_cap[part_buf] = ((uint32_t) buf[i + 4] << 24)
					+ ((uint32_t) buf[i + 5] << 16)
					+ ((uint32_t) buf[i + 6] << 8) + (uint32_t) buf[i + 7];
		} else {
			ltfsmsg(LTFS_WARN, 12172W, i, part_buf, len);
		}

		i += (len + 1);
	}

	return (part_cap[part] == 0xFFFFFFFF) ? EOD_MISSING : EOD_GOOD;
}

/**------------------------------------------------------------------------**
 * Print out options specific to this backend
 * @return Nothing
 */
void ltotape_help_message(void)
{
	if (! strcmp(getprogname(), "ltfs")) {
		fprintf(stderr,
				"LTOTAPE backend options:\n"
				"    -o devname=<dev>          tape device (default=%s)\n"
				"    -o log_directory=<dir>    log snapshot directory (default=%s)\n"
				"    -o nosizelimit            remove 512kB limit (NOT RECOMMENDED)\n\n",
				ltotape_default_device,
				ltotape_get_default_snapshotdir());
	} else {
		fprintf(stderr,
				"LTOTAPE backend options:\n"
				"  -o log_directory=<dir>      log snapshot directory (default=%s)\n"
				"  -o nosizelimit              remove 512kB limit (NOT RECOMMENDED)\n",
				ltotape_get_default_snapshotdir());
	}
}

/**------------------------------------------------------------------------**
 * Return the name of the default device for this backend
 * @return pointer to device name
 */
const char *ltotape_default_device_name(void)
{
	return ltotape_default_device;
}

int ltotape_set_key(void *device, const unsigned char *keyalias,
		const unsigned char *key)
{
	return 0;
}

int ltotape_get_keyalias(void *device, unsigned char **keyalias)
{
	return 0;
}

int ltotape_takedump_drive(void *device, bool capture_unforced)
{
	return 0;
}

int ltotape_is_mountable(void *device, const char *barcode,
		const unsigned char cart_type, const unsigned char density_code)
{
	return 1;
}

/**
 * Updating the MAM attributes.
 * @param device Pointer to ltotape backend.
 * @param FORMAT The format type
 * @param vol_name An optional volume name to the tape.
 * @param barcode_name The volume barcode name
 * @param lockbit volume lock state bit to be set in MAM
 * @return 0 on success, a negative value on error.
 */
int ltotape_update_mam_attr(void *device, TC_FORMAT_TYPE format, const char *vol_name,
							unsigned int attribute_id, const char *barcode_name, mam_lockval lockbit) {

	int status = -1;

	status = ltotape_set_MAMattributes(device, format, vol_name, attribute_id, barcode_name, lockbit,
			NULL);

	return status;
}

/**
 * Check if the loaded carridge is WORM.
 * @param device Device handle returned by the backend's open().
 * @param is_worm Pointer to worm status.
 * @return 0 on success or a negative value on error.
 */
int ltotape_get_worm_status(void *device, bool *is_worm)
{
	int status = 0;

	*is_worm = false;

	return status;
}

int ltotape_get_serialnumber(void *device, char **result)
{
	ltotape_scsi_io_type	*sio = (ltotape_scsi_io_type *) device;

	CHECK_ARG_NULL(device, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(result, -LTFS_NULL_ARG);

	*result = strdup((const char *) sio->serialno);
	if (! *result) {
		ltfsmsg(LTFS_ERR, 10001E, "ltotape_get_serialnumber: result");
		return -EDEV_NO_MEMORY;
	}

	return 0;
}

int ltotape_set_profiler(void *device, char *work_dir, bool enable) 
{
	printf("uninmplemente\n");
	abort();
	return 0;
}

int ltotape_get_block_in_buffer(void *device, uint32_t *block)
{
	int                                             status = 0;
	ltotape_scsi_io_type	*sio = (ltotape_scsi_io_type *) device;
	unsigned char buf[REDPOS_EXT_LEN];

	memset(buf, 0, sizeof(buf));

	/* Build CDB */
	sio->cdb[0] = READ_POSITION;
	sio->cdb[1] = 0x08; /* Extended Format */
	sio->cdb_length = 6;

        /* Set up the data part: */
        sio->data = buf;
        sio->data_length = sizeof(buf);
        sio->data_direction = HOST_READ;

	sio->timeout_ms = (sio->family == drivefamily_lto) ?
                        LTO_READ_TIMEOUT : DAT_READ_TIMEOUT;

	status = ltotape_scsiexec (sio);
	
	if (status == 0) {
		*block = (buf[5] << 16) + (buf[6] << 8) + (int)buf[7];

		ltfsmsg(LTFS_DEBUG, 30398D, "blocks-in-buffer",
				(unsigned long long)*block, (unsigned long long)0, (unsigned long long)0, sio->serialno);
	}

	return status;
}

int ltotape_is_readonly(void *device)
{
	int status;
	struct tc_current_param drive_param;

	status = ltotape_get_parameters(device, &drive_param);
	if (!status)
		return status;

	return drive_param.write_protected;
}

/**
 * Return the name of the messages facility for this backend
 * @param message_data pointer to source of message text
 * @return pointer to facility name
 */
extern char driver_ltotape_dat[];
const char *tape_dev_get_message_bundle_name(void **message_data)
{
	*message_data = driver_ltotape_dat;
	return "driver_ltotape";
}


/**
 * Finally declare the set of operations defined by this backend
 * and provide a function to access the structure:
 */
struct tape_ops ltotape_drive_handler = {
	.open                   = ltotape_open,
	.reopen                 = ltotape_reopen,
	.close                  = ltotape_close,
	.close_raw              = ltotape_close_raw,
	.is_connected           = ltotape_is_connected,
	.inquiry                = ltotape_inquiry,
	.inquiry_page           = ltotape_inquiry_page,
	.test_unit_ready        = ltotape_test_unit_ready,
	.read                   = ltotape_read,
	.write                  = ltotape_write,
	.writefm                = ltotape_writefm,
	.rewind                 = ltotape_rewind,
	.locate                 = ltotape_locate,
	.space                  = ltotape_space,
	.erase                  = ltotape_erase,
	.load                   = ltotape_load,
	.unload                 = ltotape_unload,
	.readpos                = ltotape_readposition,
	.setcap                 = ltotape_setcap,
	.format                 = ltotape_format,
	.remaining_capacity     = ltotape_remaining_capacity,
	.logsense               = ltotape_logsense,
	.modesense              = ltotape_modesense,
	.modeselect             = ltotape_modeselect,
	.reserve_unit           = ltotape_reserve_unit,
	.release_unit           = ltotape_release_unit,
	.prevent_medium_removal = ltotape_prevent_medium_removal,
	.allow_medium_removal   = ltotape_allow_medium_removal,
	.read_attribute         = ltotape_read_attribute,
	.write_attribute        = ltotape_write_attribute,
	.allow_overwrite        = ltotape_allow_overwrite,
	.set_compression        = ltotape_set_compression,
	.set_default            = ltotape_set_default,
	.get_cartridge_health   = ltotape_get_cartridge_health,
	.get_tape_alert         = ltotape_get_tape_alert,
	.clear_tape_alert       = ltotape_clear_tape_alert,
	.get_xattr              = ltotape_get_xattr,
	.set_xattr              = ltotape_set_xattr,
	.get_eod_status         = ltotape_get_eod_status,
	.get_parameters         = ltotape_get_parameters,
	.get_device_list        = ltotape_get_device_list,
	.help_message           = ltotape_help_message,
	.parse_opts             = ltotape_parse_opts,
	.default_device_name    = ltotape_default_device_name,
	.set_key                = ltotape_set_key,
	.get_keyalias           = ltotape_get_keyalias,
	.takedump_drive         = ltotape_takedump_drive,
	.is_mountable           = ltotape_is_mountable,
	.get_worm_status        = ltotape_get_worm_status,
	.get_serialnumber       = ltotape_get_serialnumber,
	.set_profiler           = ltotape_set_profiler,
	.get_block_in_buffer    = ltotape_get_block_in_buffer,
	.is_readonly            = ltotape_is_readonly,
};

struct tape_ops *tape_dev_get_ops(void)
{
	return &ltotape_drive_handler;
}

#undef __ltotape_c
