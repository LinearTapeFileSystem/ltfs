/*
**
**  OS_Copyright_BEGIN
**
**
**  Copyright 2010, 2018 IBM Corp. All rights reserved.
**  Copyright (c) 2013-2018 Spectra Logic Corporation. All rights reserved.
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
**  OS_Copyright_END
**
*************************************************************************************
**
** COMPONENT NAME:  IBM Linear Tape File System
**
** FILE NAME:       tape_drivers/freebsd/cam/camtape_tc.c
**
** DESCRIPTION:     Implements FreeBSD CAM backend for LTFS.
**
** AUTHORS:         Atsushi Abe
**                  IBM Yamato, Japan
**                  PISTE@jp.ibm.com
**
**                  Ken Merry
**                  Spectra Logic Corporation
**                  ken@FreeBSD.ORG, kenm@spectralogic.com
**
*************************************************************************************
*/

#define __camtape_tc_c

#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/queue.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include <ctype.h>
#include <bsdxml.h>
#include <mtlib.h>
#include <cam/scsi/scsi_message.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "libltfs/ltfs_fuse_version.h"
#include "libltfs/arch/time_internal.h"
#include <fuse.h>

#include "libltfs/ltfslogging.h"

#include "tape_drivers/tape_drivers.h"
#include "tape_drivers/ibm_tape.h"

#define TAPE_BACKEND /* Use static vriable definition in tape_drivers.h */
#include "cam_cmn.h"
#include "reed_solomon_crc.h"
#include "crc32c_crc.h"

/*
 * Prototypes for opening the SA (Sequential Access) and pass (Pass Through)
 * device drivers on FreeBSD
*/
int open_sa_pass(struct camtape_data *softc, const char *saDeviceName);

int open_sa_device(struct camtape_data *softc, const char* saDeviceName);
void close_sa_device(struct camtape_data *softc);

void close_cd_pass_device(struct camtape_data *softc);

/*
 * Default tape device
 */
const char *camtape_default_device = "/dev/sa0";

/*
 * Default changer device
 */
const char *camtape_default_changer_device = "/dev/ch0";

/*
 *  Definitions
 */
#define LOG_PAGE_HEADER_SIZE      (4)
#define LOG_PAGE_PARAMSIZE_OFFSET (3)
#define LOG_PAGE_PARAM_OFFSET     (4)

#define LINUX_MAX_BLOCK_SIZE (1 * MB)
#define	LTFS_CRC_LEN			  4
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define CRC32C_CRC (0x02)

/*
 *  Global values
 */
struct camtape_global_data global_data;
struct error_table *standard_table;
struct error_table *vendor_table;

/*
 *  Forward reference
 */
int camtape_readpos(void *device, struct tc_position *pos);
int camtape_rewind(void *device, struct tc_position *pos);
int camtape_modesense(void *device, const uint8_t page, const TC_MP_PC_TYPE pc, const uint8_t subpage,
					  unsigned char *buf, const size_t size);
int camtape_set_key(void *device, const unsigned char * const keyalias, const unsigned char * const key);
int camtape_set_lbp(void *device, bool enable);

/*
 *  Local Functions
 */

/**
 * Parse Log page contents
 * @param logdata pointer to logdata buffer to parse. including log sense header is expected
 * @param param parameter id to fetch
 * @param param_size size of value to fetch
 * @param buf pointer to the buffer to filled in teh fetched value. this function will update this value.
 * @param bufsize size of the buffer
 * @return 0 on success or negative value on error
 */
int parse_logPage(const unsigned char *logdata, const uint16_t param, int *param_size,
								unsigned char *buf, const size_t bufsize)
{
	uint16_t page_len, param_code, param_len;
	long i;

	page_len = ltfs_betou16(logdata + 2);
	i = LOG_PAGE_HEADER_SIZE;

	while (i < page_len) {
		param_code = ltfs_betou16(logdata + i);
		param_len = (uint16_t) logdata[i + LOG_PAGE_PARAMSIZE_OFFSET];
		if (param_code == param) {
			*param_size = param_len;
			if (bufsize < param_len) {
				ltfsmsg(LTFS_INFO, 31218I, bufsize, i + LOG_PAGE_PARAM_OFFSET);
				memcpy(buf, &logdata[i + LOG_PAGE_PARAM_OFFSET], bufsize);
				return -2;
			}
			else {
				memcpy(buf, &logdata[i + LOG_PAGE_PARAM_OFFSET], param_len);
				return 0;
			}
		}
		i += param_len + LOG_PAGE_PARAM_OFFSET;
	}

	return -1;
}

/**
 * Parse option for CAM tape driver
 * @param devname device name of the LTO tape driver
 * @return a pointer to the camtape backend on success or NULL on error
 */
#define CAMTAPE_OPT(templ,offset,value) { templ, offsetof(struct camtape_global_data, offset), value }

static struct fuse_opt camtape_global_opts[] = {
	CAMTAPE_OPT("autodump",          disable_auto_dump, 0),
	CAMTAPE_OPT("noautodump",        disable_auto_dump, 1),
	CAMTAPE_OPT("scsi_lbprotect=%s", str_crc_checking, 0),
	CAMTAPE_OPT("strict_drive",   strict_drive, 1),
	CAMTAPE_OPT("nostrict_drive", strict_drive, 0),
	FUSE_OPT_END
};

int null_parser(void *device, const char *arg, int key, struct fuse_args *outargs)
{
	return 1;
}

int camtape_parse_opts(void *device, void *opt_args)
{
	struct fuse_args *args = (struct fuse_args *) opt_args;
	struct camtape_data *softc = (struct camtape_data *)device;
	int ret;

	/* fuse_opt_parse can handle a NULL device parameter just fine */
	ret = fuse_opt_parse(args, &global_data, camtape_global_opts, null_parser);
	if (ret < 0) {
		ltfsmsg(LTFS_INFO, 31219I, ret);
		ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_PARSEOPTS));
		return ret;
	}

	/* Validate scsi logical block protection */
	if (global_data.str_crc_checking) {
		if (strcasecmp(global_data.str_crc_checking, "on") == 0)
			global_data.crc_checking = 1;
		else if (strcasecmp(global_data.str_crc_checking, "off") == 0)
			global_data.crc_checking = 0;
		else {
			ltfsmsg(LTFS_ERR, 31220E, global_data.str_crc_checking);
			ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_PARSEOPTS));
			return -EINVAL;
		}
	} else
		global_data.crc_checking = 0;

	return 0;
}

/**
 * Open CAM tape backend.
 * @param devname device name of the LTO tape driver
 * @param[out] handle contains the handle to the CAM tape backend on success
 * @return 0 on success or a negative value on error
 */
int camtape_open(const char *devname, void **handle)
{
	struct camtape_data *softc = NULL;
	int drive_type = DRIVE_UNSUPPORTED;
	char vendor[10];
	char product[20];
	int ret;

	CHECK_ARG_NULL(handle, -LTFS_NULL_ARG);
	*handle = NULL;

	/*
	 * XXX KDM check to see whether the sa(4) driver has required features.
	 * ret =  camtape_check_lin_tape_version();
	 * if (ret != DEVICE_GOOD) {
	 * return ret;
 	 *  }
	 */

	ltfsmsg(LTFS_INFO, 31223I, devname);

	softc = calloc(1, sizeof(struct camtape_data));
	if (! softc) {
		ltfsmsg(LTFS_ERR, 10001E, "camtape_open: device private data");
		return -EDEV_NO_MEMORY;
	}

	ret = open_sa_pass(softc, devname);
	if (ret) {
		free(softc);
		return ret;
	}

	cam_strvis((uint8_t *)product, (uint8_t *)softc->cd->inq_data.product,
	    sizeof(softc->cd->inq_data.product), sizeof(product));
	cam_strvis((uint8_t *)vendor, (uint8_t *)softc->cd->inq_data.vendor,
	    sizeof(softc->cd->inq_data.vendor), sizeof(vendor));
	ltfsmsg(LTFS_INFO, 31228I, product);
	ltfsmsg(LTFS_INFO, 31229I, vendor);

	/* Check the drive is supportable */
	struct supported_device **cur = ibm_supported_drives;
	while(*cur) {
		if ((! strncmp((char*)softc->cd->inq_data.vendor, (*cur)->vendor_id,
					   strlen((*cur)->vendor_id)) ) &&
		   (! strncmp((char*)softc->cd->inq_data.product, (*cur)->product_id,
					  strlen((*cur)->product_id)) ) ) {
			drive_type = (*cur)->drive_type;
			break;
		}
		cur++;
	}

	if (drive_type != DRIVE_UNSUPPORTED) {
		softc->drive_type = drive_type;

		/* Setup IBM tape specific parameters */
		standard_table = standard_tape_errors;
		vendor_table   = ibm_tape_errors;

		/* Set specific timeout value based on drive type */
		ibm_tape_init_timeout(&softc->timeouts, softc->drive_type);
	} else {
		ltfsmsg(LTFS_INFO, 31230I, softc->cd->inq_data.product);
		close(softc->fd_sa);
		close_cd_pass_device(softc);
		free(softc);
		return -EDEV_DEVICE_UNSUPPORTABLE;
	}

	/* Set drive serial number to private data to put it to the dump file name */
	memset(softc->drive_serial, 0, sizeof(softc->drive_serial));
	memcpy(softc->drive_serial, softc->cd->serial_num, softc->cd->serial_num_len);

	ltfsmsg(LTFS_INFO, 31232I, softc->cd->inq_data.revision);
	if (! ibm_tape_is_supported_firmware(softc->drive_type, (uint8_t *)softc->cd->inq_data.revision)) {
		ltfsmsg(LTFS_INFO, 31230I, "firmware", softc->cd->inq_data.revision);
		close(softc->fd_sa);
		close_cd_pass_device(softc);

		free(softc);
		return -EDEV_UNSUPPORTED_FIRMWARE;
	}

	ltfsmsg(LTFS_INFO, 31233I, softc->drive_serial);

	softc->loaded = false; /* Assume tape is not loaded until a successful load call. */

	softc->clear_by_pc     = false;
	softc->force_writeperm = DEFAULT_WRITEPERM;
	softc->force_readperm  = DEFAULT_READPERM;
	softc->force_errortype = DEFAULT_ERRORTYPE;

	*handle = (void *) softc;
	return DEVICE_GOOD;
}

/**
 * Reopen CAM tape backend
 * @param devname device name of the LTO tape driver
 * @param device a pointer to the camtape backend
 * @return 0 on success or a negative value on error
 */
int camtape_reopen(const char *name, void *vstate)
{
	/* Do nothing */
	return 0;
}

/**
 * Close CAM tape backend
 * @param device a pointer to the camtape backend
 * @return 0 on success or a negative value on error
 */
int camtape_close(void *device)
{
	struct camtape_data *softc = (struct camtape_data *) device;
	struct tc_position pos;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_CLOSE));
	if (softc->loaded)
		camtape_rewind(device, &pos);

	camtape_set_lbp(device, false);

	close(softc->fd_sa);

	close_cd_pass_device(softc);

	ibm_tape_destroy_timeout(&softc->timeouts);

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_CLOSE));

	if (softc->profiler) {
		fclose(softc->profiler);
		softc->profiler = NULL;
	}

	free(softc);
	return 0;
}

/**
 * Close only file descriptor
 * @param device a pointer to the camtape backend
 * @return 0 on success or a negative value on error
 */
int camtape_close_raw(void *device)
{
	struct camtape_data *softc = (struct camtape_data *) device;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_CLOSERAW));
	close(softc->fd_sa);
	softc->fd_sa = -1;

	close_cd_pass_device(softc);

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_CLOSERAW));
	return 0;
}

/**
 * Test if a given tape device is connected to the host
 * @param devname device name of the LTO tape driver
 * @return 0 on success, indicating that the drive is connected to the host,
 *  or a negative value on error.
 */
int camtape_is_connected(const char *devname)
{
	struct stat statbuf;
	int ret = 0;

	/*
	 * We assume that /dev is handled by a daemon such as Udev and that
	 * device entries are automatically removed and added upon hotplug events.
	 */
	ret = stat(devname, &statbuf);
	return ret;
}

int _mt_command(void *device, int cmd, char *cmd_name, int param, char **msg)
{
	struct camtape_data *softc = device;
	int fd = softc->fd_sa;
	struct mtop mt = {.mt_op = cmd,.mt_count = param };
	struct scsi_sense_data sense_data;
	int rc;

start:
	rc = ioctl(fd, MTIOCTOP, &mt);

	if (rc != 0) {
		rc = camtape_ioctlrc2err(softc, fd, &sense_data, /*control_cmd*/ 1, msg);
		if (rc == -EDEV_TIME_STAMP_CHANGED) {
			ltfsmsg(LTFS_DEBUG, 31211D, cmd_name, cmd, rc);
			goto start;
		}
		ltfsmsg(LTFS_INFO, 31208I, cmd_name, cmd, rc, errno, softc->drive_serial);
	}
	else {
		*msg = NULL;
		rc = DEVICE_GOOD;
	}

	return rc;
}

/**
 * Read a record from tape
 * @param device a pointer to the camtape backend
 * @param buf a pointer to read buffer
 * @param count read size
 * @param pos a pointer to position data. This function will update position infomation.
 * @param unusual_size a flag specified unusual size or not
 * @return read length on success or a negative value on error
 */
int camtape_read(void *device, char *buf, size_t count, struct tc_position *pos,
				 const bool unusual_size)
{
	ssize_t len = -1, read_len;
	int rc;
	bool silion = unusual_size;
	char *msg = NULL;
	struct camtape_data *softc = (struct camtape_data *) device;
	int    fd = softc->fd_sa;
	size_t datacount = count;

	/*
	 * TODO: Check count is smaller than max of SSIZE_MAX
	 *       The prototype of read system call is
	 *       ssize_t read(int fd, void *buf, size_t count);
	 */

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_READ));
	ltfsmsg(LTFS_DEBUG3, 31395D, "read", count, softc->drive_serial);

	if (softc->force_readperm) {
		softc->read_counter++;
		if (softc->read_counter > softc->force_readperm) {
			ltfsmsg(LTFS_INFO, 31234I, "read");
			ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READ));
			if (softc->force_errortype)
				return -EDEV_NO_SENSE;
			else
				return -EDEV_READ_PERM;
		}
	}

	read_len = read(fd, buf, datacount);

	if ((!silion && (size_t)read_len != datacount) || (read_len <= 0)) {
		struct scsi_sense_data sense_data;
		uint8_t stream_bits;

		/* XXX KDM need to pass back valid length of sense data */
		rc = camtape_ioctlrc2err(softc, fd , &sense_data, /*control_cmd*/ 0, &msg);

		if (scsi_get_stream_info(&sense_data, sizeof(sense_data), &softc->cd->inq_data,
								 &stream_bits) != 0) {
			stream_bits = 0;
		}

		switch (rc) {
		case -EDEV_NO_SENSE:
			if (stream_bits & SSD_FILEMARK) {
				/* Filemark Detected */
				ltfsmsg(LTFS_DEBUG, 31236D);
				rc = DEVICE_GOOD;
				pos->block++;
				pos->filemarks++;
				len = 0;
			}
			else if (stream_bits & SSD_ILI) {
				/* Illegal Length */
				int64_t diff_len;
				uint64_t unsigned_diff_len;

				/* XXX KDM need to get the real length of the sense data */
				if (scsi_get_sense_info(&sense_data, sizeof(sense_data), SSD_DESC_INFO,
					&unsigned_diff_len, &diff_len) != 0) {

					/* XXX KDM what do we do if there is no info field? */
					diff_len = 0;
				}

				if (diff_len < 0) {
					ltfsmsg(LTFS_INFO, 31237I, diff_len, count - diff_len); // "Detect overrun condition"
					rc = -EDEV_OVERRUN;
				}
				else {
					ltfsmsg(LTFS_DEBUG, 31238D, diff_len, count - diff_len); // "Detect underrun condition"
					len = count - diff_len;
					rc = DEVICE_GOOD;
					pos->block++;
				}
			}
			else if (errno == EOVERFLOW) {
				ltfsmsg(LTFS_INFO, 31237I, count - read_len, read_len); // "Detect overrun condition"
				rc = -EDEV_OVERRUN;
			}
			else if ((size_t)read_len < count) {
				ltfsmsg(LTFS_DEBUG, 31238D, count - read_len, read_len); // "Detect underrun condition"
				len = read_len;
				rc = DEVICE_GOOD;
				pos->block++;
			}
			break;
		case -EDEV_FILEMARK_DETECTED:
			ltfsmsg(LTFS_DEBUG, 31236D);
			rc = DEVICE_GOOD;
			pos->block++;
			pos->filemarks++;
			len = 0;
			break;
		}

		if (rc != DEVICE_GOOD) {
			if ((rc != -EDEV_CRYPTO_ERROR && rc != -EDEV_KEY_REQUIRED) || ((struct camtape_data *) device)->is_data_key_set) {
				ltfsmsg(LTFS_INFO, 31208I, "READ", count, rc, errno, ((struct camtape_data *) device)->drive_serial);
				camtape_process_errors(device, rc, msg, "read", true);
			}
			len = rc;
		}
	}
	else {
		len = silion ? read_len : (ssize_t)datacount;
		pos->block++;
	}

	if(global_data.crc_checking && len > 4) {
		if (softc->f_crc_check)
			len = softc->f_crc_check(buf, len - 4);
		if (len < 0) {
			ltfsmsg(LTFS_ERR, 31239E);
			len = -EDEV_LBP_READ_ERROR;
		}
	}

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READ));
	return len;
}

/**
 * Write a record to tape
 * When the drive detect early warning condition, this function will return {-ENOSPC, true}
 *
 * @param device a pointer to the camtape backend
 * @param buf a pointer to read buffer
 * @param count write size
 * @param pos a pointer to position data. This function will update position infomation.
 * @param unusual_size a flag specified unusual size or not
 * @return rc 0 on success or a negative value on error
 */
int camtape_write(void *device, const char *buf, size_t count, struct tc_position *pos)
{
	int rc = -1;
	ssize_t written;
	char *msg = NULL;
	struct scsi_sense_data sense_data;
	int write_retry_done = 0;

	struct camtape_data *softc = (struct camtape_data *) device;
	int    fd = softc->fd_sa;
	size_t     datacount = count;

	/*
	 * TODO: Check count is smaller than max of SSIZE_MAX
	 *       The prototype of write system call is
	 *       ssize_t write(int fd, const void *buf, size_t count);
	 */

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_WRITE));
	ltfsmsg(LTFS_DEBUG, 31395D, "write", count, ((struct camtape_data *) device)->drive_serial);

	if ( softc->force_writeperm ) {
		softc->write_counter++;
		if ( softc->write_counter > softc->force_writeperm ) {
			ltfsmsg(LTFS_INFO, 31234I, "write");
			ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_WRITE));
			if (softc->force_errortype)
				return -EDEV_NO_SENSE;
			else
				return -EDEV_WRITE_PERM;
		} else if ( softc->write_counter > (softc->force_writeperm - THRESHOLD_FORCE_WRITE_NO_WRITE)) {
			ltfsmsg(LTFS_INFO, 31235I);
			pos->block++;
			ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_WRITE));
			return DEVICE_GOOD;
		}
	}

	/* Invoke _ioctl to Write */
	if(global_data.crc_checking) {
		if (softc->f_crc_enc)
			softc->f_crc_enc((void *)buf, count);
		datacount = count + 4;
	}

retry_write:

	errno = 0;

	/*
	 * Note that we assume here that our write cannot be broken into multiple pieces.  If it
	 * can, then we may have indeterminate results because it isn't possible to get both an
	 * error and a residual count here.  We get one or the other.
	 */
	written = write(fd, buf, datacount);
	if ((size_t)written != datacount) {
		ltfsmsg(LTFS_INFO, 31208I, "WRITE", count, written, errno, softc->drive_serial);

		if (written == -1) {
			/*
			 * We only get an error when a write actually fails.  The Linux backend also checks
			 * for ENOMEM and retries a number of times.  Note that ENOMEM is returned for
			 * memory allocation failures inside the Linux kernel, and isn't returned for I/O
			 * failures from the tape drive.  In FreeBSD, if any memory allocation is needed in
			 * the write path, we do a blocking allocation that will sleep until it has memory.
			 * So, the only failures we'll see here are failures that come from the tape drive.
			 */
			rc = camtape_ioctlrc2err(softc, fd , &sense_data, /*control_cmd*/ 0, &msg);
		} else {
			/*
			 * Short write.  This means that we hit early warning.  Grab the position to see
			 * what actually happened, and then retry the write.  If we've already done one
			 * retry, try to grab the sense data and return an error from that.
			 */
			camtape_readpos(device, pos);
			if (write_retry_done == 0) {
				write_retry_done = 1;
				goto retry_write;
			} else
				rc = camtape_ioctlrc2err(softc, fd , &sense_data, /*control_cmd*/ 0, &msg);
		}

		if (rc != DEVICE_GOOD)
			camtape_process_errors(device, rc, msg, "write", true);

		if (rc == -EDEV_LBP_WRITE_ERROR)
			ltfsmsg(LTFS_ERR, 31247E);
	} else {
		rc = DEVICE_GOOD;
		pos->block++;
	}

	softc->dirty_acq_loss_w = true;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_WRITE));
	return rc;
}

/**
 * Write filemark(s) to tape
 * @param device a pointer to the camtape backend
 * @param count count to write filemark. If 0 only flush.
 * @param pos a pointer to position data. This function will update position infomation.
 * @return rc 0 on success or a negative value on error
 */
int camtape_writefm(void *device, size_t count, struct tc_position *pos, bool immed)
{
	int rc = -1;
	char *msg = NULL;
	size_t written_count;
	tape_filemarks_t cur_fm = pos->filemarks;
	struct camtape_data *softc = (struct camtape_data *) device;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_WRITEFM));
	ltfsmsg(LTFS_DEBUG, 31396D, "writefm", count, softc->drive_serial);

start_wfm:
	errno = 0;
	rc = _mt_command(device, immed ? MTWEOFI : MTWEOF, "WRITE FM", count, &msg);
	camtape_readpos(device, pos);

	if (rc != DEVICE_GOOD) {
		switch (rc) {
		case -EDEV_EARLY_WARNING:
			ltfsmsg(LTFS_WARN, 31245W, "writefm");
			rc = DEVICE_GOOD;
			pos->early_warning = true;
			break;
		case -EDEV_PROG_EARLY_WARNING:
			ltfsmsg(LTFS_WARN, 31246W, "writefm");
			rc = DEVICE_GOOD;
			pos->programmable_early_warning = true;
			break;
		case -EDEV_CONFIGURE_CHANGED:
			written_count = pos->filemarks - cur_fm;
			if (count != written_count) {
				/* need to write fm again */
				count = count - written_count;
				cur_fm = pos->filemarks;
				goto start_wfm;
			}
			break;
		default:
			if (pos->early_warning) {
				ltfsmsg(LTFS_WARN, 31245W, "writefm");
				rc = DEVICE_GOOD;
			}
			if (pos->programmable_early_warning) {
				ltfsmsg(LTFS_WARN, 31246W, "writefm");
				rc = DEVICE_GOOD;
			}
			break;
		}

		if (rc != DEVICE_GOOD) {
			camtape_process_errors(device, rc, msg, "writefm", true);
		}
	} else {
		if (pos->early_warning) {
			ltfsmsg(LTFS_WARN, 31245W, "writefm");
			rc = DEVICE_GOOD;
		}
		if (pos->programmable_early_warning) {
			ltfsmsg(LTFS_WARN, 31246W, "writefm");
			rc = DEVICE_GOOD;
		}
	}

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_WRITEFM));
	return rc;
}

/**
 * Rewind tape
 * @param device a pointer to the camtape backend
 * @param pos a pointer to position data. This function will update position infomation.
 * @return 0 on success or a negative value on error
 */
int camtape_rewind(void *device, struct tc_position *pos)
{
	int rc;
	char *msg = NULL;
	struct camtape_data *softc = device;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_REWIND));
	ltfsmsg(LTFS_DEBUG, 31392D, "rewind", softc->drive_serial);

	rc = _mt_command(device, MTREW, "REWIND", 0, &msg);
	camtape_readpos(device, pos);

	if (rc != DEVICE_GOOD) {
		camtape_process_errors(device, rc, msg, "rewind", true);
	}

	softc->clear_by_pc     = false;
	softc->force_writeperm = DEFAULT_WRITEPERM;
	softc->force_readperm  = DEFAULT_READPERM;
	softc->write_counter = 0;
	softc->read_counter = 0;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_REWIND));
	return rc;
}

/**
 * Locate to position on tape
 * @param device a pointer to the camtape backend
 * @param dest a position data of destination.
 * @param pos a pointer to position data. This function will update position infomation.
 * @return rc 0 on success or a negative value on error
 */
int camtape_locate(void *device, struct tc_position dest, struct tc_position *pos)
{
	int rc;
	char *msg = NULL;
	struct camtape_data *softc = device;
	struct scsi_sense_data sense_data;
	struct mtlocate mtl;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_LOCATE));
	ltfsmsg(LTFS_DEBUG, 31397D, "locate", (unsigned long long)dest.partition,
		(unsigned long long)dest.block, softc->drive_serial);

	memset(&mtl, 0, sizeof(mtl));

	mtl.dest_type = MT_LOCATE_DEST_OBJECT;
	mtl.block_address_mode = MT_LOCATE_BAM_IMPLICIT;
	mtl.logical_id = dest.block;
	mtl.partition = dest.partition;
	if (pos->partition != dest.partition) {
		mtl.flags |= MT_LOCATE_FLAG_CHANGE_PART;

		if (softc->clear_by_pc) {
			softc->clear_by_pc     = false;
			softc->force_writeperm = DEFAULT_WRITEPERM;
			softc->force_readperm  = DEFAULT_READPERM;
			softc->write_counter = 0;
			softc->read_counter  = 0;
		}
	}

	rc = ioctl(softc->fd_sa, MTIOCEXTLOCATE, (caddr_t)&mtl);
	if (rc != 0) {
		rc = camtape_ioctlrc2err(softc, softc->fd_sa, &sense_data, /*control_cmd*/ 1, &msg);
	} else {
		rc = DEVICE_GOOD;
	}

	if (rc != DEVICE_GOOD) {
		if ((unsigned long long)dest.block == TAPE_BLOCK_MAX && rc == -EDEV_EOD_DETECTED) {
			ltfsmsg(LTFS_DEBUG, 31248D, "Locate");
			rc = DEVICE_GOOD;
		}

		if (rc != DEVICE_GOOD)
			camtape_process_errors(device, rc, msg, "locate", true);
	}

	camtape_readpos(device, pos);

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_LOCATE));
	return rc;
}

/**
 * Space to position on tape
 * @param device a pointer to the camtape backend
 * @param count specify record or fm count to move
 * @param type specify type of move
 * @param pos a pointer to position data. This function will update position infomation.
 * @return rc 0 on success or a negative value on error
 */
int camtape_space(void *device, size_t count, TC_SPACE_TYPE type, struct tc_position *pos)
{
	int cmd;
	int rc;
	char *msg = NULL;
	struct camtape_data *softc = (struct camtape_data *)device;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_SPACE));
	switch (type) {
		case TC_SPACE_EOD:
			ltfsmsg(LTFS_DEBUG, 31392D, "space to EOD", softc->drive_serial);
			cmd = MTEOD;
			count = 0;
			break;
		case TC_SPACE_FM_F:
			ltfsmsg(LTFS_DEBUG, 31394D, "space forward file marks", (unsigned long long)count,
					softc->drive_serial);
			cmd = MTFSF;
			break;
		case TC_SPACE_FM_B:
			ltfsmsg(LTFS_DEBUG, 31394D, "space back file marks", (unsigned long long)count,
					softc->drive_serial);
			cmd = MTBSF;
			break;
		case TC_SPACE_F:
			ltfsmsg(LTFS_DEBUG, 31394D, "space forward records", (unsigned long long)count,
					softc->drive_serial);
			cmd = MTFSR;
			break;
		case TC_SPACE_B:
			ltfsmsg(LTFS_DEBUG, 31394D, "space back records", (unsigned long long)count,
					softc->drive_serial);
			cmd = MTBSR;
			break;
		default:
			/* unexpected space type */
			ltfsmsg(LTFS_INFO, 31249I);
			ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SPACE));
			return EDEV_INVALID_ARG;
	}

	if ((unsigned long long)count > 0xFFFFFF) {
		/* count is too large for SPACE 6 command */
		ltfsmsg(LTFS_INFO, 31250I, count);
		ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SPACE));
		return EDEV_INVALID_ARG;
	}

	rc = _mt_command(device, cmd, "SPACE", count, &msg);
	camtape_readpos(device, pos);

	if (rc != DEVICE_GOOD) {
		camtape_process_errors(device, rc, msg, "space", true);
	}

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SPACE));
	return rc;
}

int camtape_long_erase(void *device)
{
	int rc;
	union ccb *ccb = NULL;
	struct camtape_data *softc = (struct camtape_data *)device;
	char *msg;
	int timeout;

	ccb = cam_getccb(softc->cd);
	if (ccb == NULL) {
		rc = -EDEV_NO_MEMORY;
		goto bailout;
	}

	memset(&(&ccb->ccb_h)[1], 0,
		sizeof(struct ccb_scsiio) - sizeof(struct ccb_hdr));

	timeout = camtape_get_timeout(softc->timeouts, ERASE);
	if (timeout < 0) {
		rc = -EDEV_UNSUPPORETD_COMMAND;
		goto bailout;
	}

	scsi_erase(&ccb->csio,
			   /*retries*/ 1,
			   /*cbfcnp*/ NULL,
			   /*tag_action*/ MSG_SIMPLE_Q_TAG,
			   /*immediate*/ 1,
			   /*long_erase*/ 1,
			   /*sense_len*/ SSD_FULL_SIZE,
			   /*timeout*/ timeout);

	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS | CAM_PASS_ERR_RECOVER;

	rc = camtape_send_ccb(softc, ccb, &msg);

	if (rc != DEVICE_GOOD)
		camtape_process_errors(softc, rc, msg, "long erase", true);

bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);

	return rc;
}

/**
 * Erase tape from current position
 * @param device a pointer to the camtape backend
 * @param pos a pointer to position data. This function will update position infomation.
 * @param long_erase Set long bit and immed bit ON.
 * @return 0 on success or a negative value on error
 */
int camtape_erase(void *device, struct tc_position *pos, bool long_erase)
{
	int rc;
	char *msg = NULL;
	struct ltfs_timespec ts_start, ts_now;
	struct camtape_data *softc = (struct camtape_data *)device;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_ERASE));

	if (long_erase) {
		ltfsmsg(LTFS_DEBUG, 31392D, "long erase", softc->drive_serial);
		get_current_timespec(&ts_start);

		rc = camtape_long_erase(device);

		if (rc == -EDEV_TIME_STAMP_CHANGED) {
			ltfsmsg(LTFS_DEBUG, 31211D, "erase", -1, rc);
			rc = camtape_long_erase(device);
		}

		if (rc != -EDEV_OPERATION_IN_PROGRESS)
			goto bailout;

		while (true) {
			struct scsi_sense_data sense_data;
			int fill_len = 0;

			memset(&sense_data, 0, sizeof(sense_data));
			rc = camtape_request_sense(device, &sense_data, sizeof(sense_data), &fill_len);
			if (rc != -EDEV_OPERATION_IN_PROGRESS)
				goto bailout;

			if (IS_ENTERPRISE(softc->drive_type)) {
				get_current_timespec(&ts_now);
				ltfsmsg(LTFS_INFO, 31251I, (ts_now.tv_sec - ts_start.tv_sec)/60);
			} else {
				struct scsi_sense_sks_progress prog;

				memset(&prog, 0, sizeof(prog));
				rc = scsi_get_sks(&sense_data, fill_len, (uint8_t *)&prog);
				if (rc == 0) {
					int progress;

					progress = scsi_2btoul(prog.progress);

					ltfsmsg(LTFS_INFO, 31252I, progress*100/0xFFFF);
				} else {
					rc = 0;
					goto bailout;
				}
			}
			sleep(60);
		}
	} else {
		ltfsmsg(LTFS_DEBUG, 31392D, "erase", softc->drive_serial);
		rc = _mt_command(device, MTERASE, "ERASE", 0, &msg);	// param=0 means invoking short erase. (not long erase)
	}

bailout:

	camtape_readpos(device, pos);

	if (rc != DEVICE_GOOD) {
		camtape_process_errors(device, rc, msg, "erase", true);
	}

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_ERASE));
	return rc;
}

/**
 * Load tape or rewind when a tape is already loaded
 * @param device a pointer to the camtape backend
 * @param pos a pointer to position data. This function will update position infomation.
 * @return 0 on success or a negative value on error
 */

int _camtape_load_unload(void *device, bool load, struct tc_position *pos)
{
	int rc;
	char *msg = NULL;
	bool take_dump = true;
	struct camtape_data *softc = ((struct camtape_data *) device);

	if (load) {
		rc = _mt_command(device, MTLOAD, "LOAD", 0, &msg);
	}
	else {
		rc = _mt_command(device, MTOFFL, "UNLOAD", 0, &msg);
	}

	if (rc != DEVICE_GOOD) {
		switch (rc) {
		case -EDEV_LOAD_UNLOAD_ERROR:
			if (softc->loadfailed) {
				take_dump = false;
			}
			else {
				softc->loadfailed = true;
			}
			break;
		case -EDEV_NO_MEDIUM:
		case -EDEV_BECOMING_READY:
		case -EDEV_MEDIUM_MAY_BE_CHANGED:
			take_dump = false;
			break;
		default:
			break;
		}
		camtape_readpos(device, pos);
		camtape_process_errors(device, rc, msg, "load unload", take_dump);
	}
	else {
		if (load) {
			camtape_readpos(device, pos);
			softc->tape_alert = 0;
		}
		else {
			pos->partition = 0;
			pos->block = 0;
			softc->tape_alert = 0;
		}
		softc->loadfailed = false;
	}

	return rc;
}

int camtape_load(void *device, struct tc_position *pos)
{
	int rc;
	unsigned char buf[TC_MP_SUPPORTEDPAGE_SIZE];
	struct camtape_data *softc = (struct camtape_data *)device;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_LOAD));
	ltfsmsg(LTFS_DEBUG, 31392D, "load", softc->drive_serial);

	rc = _camtape_load_unload(device, true, pos);
	if (rc < 0) {
		ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_LOAD));
		return rc;
	}

	/* Check Cartridge type */
	rc = camtape_modesense(device, TC_MP_SUPPORTEDPAGE, TC_MP_PC_CURRENT, 0x00, buf, sizeof(buf));
	if (rc < 0) {
		ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_LOAD));
		return rc;
	}

	softc->loaded = true;
	softc->is_worm = false;

	softc->clear_by_pc     = false;
	softc->force_writeperm = DEFAULT_WRITEPERM;
	softc->force_readperm  = DEFAULT_READPERM;
	softc->write_counter = 0;
	softc->read_counter = 0;
	softc->cart_type = buf[2];
	softc->density_code = buf[8];

	if (softc->cart_type == 0x00) {
		ltfsmsg(LTFS_WARN, 31253W);
		ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_LOAD));
		return 0;
	}

	rc = ibm_tape_is_supported_tape(softc->cart_type, softc->density_code, &(softc->is_worm));
	if(rc == -LTFS_UNSUPPORTED_MEDIUM)
		ltfsmsg(LTFS_INFO, 31255I, softc->cart_type, softc->density_code);

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_LOAD));

	return rc;
}

/**
 * Unload tape
 * @param device a pointer to the camtape backend
 * @param pos a pointer to position data. This function will update position infomation.
 * @return 0 on success or a negative value on error
 */
int camtape_unload(void *device, struct tc_position *pos)
{
	int rc;
	struct camtape_data *softc = (struct camtape_data *)device;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_UNLOAD));
	ltfsmsg(LTFS_DEBUG, 31392D, "unload", softc->drive_serial);

	rc = _camtape_load_unload(device, false, pos);

	softc->clear_by_pc     = false;
	softc->force_writeperm = DEFAULT_WRITEPERM;
	softc->force_readperm = DEFAULT_READPERM;
	softc->write_counter = 0;
	softc->read_counter = 0;

	if (rc < 0) {
		ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_UNLOAD));
		return rc;
	} else {
		softc->loaded = false;
		softc->is_worm = false;
		ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_UNLOAD));
		return rc;
	}
}

/*
 * Get the number of blocks in the buffer on the tape drive after a write.  Eventually it would
 * be nice to include this in status information returned from the sa(4) driver.
 */
static int camtape_get_block_in_buffer(void *device, uint32_t *block)
{
	int rc;
	union ccb *ccb = NULL;
	struct camtape_data *softc = (struct camtape_data *)device;
	struct scsi_tape_position_ext_data ext_data;
	int timeout;
	char *msg;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_READPOS));

	ccb = cam_getccb(softc->cd);
	if (ccb == NULL) {
		rc = -EDEV_NO_MEMORY;
		goto bailout;
	}
	CCB_CLEAR_ALL_EXCEPT_HDR(ccb);
	memset(&ext_data, 0, sizeof(ext_data));

	timeout = camtape_get_timeout(softc->timeouts, READ_POSITION);
	if (timeout < 0) {
		rc = -EDEV_UNSUPPORETD_COMMAND;
		goto bailout;
	}

	/*
	 * XXX KDM should we do any retries here?  Doing retries potentially hides sense data.
	 */
	scsi_read_position_10(&ccb->csio,
						  /*retries*/ 0,
						  /*cbfcnp*/ NULL,
						  /*tag_action*/ MSG_SIMPLE_Q_TAG,
						  /*service_action*/ SA_RPOS_EXTENDED_FORM,
						  /*data_ptr*/ (uint8_t *)&ext_data,
						  /*length*/ sizeof(ext_data),
						  /*sense_len*/ SSD_FULL_SIZE,
						  /*timeout*/ timeout);

	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;
	rc = camtape_send_ccb(softc, ccb, &msg);

	if (rc != DEVICE_GOOD)
		camtape_process_errors(softc, rc, msg, "READPOS", true);
	else {
		*block = scsi_3btoul(ext_data.num_objects);
		ltfsmsg(LTFS_DEBUG, 30398D, "blocks-in-buffer",
				(unsigned long long) *block, 0, 0, softc->drive_serial);
	}

bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READPOS));

	return rc;
}

static int camtape_load_attr(struct mt_status_data *mtinfo, xmlDocPtr doc, xmlAttr *attr,
    int level, char **msg)
{
	struct mt_status_entry *entry;
	xmlAttr *xattr = NULL;
	int retval = DEVICE_GOOD;

	entry = mtinfo->cur_entry[mtinfo->level];

	for (xattr = attr; xattr != NULL; xattr = xattr->next) {
		if (xattr->type == XML_ATTRIBUTE_NODE) {
			struct mt_status_nv *nv;
			int need_nv = 0, need_free = 1;
			char *str;

			str = (char *)xmlNodeListGetString(doc, xattr->children, 1);
			if (strcmp((char *)xattr->name, "size") == 0) {
				entry->size = strtoull(str, NULL, 0);
			} else if (strcmp((char *)xattr->name, "type") == 0) {
				if (strcmp(str, "int") == 0) {
					entry->var_type = MT_TYPE_INT;
				} else if (strcmp(str, "uint") == 0) {
					entry->var_type = MT_TYPE_UINT;
				} else if (strcmp(str, "str") == 0) {
					entry->var_type = MT_TYPE_STRING;
				} else if (strcmp(str, "node") == 0) {
					entry->var_type = MT_TYPE_NODE;
				} else {
					need_nv = 1;
				}
			} else if (strcmp((char *)xattr->name, "fmt") == 0) {
				entry->fmt = str;
				need_free = 0;
			} else if (strcmp((char *)xattr->name, "desc") == 0) {
				entry->desc = str;
				need_free = 0;
			} else {
				need_nv = 1;
			}
			if (need_nv != 0) {
				nv = malloc(sizeof(*nv));
				if (nv == NULL) {
					*msg = strdup("Unable to allocate memory");
					retval = -EDEV_NO_MEMORY;
					goto bailout;
				}
				memset(nv, 0, sizeof(*nv));
				nv->name = strdup((char *)xattr->name);
				nv->value = str;
				STAILQ_INSERT_TAIL(&entry->nv_list, nv, links);
				need_free = 0;
			}
			if (need_free != 0)
				xmlFree(str);
		}
	}
bailout:

	return (retval);
}

static int camtape_load_elements(struct mt_status_data *mtinfo, xmlDocPtr doc, xmlNode *node,
    int level, char **msg)
{
	struct mt_status_entry *entry;
	xmlNode *xnode = NULL;
	int retval = DEVICE_GOOD;

	for (xnode = node; xnode != NULL; xnode = xnode->next) {
		int created_element = 0;

		if (xnode->type == XML_ELEMENT_NODE) {
			mtinfo->level++;
			if ((u_int)mtinfo->level > sizeof(mtinfo->cur_entry) /
			    sizeof(mtinfo->cur_entry[0])) {
				*msg = strdup("Too many nesting levels");
				retval = -EDEV_INVALID_ARG;
				goto bailout;
			}
			created_element = 1;
			entry = malloc(sizeof(*entry));
			if (entry == NULL) {
				*msg = strdup("Unable to allocate memory");
				retval = -EDEV_NO_MEMORY;
				goto bailout;
			}
			memset(entry, 0, sizeof(*entry));
			STAILQ_INIT(&entry->nv_list);
			STAILQ_INIT(&entry->child_entries);
			entry->entry_name = strdup((char *)xnode->name);
			mtinfo->cur_entry[mtinfo->level] = entry;
			if (mtinfo->cur_entry[mtinfo->level - 1] == NULL) {
				STAILQ_INSERT_TAIL(&mtinfo->entries, entry, links);
			} else {
				STAILQ_INSERT_TAIL(
				    &mtinfo->cur_entry[mtinfo->level - 1]->child_entries,
				    entry, links);
				entry->parent = mtinfo->cur_entry[mtinfo->level - 1];
			}
		} else if (xnode->type == XML_TEXT_NODE) {
			char *str;


			str = (char *)xmlNodeListGetString(doc, xnode, 1);

			if (xmlIsBlankNode(xnode) != 0)
				continue;

			entry = mtinfo->cur_entry[mtinfo->level];

			entry->value = str;
			switch (entry->var_type) {
			case MT_TYPE_INT:
				entry->value_signed = strtoll(str, NULL, 0);
				break;
			case MT_TYPE_UINT:
				entry->value_unsigned = strtoull(str, NULL, 0);
				break;
			default:
				break;
			}
		}
		if (xnode->properties != NULL) {
			retval = camtape_load_attr(mtinfo, doc, xnode->properties, level, msg);
			if (retval != DEVICE_GOOD)
				goto bailout;
		}
		retval = camtape_load_elements(mtinfo, doc, xnode->children, level + 1, msg);
		if (retval != DEVICE_GOOD)
			goto bailout;

		if (created_element != 0) {
			mtinfo->cur_entry[mtinfo->level] = NULL;
			mtinfo->level--;
		}
	}

bailout:
	return (retval);
}

int camtape_get_mtinfo(struct camtape_data *softc, struct mt_status_data *mtinfo, int params,
    char **msg)
{
	struct mtextget extget;
	int alloc_size = 32768;
	xmlParserCtxtPtr ctx = NULL;
	xmlDocPtr doc = NULL;
	xmlNode *root_element = NULL;
	char *xml_str = NULL;
	int retval = DEVICE_GOOD;

extget_retry:

	memset(&extget, 0, sizeof(extget));
	xml_str = malloc(alloc_size);
	if (xml_str == NULL) {
		*msg = strdup("Unable to allocate memory");
		retval = -EDEV_NO_MEMORY;
		goto bailout;
	}
	extget.status_xml = xml_str;
	extget.alloc_len = alloc_size;

	if (ioctl(softc->fd_sa, (params != 0) ? MTIOCPARAMGET : MTIOCEXTGET, &extget) == -1) {
		char tmpstr[512];

		snprintf(tmpstr, sizeof(tmpstr), "ioctl error from sa(4) driver: %s",
		    strerror(errno));
		*msg = strdup(tmpstr);
		retval = -errno;
		goto bailout;
	}

	if (extget.status == MT_EXT_GET_NEED_MORE_SPACE) {
		/*
		 * The driver needs more space, so double
		 * our allocation and try again.
		 */
		alloc_size *= 2;
		free(xml_str);
		xml_str = NULL;
		goto extget_retry;
	} else if (extget.status != MT_EXT_GET_OK) {
		char tmpstr[512];

		retval = -EDEV_DRIVER_ERROR;
		snprintf(tmpstr, sizeof(tmpstr), "Error getting status data from sa(4) driver: status = %d",
			extget.status);
		*msg = strdup(tmpstr);
		goto bailout;
	}

	LIBXML_TEST_VERSION;

	ctx = xmlNewParserCtxt();
	if (ctx == NULL) {
		*msg = strdup("Unable to create new XML parser context");
		retval = -EDEV_NO_MEMORY;
		goto bailout;
	}

	doc = xmlCtxtReadMemory(ctx, xml_str, strlen(xml_str), NULL, NULL, 0);
	if (doc == NULL) {
		*msg = strdup("Unable to parse XML");
		retval = -EDEV_DRIVER_ERROR;
		goto bailout;
	} else {
		if (ctx->valid == 0) {
			*msg = strdup("XML parsing result is: not valid");
			retval = -EDEV_INVALID_ARG;
			goto bailout;
		}
	}

	root_element = xmlDocGetRootElement(doc);
	memset(mtinfo, 0, sizeof(*mtinfo));
	mtinfo->level = 1;
	STAILQ_INIT(&mtinfo->entries);
	retval = camtape_load_elements(mtinfo, doc, root_element, 0, msg);

bailout:

	if (xml_str != NULL)
		free(xml_str);
	if (doc != NULL)
		xmlFreeDoc(doc);
	if (ctx != NULL)
		xmlFreeParserCtxt(ctx);

	return (retval);
}

int camtape_free_mtinfo(struct camtape_data *softc, struct mt_status_data *mtinfo)
{
	mt_status_free(mtinfo);

	return (DEVICE_GOOD);
}

typedef enum {
	MT_REPORTED_FILENO 	= 0,
	MT_REPORTED_BLKNO	= 1,
	MT_PARTITION		= 2,
	MT_BOP				= 3,
	MT_EOP				= 4,
	MT_BPEW				= 5
} camtape_status_index;

struct camtape_status_item {
	const char *name;
	struct mt_status_entry *entry;
} req_status_items[] = {
	{"reported_fileno", NULL },
	{"reported_blkno", NULL },
	{"partition", NULL },
	{"bop", NULL },
	{"eop", NULL },
	{"bpew", NULL}
};
#define	CT_NUM_STATUS_ITEMS	(sizeof(req_status_items)/sizeof(req_status_items[0]))

int camtape_getstatus(struct camtape_data *softc, struct mt_status_data *mtinfo,
    struct camtape_status_item *status_items, int num_status_items, char **msg)
{
	int retval = DEVICE_GOOD;
	int i;

	retval = camtape_get_mtinfo(softc, mtinfo, /*params*/ 0, msg);
	if (retval != DEVICE_GOOD)
		goto bailout;

	for (i = 0; i < num_status_items; i++) {
		char *name;

		name = __DECONST(char *, status_items[i].name);
		status_items[i].entry = mt_status_entry_find(mtinfo, name);
		if (status_items[i].entry == NULL) {
			char tmpstr[512];

			snprintf(tmpstr, sizeof(tmpstr), "Unable to fetch sa(4) status item %s", name);
			*msg = strdup(tmpstr);
			retval = -EDEV_INVALID_ARG;
			goto bailout;
		}
	}

bailout:
	return (retval);
}

/**
 * Tell the current position
 * @param device a pointer to the camtape backend
 * @param pos a pointer to position data. This function will update position infomation.
 * @return 0 on success or a negative value on error
 */
int camtape_readpos(void *device, struct tc_position *pos)
{
	struct camtape_data *softc = (struct camtape_data *)device;
	int rc = DEVICE_GOOD;
	char *msg = NULL;
	struct camtape_status_item status_items[CT_NUM_STATUS_ITEMS];
	struct mt_status_data mtinfo;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_READPOS));

	memset(status_items, 0, sizeof(status_items));
	memset(&mtinfo, 0, sizeof(mtinfo));
	memcpy(status_items, req_status_items, MIN(sizeof(status_items), sizeof(req_status_items)));

	rc = camtape_getstatus(softc, &mtinfo, status_items, CT_NUM_STATUS_ITEMS, &msg);
	if (rc != DEVICE_GOOD) {
		camtape_process_errors(device, rc, msg, "readpos", true);
		goto bailout;
	}

	if (status_items[MT_EOP].entry->value_signed == 0)
		pos->early_warning = false;
	else if (status_items[MT_EOP].entry->value_signed == 1)
		pos->early_warning = true;

	if (status_items[MT_BPEW].entry->value_signed == 0)
		pos->programmable_early_warning = false;
	else if (status_items[MT_BPEW].entry->value_signed == 1)
		pos->programmable_early_warning = true;
	pos->partition = status_items[MT_PARTITION].entry->value_signed;
	pos->block = status_items[MT_REPORTED_BLKNO].entry->value_signed;
	pos->filemarks = status_items[MT_REPORTED_FILENO].entry->value_signed;

	ltfsmsg(LTFS_DEBUG, 31398D, "readpos", (unsigned long long)pos->partition,
			(unsigned long long)pos->block, (unsigned long long)pos->filemarks,
			softc->drive_serial);
bailout:

	camtape_free_mtinfo(softc, &mtinfo);

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READPOS));

	return rc;
}

/**
 * Make/Unmake partition
 * @param device a pointer to the camtape backend
 * @param format specify type of format
 * @return 0 on success or a negative value on error
 */
int camtape_format(void *device, TC_FORMAT_TYPE format)
{
	int rc, aux_rc;
	char *msg = NULL;
	struct camtape_data *softc = (struct camtape_data *)device;
	unsigned char buf[TC_MP_SUPPORTEDPAGE_SIZE];
	union ccb *ccb = NULL;
	int timeout;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_FORMAT));
	ltfsmsg(LTFS_DEBUG, 31392D, "format", softc->drive_serial);

	if ((unsigned char) format >= (unsigned char) TC_FORMAT_MAX) {
		ltfsmsg(LTFS_INFO, 31256I, format);
		ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_FORMAT));
		return -1;
	}

	ccb = cam_getccb(softc->cd);
	if (ccb == NULL) {
		rc = -EDEV_NO_MEMORY;
		goto bailout;
	}

	memset(&(&ccb->ccb_h)[1], 0,
		sizeof(struct ccb_scsiio) - sizeof(struct ccb_hdr));

	timeout = camtape_get_timeout(softc->timeouts, FORMAT_MEDIUM);
	if (timeout < 0) {
		rc = -EDEV_UNSUPPORETD_COMMAND;
		goto bailout;
	}

	scsi_format_medium(&ccb->csio,
					   /*retries*/ 1,
					   /*cbfcnp*/ NULL,
					   /*tag_action*/ MSG_SIMPLE_Q_TAG,
					   /*byte1*/ 0,
					   /*byte2*/ format,
					   /*data_ptr*/ NULL,
					   /*dxfer_len*/ 0,
					   /*sense_len*/ SSD_FULL_SIZE,
					   /*timeout*/ timeout);

	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS | CAM_PASS_ERR_RECOVER;

	rc = camtape_send_ccb(softc, ccb, &msg);

	if (rc != DEVICE_GOOD) {
		camtape_process_errors(device, rc, msg, "format", true);
		goto bailout;
	}

	aux_rc = camtape_modesense(device, TC_MP_SUPPORTEDPAGE, TC_MP_PC_CURRENT, 0x00, buf, sizeof(buf));
	if (aux_rc == DEVICE_GOOD) {
		softc->cart_type = buf[2];
		softc->density_code = buf[8];
	}
bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_FORMAT));
	return rc;
}

/**
 * Tell log data from the drive
 * @param device a pointer to the camtape backend
 * @param page page code of log sense
 * @param buf pointer to buffer to store log data
 * @param size length of the buffer
 * @return 0 on success or a negative value on error
 */
#define MAX_UINT16 (0x0000FFFF)

int camtape_logsense_page(struct camtape_data *softc, const uint8_t page, const uint8_t subpage,
						  unsigned char *buf, const size_t size)
{
	int rc = DEVICE_GOOD;
	char *msg = NULL;
	struct scsi_log_sense *scsi_cmd;
	union ccb *ccb = NULL;
	int timeout;

	ltfsmsg(LTFS_DEBUG3, 31397D, "logsense", (unsigned long long)page, (unsigned long long)subpage,
			softc->drive_serial);

	ccb = cam_getccb(softc->cd);
	if (ccb == NULL) {
		rc = -EDEV_NO_MEMORY;
		goto bailout;
	}

	memset(&(&ccb->ccb_h)[1], 0,
		sizeof(struct ccb_scsiio) - sizeof(struct ccb_hdr));

	timeout = camtape_get_timeout(softc->timeouts, LOG_SENSE);
	if (timeout < 0) {
		rc = -EDEV_UNSUPPORETD_COMMAND;
		goto bailout;
	}

	scsi_log_sense(&ccb->csio,
				   /*retries*/ 1,
				   /*cbfcnp*/ NULL,
				   /*tag_action*/ MSG_SIMPLE_Q_TAG,
				   /*page_code*/ page,
				   /*page*/ SLS_PAGE_CTRL_CUMULATIVE,
				   /*save_pages*/ 0,
				   /*ppc*/ 0,
				   /*paramptr*/ 0,
				   /*param_buf*/ buf,
				   /*param_len*/ size,
				   /*sense_len*/ SSD_FULL_SIZE,
				   /*timeout*/ timeout);
	/*
	 * XXX KDM need to add subpage to the log sense fill function.
	 */
	scsi_cmd = (struct scsi_log_sense *)&ccb->csio.cdb_io.cdb_bytes;
	scsi_cmd->subpage = subpage;

	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS | CAM_PASS_ERR_RECOVER;

	rc = camtape_send_ccb(softc, ccb, &msg);

	if (rc != DEVICE_GOOD)
		camtape_process_errors(softc, rc, msg, "logsense page", true);

bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);

	return rc;
}

int camtape_logsense(void *device, const uint8_t page, unsigned char *buf, const size_t size)
{
	struct camtape_data *softc = (struct camtape_data *)device;
	int ret = 0;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_LOGSENSE));
	ret = camtape_logsense_page(softc, page, 0, buf, size);
	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_LOGSENSE));
	return ret;
}

#define PARTITIOIN_REC_HEADER_LEN (4)

int camtape_remaining_capacity(void *device, struct tc_remaining_cap *cap)
{
	unsigned char logdata[LOGSENSEPAGE];
	unsigned char buf[32];
	struct camtape_data *softc = (struct camtape_data *)device;
	int param_size, i;
	int length;
	int offset;
	uint32_t logcap;
	int rc;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_REMAINCAP));
	if (IS_LTO(softc->drive_type) && (DRIVE_GEN(softc->drive_type) == 0x05)) {
		/* Issue LogPage 0x31 */
		rc = camtape_logsense(device, LOG_TAPECAPACITY, logdata, LOGSENSEPAGE);
		if (rc) {
			ltfsmsg(LTFS_INFO, 31257I, LOG_TAPECAPACITY, rc);
			ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_REMAINCAP));
			return rc;
		}

		for(i = TAPECAP_REMAIN_0; i < TAPECAP_SIZE; i++) {
			if (parse_logPage(logdata, (uint16_t) i, &param_size, buf, sizeof(buf))
				|| param_size != sizeof(uint32_t)) {
				ltfsmsg(LTFS_INFO, 31258I);
				ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_REMAINCAP));
				return -EDEV_NO_MEMORY;
			}

			logcap = ltfs_betou32(buf);

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
				ltfsmsg(LTFS_INFO, 31259I, i);
				ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_REMAINCAP));
				return -EDEV_INVALID_ARG;
				break;
			}
		}
	}
	else {
		/* Issue LogPage 0x17 */
		rc = camtape_logsense(device, LOG_VOLUMESTATS, logdata, LOGSENSEPAGE);
		if (rc) {
			ltfsmsg(LTFS_INFO, 31257I, LOG_VOLUMESTATS, rc);
			ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_REMAINCAP));
			return rc;
		}

		/* parse param 0x202 - nominal capacity of the partitions */
		if (parse_logPage(logdata, (uint16_t)VOLSTATS_PARTITION_CAP, &param_size, buf, sizeof(buf))) {
			ltfsmsg(LTFS_INFO, 31258I);
			ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_REMAINCAP));
			return -EDEV_NO_MEMORY;
		}

		memset(cap, 0, sizeof(struct tc_remaining_cap));

		cap->max_p0 = ltfs_betou32(&buf[PARTITIOIN_REC_HEADER_LEN]);
		offset = (int)buf[0] + 1;
		length = (int)buf[offset] + 1;

		if (offset + length <= param_size) {
			cap->max_p1 = ltfs_betou32(&buf[offset + PARTITIOIN_REC_HEADER_LEN]);
		}

		/* parse param 0x204 - remaining capacity of the partitions */
		if (parse_logPage(logdata, (uint16_t)VOLSTATS_PART_REMAIN_CAP, &param_size, buf, sizeof(buf))) {
			ltfsmsg(LTFS_INFO, 31258I);
			ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_REMAINCAP));
			return -EDEV_NO_MEMORY;
		}

		cap->remaining_p0 = ltfs_betou32(&buf[PARTITIOIN_REC_HEADER_LEN]);
		offset = (int)buf[0] + 1;
		length = (int)buf[offset] + 1;

		if (offset + length <= param_size) {
			cap->remaining_p1 = ltfs_betou32(&buf[offset + PARTITIOIN_REC_HEADER_LEN]);
		}

		/* Convert MB to MiB -- Need to consider about overflow when max cap reaches to 18EB */
		cap->max_p0 = (cap->max_p0 * 1000 * 1000) >> 20;
		cap->max_p1 = (cap->max_p1 * 1000 * 1000) >> 20;
		cap->remaining_p0 = (cap->remaining_p0 * 1000 * 1000) >> 20;
		cap->remaining_p1 = (cap->remaining_p1 * 1000 * 1000) >> 20;
	}

	ltfsmsg(LTFS_DEBUG3, 31397D, "capacity part0", (unsigned long long)cap->remaining_p0,
			(unsigned long long)cap->max_p0, softc->drive_serial);
	ltfsmsg(LTFS_DEBUG3, 31397D, "capacity part1", (unsigned long long)cap->remaining_p1,
		(unsigned long long)cap->max_p1, softc->drive_serial);

	return 0;
}


/**
 * Get mode data
 * @param device a pointer to the camtape backend
 * @param page a page id of mode data
 * @param pc page control value for mode sense command
 * @param buf pointer to mode page data. this function will update this data
 * @param size length of buf
 * @return 0 on success or a negative value on error
 */
int camtape_modesense(void *device, const uint8_t page, const TC_MP_PC_TYPE pc, const uint8_t subpage,
					  unsigned char *buf, const size_t size)
{
	int rc;
	char *msg = NULL;
	struct camtape_data *softc = (struct camtape_data *)device;
	union ccb *ccb = NULL;
	int timeout;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_MODESENSE));
	ltfsmsg(LTFS_DEBUG3, 31393D, "modesense", page, softc->drive_serial);

	ccb = cam_getccb(softc->cd);
	if (ccb == NULL) {
		rc = -EDEV_NO_MEMORY;
		goto bailout;
	}

	memset(&(&ccb->ccb_h)[1], 0,
		sizeof(struct ccb_scsiio) - sizeof(struct ccb_hdr));

	timeout = camtape_get_timeout(softc->timeouts, MODE_SENSE_10);
	if (timeout < 0) {
		rc = -EDEV_UNSUPPORETD_COMMAND;
		goto bailout;
	}

	scsi_mode_sense_len(&ccb->csio,
						/*retries*/ 1,
						/*cbfcnp*/ NULL,
						/*tag_action*/ MSG_SIMPLE_Q_TAG,
						/*dbd*/ 0,
						/*page_code*/ pc,
						/*page*/ page,
						/*param_buf*/ buf,
						/*param_len*/ MIN(MAX_UINT16, size),
						/*minimum_cmd_size*/ 10,
						/*sense_len*/ SSD_FULL_SIZE,
						/*timeout*/ timeout);
	/*
	 * XXX KDM need a version of scsi_mode_sense() that allows setting the subpage.  The offset
	 * is the same in the 6 and 10 byte versions, so we can just set the same byte here.
	 */
	ccb->csio.cdb_io.cdb_bytes[3] = subpage;

	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS | CAM_PASS_ERR_RECOVER;

	rc = camtape_send_ccb(softc, ccb, &msg);

	if (rc != DEVICE_GOOD)
		camtape_process_errors(softc, rc, msg, "modesense", true);

bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_MODESENSE));
	return rc;
}

/**
 * Set mode data
 * @param device a pointer to the camtape backend
 * @param buf pointer to mode page data. This data will be sent to the drive
 * @param size length of buf
 * @return 0 on success or a negative value on error
 */
int camtape_modeselect(void *device, unsigned char *buf, const size_t size)
{
	int rc;
	char *msg = NULL;
	struct camtape_data *softc = (struct camtape_data *)device;
	union ccb *ccb;
	int timeout;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_MODESELECT));
	ltfsmsg(LTFS_DEBUG3, 31392D, "modeselect", softc->drive_serial);

	ccb = cam_getccb(softc->cd);
	if (ccb == NULL) {
		rc = -EDEV_NO_MEMORY;
		goto bailout;
	}

	memset(&(&ccb->ccb_h)[1], 0,
		sizeof(struct ccb_scsiio) - sizeof(struct ccb_hdr));

	timeout = camtape_get_timeout(softc->timeouts, MODE_SELECT_10);
	if (timeout < 0) {
		rc = -EDEV_UNSUPPORETD_COMMAND;
		goto bailout;
	}

	scsi_mode_select_len(&ccb->csio,
						 /*retries*/ 1,
						 /*cbfcnp*/ NULL,
						 /*tag_action*/ MSG_SIMPLE_Q_TAG,
						 /*scsi_page_fmt*/ 0,
						 /*save_pages*/ 0,
						 /*param_buf*/ buf,
						 /*param_len*/ size,
						 /*minimum_cmd_len*/ 10,
						 /*sense_len*/ SSD_FULL_SIZE,
						 /*timeout*/ timeout);

	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS | CAM_PASS_ERR_RECOVER;

	rc = camtape_send_ccb(softc, ccb, &msg);

	if (rc != DEVICE_GOOD) {
		if (rc == -EDEV_MODE_PARAMETER_ROUNDED)
			rc = DEVICE_GOOD;

		if (rc != DEVICE_GOOD)
			camtape_process_errors(device, rc, msg, "modeselect", true);
	}
bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_MODESELECT));
	return rc;
}

/**
 * Prevent medium removal
 * @param device a pointer to the camtape backend
 * @return 0 on success or a negative value on error
 */
int camtape_prevent_medium_removal(void *device)
{
	struct camtape_data *softc = (struct camtape_data *)device;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_PREVENTM));
	ltfsmsg(LTFS_DEBUG, 31392D, "prevent medium removal", softc->drive_serial);

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_PREVENTM));

	/*
	 * The FreeBSD tape driver issues a prevent medium removal command on open, so we don't
	 * need to do it again.
	 */
	return DEVICE_GOOD;
}

/**
 * Allow medium removal
 * @param device a pointer to the camtape backend
 * @return 0 on success or a negative value on error
 */
int camtape_allow_medium_removal(void *device)
{
	struct camtape_data *softc = (struct camtape_data *)device;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_ALLOWMREM));
	ltfsmsg(LTFS_DEBUG, 31392D, "allow medium removal", softc->drive_serial);

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_ALLOWMREM));
	/*
	 * The FreeBSD tape driver issues an allow medium removal command on close, so we don't
	 * need to do it again.
	 */
	return DEVICE_GOOD;
}

/**
 * Read attribute
 * @param device a pointer to the camtape backend
 * @param part partition to read attribute
 * @param id attribute id to get
 * @param buf pointer to the attribute buffer. This function will update this value.
 * @param size length of the buffer
 * @return 0 on success or a negative value on error
 */
int camtape_read_attribute(void *device, const tape_partition_t part, const uint16_t id,
						   unsigned char *buf, const size_t size)
{
	int rc = DEVICE_GOOD;
	char *msg = NULL;
	bool take_dump= true;
	int timeout;
	struct camtape_data *softc = (struct camtape_data *)device;
	struct scsi_read_attribute_values *attr_header = NULL;
	size_t attr_size;
	union ccb *ccb = NULL;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_READATTR));
	ltfsmsg(LTFS_DEBUG3, 31397D, "readattr", (unsigned long long)part,
			(unsigned long long)id, softc->drive_serial);

	ccb = cam_getccb(softc->cd);
	if (ccb == NULL) {
		rc = -EDEV_NO_MEMORY;
		goto bailout;
	}

	memset(&(&ccb->ccb_h)[1], 0,
		sizeof(struct ccb_scsiio) - sizeof(struct ccb_hdr));

	/*
	 * The function interface only includes enough space for the attribute, and doesn't include
	 * space for the header.  So we need to allocate that here.
	 */
	attr_size = size + sizeof(*attr_header);
	attr_header = calloc(1, attr_size);
	if (attr_header == NULL) {
		rc = -EDEV_NO_MEMORY;
		goto bailout;
	}

	timeout = camtape_get_timeout(softc->timeouts, READ_ATTRIBUTE);
	if (timeout < 0) {
		rc = -EDEV_UNSUPPORETD_COMMAND;
		goto bailout;
	}

	scsi_read_attribute(&ccb->csio,
						/*retries*/ 1,
						/*cbfcnp*/ NULL,
						/*tag_action*/ MSG_SIMPLE_Q_TAG,
						/*service_action*/ SRA_SA_ATTR_VALUES,
						/*element*/ 0,
						/*elem_type*/ 0,
						/*logical_volume*/ 0,
						/*partition*/ part,
						/*first_attribute*/ id,
						/*cache*/ 0,
						/*data_ptr*/ (uint8_t *)attr_header,
						/*length*/ attr_size,
						/*sense_len*/ SSD_FULL_SIZE,
						/*timeout*/ timeout);

	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS | CAM_PASS_ERR_RECOVER;

	rc = camtape_send_ccb(softc, ccb, &msg);

	if (rc != DEVICE_GOOD) {
		if ( rc == -EDEV_INVALID_FIELD_CDB )
			take_dump = false;

		camtape_process_errors(device, rc, msg, "readattr", take_dump);

		if (rc < 0 &&
			id != TC_MAM_PAGE_COHERENCY &&
			id != TC_MAM_APP_VENDER &&
			id != TC_MAM_APP_NAME &&
			id != TC_MAM_APP_VERSION &&
			id != TC_MAM_USER_MEDIUM_LABEL &&
			id != TC_MAM_TEXT_LOCALIZATION_IDENTIFIER &&
			id != TC_MAM_BARCODE &&
			id != TC_MAM_APP_FORMAT_VERSION)
			ltfsmsg(LTFS_INFO, 31260I, rc);
	} else {
		memcpy(buf, &attr_header[1], size);
	}

bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);
	if (attr_header != NULL)
		free(attr_header);

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READATTR));
	return rc;
}

/**
 * Write attribute
 * @param device a pointer to the camtape backend
 * @param part partition to write attribute
 * @param buf pointer to the attribute buffer as defined in SPC-4 7.4.1
 * 	  "Attribute Format". The header for the parameter list is created based
 * 	  on the size of the attribute buffer
 * @param size length of the attribute buffer
 * @return 0 on success or a negative value on error
 */
int camtape_write_attribute(void *device, const tape_partition_t part, const unsigned char *buf,
							const size_t size)
{
	int rc = DEVICE_GOOD;
	char *msg = NULL;
	struct camtape_data *softc = (struct camtape_data *)device;
	struct scsi_read_attribute_values *attr_header = NULL;
	size_t attr_size;
	int timeout;
	union ccb *ccb = NULL;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_WRITEATTR));
	ltfsmsg(LTFS_DEBUG3, 31394D, "writeattr", (unsigned long long)part,
			softc->drive_serial);

	ccb = cam_getccb(softc->cd);
	if (ccb == NULL) {
		rc = -EDEV_NO_MEMORY;
		goto bailout;
	}

	memset(&(&ccb->ccb_h)[1], 0,
		sizeof(struct ccb_scsiio) - sizeof(struct ccb_hdr));

	/*
	 * The function interface only includes enough space for the attribute, and doesn't include
	 * space for the header.  So we need to allocate that here, fill in the length field in the
	 * header (which should be ignored by the target) and copy the user's data in.
	 */
	attr_size = size + sizeof(*attr_header);
	attr_header = calloc(1, attr_size);
	if (attr_header == NULL) {
		ltfsmsg(LTFS_ERR, 10001E, "camtape_write_attribute: data buffer");
		rc = -EDEV_NO_MEMORY;
		goto bailout;
	}
	memcpy(&attr_header[1], buf, size);
	scsi_ulto4b(size, attr_header->length);

	timeout = camtape_get_timeout(softc->timeouts, WRITE_ATTRIBUTE);
	if (timeout < 0) {
		rc = -EDEV_UNSUPPORETD_COMMAND;
		goto bailout;
	}

	scsi_write_attribute(&ccb->csio,
						 /*retries*/ 1,
						 /*cbfcnp*/ NULL,
						 /*tag_action*/ MSG_SIMPLE_Q_TAG,
						 /*element*/ 0,
						 /*logical_volume*/ 0,
						 /*partition*/ part,
						 /*wtc*/ 1,
						 /*data_ptr*/ (uint8_t *)attr_header,
						 /*length*/ attr_size,
						 /*sense_len*/ SSD_FULL_SIZE,
						 /*timeout*/ timeout);

	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS | CAM_PASS_ERR_RECOVER;

	rc = camtape_send_ccb(softc, ccb, &msg);

	if (rc != DEVICE_GOOD)
		camtape_process_errors(device, rc, msg, "writeattr", true);

bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);
	if (attr_header != NULL)
		free(attr_header);

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_WRITEATTR));
	return rc;
}

int camtape_allow_overwrite(void *device, const struct tc_position pos)
{
	int rc;
	char *msg = NULL;
	struct camtape_data *softc = (struct camtape_data *)device;
	struct allow_data_overwrite append_pos;
	int timeout;
	union ccb *ccb;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_ALLOWOVERW));
	ltfsmsg(LTFS_DEBUG, 31397D, "allow overwrite", (unsigned long long)pos.partition,
		(unsigned long long)pos.block, softc->drive_serial);

	ccb = cam_getccb(softc->cd);
	if (ccb == NULL) {
		rc = -EDEV_NO_MEMORY;
		goto bailout;
	}

	memset(&(&ccb->ccb_h)[1], 0,
		sizeof(struct ccb_scsiio) - sizeof(struct ccb_hdr));

	timeout = camtape_get_timeout(softc->timeouts, ALLOW_OVERWRITE);
	if (timeout < 0) {
		rc = -EDEV_UNSUPPORETD_COMMAND;
		goto bailout;
	}

	memset(&append_pos, 0, sizeof(append_pos));

	scsi_allow_overwrite(&ccb->csio,
						 /*retries*/ 1,
						 /*cbfcnp*/ NULL,
						 /*tag_action*/ MSG_SIMPLE_Q_TAG,
						 /*allow_overwrite*/ SAO_ALLOW_OVERWRITE_CUR_POS,
						 /*partition*/ pos.partition,
						 /*logical_id*/ pos.block,
						 /*sense_len*/ SSD_FULL_SIZE,
						 /*timeout*/ timeout);

	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS | CAM_PASS_ERR_RECOVER;

	rc = camtape_send_ccb(softc, ccb, &msg);

	if (rc != DEVICE_GOOD) {
		if (rc == -EDEV_EOD_DETECTED) {
			ltfsmsg(LTFS_DEBUG, 31248D, "Allow Overwrite");
			rc = DEVICE_GOOD;
		}

		if (rc != DEVICE_GOOD)
			camtape_process_errors(device, rc, msg, "allow overwrite", true);
	}

bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_ALLOWOVERW));
	return rc;
}

/**
 * Set compression setting
 * @param device a pointer to the camtape backend
 * @param enable_compression enable: true, disable: false
 * @param pos a pointer to position data. This function will update position infomation.
 * @return 0 on success or a negative value on error
 */
int camtape_set_compression(void *device, const bool enable_compression, struct tc_position *pos)
{
	int rc;
	unsigned char buf[TC_MP_COMPRESSION_SIZE];
	struct camtape_data *softc = (struct camtape_data *)device;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_SETCOMPRS));
	rc = camtape_modesense(device, TC_MP_COMPRESSION, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
	if (rc != DEVICE_GOOD) {
		ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETCOMPRS));
		return rc;
	}

	buf[0] = 0x00;
	buf[1] = 0x00;
	if(enable_compression)
		buf[18] |= 0x80; /* Set DCE field*/
	else
		buf[18] &= 0x7F; /* Unset DCE field*/

	rc = camtape_modeselect(device, buf, sizeof(buf));

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETCOMPRS));
	return rc;
}

/**
 * Return drive setting in default
 * @param device a pointer to the camtape backend
 * @return 0 on success or a negative value on error
 */
int camtape_set_default(void *device)
{
	struct camtape_data *softc = (struct camtape_data *)device;
	int rc;
	unsigned char buf[TC_MP_READ_WRITE_CTRL_SIZE];
	char *msg = NULL;
	struct mtparamset sili_param;
	uint32_t eot_model;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_SETDEFAULT));
	/* Disable Read across EOD on 3592 drive */
	if (IS_ENTERPRISE(softc->drive_type)) {
		ltfsmsg(LTFS_DEBUG, 31392D, __FUNCTION__, "Disabling read across EOD");
		rc = camtape_modesense(device, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
		if (rc != DEVICE_GOOD) {
			ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
			return rc;
		}

		buf[0]  = 0x00;
		buf[1]  = 0x00;
		buf[24] = 0x0C;

		rc = camtape_modeselect(device, buf, sizeof(buf));
		if (rc != DEVICE_GOOD)
			goto bailout;
	}

	/* set SILI bit */
	ltfsmsg(LTFS_DEBUG, 31392D, __FUNCTION__, "Setting SILI bit");
	memset(&sili_param, 0, sizeof(sili_param));
	snprintf(sili_param.value_name, sizeof(sili_param.value_name), "sili");
	sili_param.value_type = MT_PARAM_SET_SIGNED;
	sili_param.value_len = sizeof(int);
	sili_param.value.value_signed = 1;

	/*
	 * XXX KDM the ibmtape backend retries setting the SILI bit 10 times.  Why?  Is this
	 * something that we need to do here?
	 */
	if (ioctl(softc->fd_sa, MTIOCPARAMSET, &sili_param) == -1) {
		msg = strdup("Error returned from MTIOCPARAMSET ioctl to set the SILI bit");
		rc = -EDEV_DRIVER_ERROR;
		camtape_process_errors(device, rc, msg, "set default parameter", true);
		goto bailout;
	}

	/* set logical block protection */
	if (global_data.crc_checking) {
		ltfsmsg(LTFS_DEBUG, 31392D, __FUNCTION__, "Setting LBP");
		rc = camtape_set_lbp(device, true);
	} else {
		ltfsmsg(LTFS_DEBUG, 31392D, __FUNCTION__, "Resetting LBP");
		rc = camtape_set_lbp(device, false);
	}
	if (rc != DEVICE_GOOD)
		goto bailout;

	ltfsmsg(LTFS_DEBUG, 31392D, __FUNCTION__, "Setting EOT model to 1FM");

	/*
	 * We have to set the EOT model to 1 filemark.  By default, FreeBSD uses two filemarks at
	 * the end of the tape unless the drive has a quirk entry to only use one.  LTFS checks
	 * (in ltfs_seek_index()) to make sure that the only thing beyond the index is a single
	 * terminating filemark.  If there is anything else (e.g. a second filemark), it
	 * thinks that the tape isn't consistent.  We could change this assumption in the code,
	 * or just live with it.  (We're doing the latter.)
	 */
	eot_model = 1;
	if (ioctl(softc->fd_sa, MTIOCSETEOTMODEL, &eot_model) == -1) {
		msg = strdup("Error returned from MTIOCSETEOTMODEL ioctl to set the EOT model to 1FM");
		rc = -EDEV_DRIVER_ERROR;
		camtape_process_errors(device, rc, msg, "set default parameter", true);
		goto bailout;
	}

bailout:

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
	return rc;
}

/**
 * Get cartridge health information
 * @param device a pointer to the camtape backend
 * @return 0 on success or a negative value on error
 */

#define LOG_TAPE_ALERT               (0x2E)
#define LOG_PERFORMANCE              (0x37)
#define LOG_PERFORMANCE_CAPACITY_SUB (0x64) // Scope(7-6): Mount Values
                                            // Level(5-4): Return Advanced Counters
                                            // Group(3-0): Capacity

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

enum {
	PERF_CART_CONDITION       = 0x0001,	/* < Media Efficiency */
	PERF_ACTIVE_CQ_LOSS_W     = 0x7113, /* < Active CQ loss Write */
};

static uint16_t perfstats[] = {
	PERF_CART_CONDITION,
};

int camtape_get_cartridge_health(void *device, struct tc_cartridge_health *cart_health)
{
	struct camtape_data *softc = (struct camtape_data *)device;
	unsigned char logdata[LOGSENSEPAGE];
	unsigned char buf[16];
	int param_size, i;
	uint64_t loghlt;
	int rc;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETCARTHLTH));

	/* Issue LogPage 0x37 */
	cart_health->tape_efficiency  = UNSUPPORTED_CARTRIDGE_HEALTH;
	rc = camtape_logsense(device, LOG_PERFORMANCE, logdata, LOGSENSEPAGE);
	if (rc)
		ltfsmsg(LTFS_INFO, 31261I, LOG_PERFORMANCE, rc, "get cart health");
	else {
		for(i = 0; i < (int)((sizeof(perfstats)/sizeof(perfstats[0]))); i++) { /* BEAM: loop doesn't iterate - Use loop for future enhancement. */
			if (parse_logPage(logdata, perfstats[i], &param_size, buf, 16)) {
				ltfsmsg(LTFS_INFO, 31262I, LOG_PERFORMANCE, "get cart health");
			} else {
				switch(param_size) {
				case sizeof(uint8_t):
					loghlt = (uint64_t)(buf[0]);
					break;
				case sizeof(uint16_t):
					loghlt = (uint64_t)ltfs_betou16(buf);
					break;
				case sizeof(uint32_t):
					loghlt = (uint32_t)ltfs_betou32(buf);
					break;
				case sizeof(uint64_t):
					loghlt = ltfs_betou64(buf);
					break;
				default:
					loghlt = UNSUPPORTED_CARTRIDGE_HEALTH;
					break;
				}

				switch(perfstats[i]) {
				case PERF_CART_CONDITION:
					cart_health->tape_efficiency = loghlt;
					break;
				default:
					break;
				}
			}
		}
	}

	/* Issue LogPage 0x17 */
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
	rc = camtape_logsense(device, LOG_VOLUMESTATS, logdata, LOGSENSEPAGE);
	if (rc)
		ltfsmsg(LTFS_INFO, 31261I, LOG_VOLUMESTATS, rc, "get cart health");
	else {
		for(i = 0; i < (int)((sizeof(volstats)/sizeof(volstats[0]))); i++) {
			if (parse_logPage(logdata, volstats[i], &param_size, buf, 16)) {
				ltfsmsg(LTFS_INFO, 31262I, LOG_VOLUMESTATS, "get cart health");
			} else {
				switch(param_size) {
				case sizeof(uint8_t):
					loghlt = (uint64_t)(buf[0]);
					break;
				case sizeof(uint16_t):
					loghlt = (uint64_t)ltfs_betou16(buf);
					break;
				case sizeof(uint32_t):
					loghlt = (uint32_t)ltfs_betou32(buf);
					break;
				case sizeof(uint64_t):
					loghlt = ltfs_betou64(buf);
					break;
				default:
					loghlt = UNSUPPORTED_CARTRIDGE_HEALTH;
					break;
				}

				switch(volstats[i]) {
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

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETCARTHLTH));
	return 0;
}

/**
 * Get tape alert from the drive this value shall be latched by backends and shall be cleard by
 * clear_tape_alert() on write clear method
 * @param device Device handle returned by the backend's open().
 * @param tape alert On success, the backend must fill this value with the tape alert
 *                    "-1" shows the unsupported value except tape alert.
 * @return 0 on success or a negative value on error.
 */
int camtape_get_tape_alert(void *device, uint64_t *tape_alert)
{
	unsigned char logdata[LOGSENSEPAGE];
	unsigned char buf[16];
	int param_size, i;
	int rc;
	uint64_t ta;
	struct camtape_data *softc = (struct camtape_data *) device;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETTAPEALT));
	/* Issue LogPage 0x2E */
	ta = 0;
	rc = camtape_logsense(device, LOG_TAPE_ALERT, logdata, LOGSENSEPAGE);
	if (rc)
		ltfsmsg(LTFS_INFO, 31261I, LOG_TAPE_ALERT, rc, "get tape alert");
	else {
		for(i = 1; i <= 64; i++) {
			if (parse_logPage(logdata, (uint16_t) i, &param_size, buf, 16)
				|| param_size != sizeof(uint8_t)) {
				ltfsmsg(LTFS_INFO, 31262I, LOG_TAPE_ALERT, "get tape alert");
				ta = 0;
			}

			if(buf[0])
				ta += (uint64_t)(1) << (i - 1);
		}
	}

	softc->tape_alert |= ta;
	*tape_alert = softc->tape_alert;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETTAPEALT));
	return rc;
}

/**
 * clear latched tape alert from the drive
 * @param device Device handle returned by the backend's open().
 * @param tape_alert value to clear tape alert. Backend shall be clear the specicied bits in this value.
 * @return 0 on success or a negative value on error.
 */
int camtape_clear_tape_alert(void *device, uint64_t tape_alert)
{
	struct camtape_data *softc = (struct camtape_data *) device;
	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_CLRTAPEALT));
	softc->tape_alert &= ~tape_alert;
	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_CLRTAPEALT));
	return 0;
}

typedef enum {
	MT_MAXIO			= 0,
	MT_CPI_MAXIO		= 1,
	MT_MAX_BLK			= 2,
	MT_MAX_EFF_IO_SIZE	= 3,
} camtape_block_index;

struct camtape_status_item req_block_items[] = {
	{"maxio", NULL },
	{"cpi_maxio", NULL },
	{"max_blk", NULL },
	{"max_effective_iosize", NULL }
};

#define	CT_NUM_BLOCK_ITEMS	(sizeof(req_block_items)/sizeof(req_block_items[0]))

/**
 * Get drive parameter
 * @param device a pointer to the camtape backend
 * @param drive_param pointer to the drive parameter infomation. This function will update this value.
 * @return 0 on success or a negative value on error
 */
uint32_t _camtape_get_block_limits(void *device)
{
	uint32_t length = 0;
	struct camtape_data *softc = (struct camtape_data *)device;
	struct camtape_status_item block_items[CT_NUM_BLOCK_ITEMS];
	struct mt_status_data mtinfo;
	char *msg;
	int rc;

	ltfsmsg(LTFS_DEBUG, 31392D, "read block limits", softc->drive_serial);

	memset(block_items, 0, sizeof(block_items));
	memset(&mtinfo, 0, sizeof(mtinfo));
	memcpy(block_items, req_block_items, MIN(sizeof(block_items), sizeof(req_block_items)));

	rc = camtape_getstatus(softc, &mtinfo, block_items, CT_NUM_BLOCK_ITEMS, &msg);

	if (rc != DEVICE_GOOD) {
		camtape_process_errors(device, rc, msg, "read block limits", true);
		goto bailout;
	}

	/*
	 * Start off with the maximum block size returned from READ BLOCK LIMITS.
	 */
	length = block_items[MT_MAX_BLK].entry->value_unsigned;

	/*
	 * Limit that by the maximum size supported by the driver and controller.
	 */
	length = MIN(length, block_items[MT_MAX_EFF_IO_SIZE].entry->value_unsigned);

bailout:
	camtape_free_mtinfo(softc, &mtinfo);

	return length;
}

int camtape_get_parameters(void *device, struct tc_current_param *params)
{
	struct camtape_data *softc = (struct camtape_data *)device;
	int rc = DEVICE_GOOD;
	unsigned char buf[TC_MP_MEDIUM_SENSE_SIZE];

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETPARAM));

	memset(params, 0x00, sizeof(*params));

	/*
	 * This calculation is slightly different than the Linux backend because FreeBSD has a
	 * different, more variable set of limits.  Depending on the controller, how the user has
	 * configured MAXPHYS in his kernel config file, and the tape drive, you can get varying
	 * values out of the _camtape_get_block_limits() function.  It could be as low as 128K,
	 * or more than 1MB.
	 *
	 * If the user has a configuration that allows him to do 1048580 byte transfers, and thus
	 * do CRC checking on media formatted with a 1MB blocksize, we will let him.  This gives us
	 * slightly better functionality than the Linux backend, which will let you format a tape
	 * with a 1MB block size, but then won't let you mount it with CRC checking turned on.
	 */
	if (global_data.crc_checking)
		params->max_blksize = MIN(_camtape_get_block_limits(device) - 4, LINUX_MAX_BLOCK_SIZE);
	else
		params->max_blksize = MIN(_camtape_get_block_limits(device), LINUX_MAX_BLOCK_SIZE);

	if (softc->loaded) {
		params->write_protected = 0;

		if (IS_ENTERPRISE(softc->drive_type)) {
			rc = camtape_modesense(device, TC_MP_MEDIUM_SENSE, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
			if (rc != DEVICE_GOOD) {
				ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETPARAM));
				return rc;
			}

			char wp_flag = buf[26];

			if (wp_flag & 0x80) {
				params->write_protected |= VOL_PHYSICAL_WP;
			} else if (wp_flag & 0x01) {
				params->write_protected |= VOL_PERM_WP;
			} else if (wp_flag & 0x10) {
				params->write_protected |= VOL_PERS_WP;
			}
		} else {
			rc = camtape_modesense(device, 0x00, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
			if (rc != DEVICE_GOOD) {
				ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETPARAM));
				return rc;
			}

			if ( (buf[3] & 0x80) )
				params->write_protected |= VOL_PHYSICAL_WP;
		}

		params->cart_type = softc->cart_type;
		params->density   = softc->density_code;
		/* TODO: Following field shall be implemented in the future */
		/*
		params->is_worm = softc->is_worm;
		if (IS_ENTERPRISE(softc->drive_type)) {
			if (softc->density_code & TEST_CRYPTO)
				params->is_encrypted = true;
		} else {
			// TODO: Store is_crypto based on LP17:200h
		}
		*/
	}

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETPARAM));
	return rc;
}

static const char *generate_product_name(const char *product_id)
{
	const char *product_name = "";
	int i = 0;

	for (i = 0; ibm_supported_drives[i]; ++i) {
		if (strncmp(ibm_supported_drives[i]->product_id, product_id, strlen(product_id)) == 0) {
			product_name = ibm_supported_drives[i]->product_name;
			break;
		}
	}

	return product_name;
}

/**
 * Get a list of available tape devices for LTFS found in the host. The caller is
 * responsible from allocating the buffer to contain the tape drive information
 * by get_device_count() call.
 * When buf is NULL, this function just returns an available tape device count.
 * @param[out] buf Pointer to tc_drive_info structure array.
 *             The backend must fill this structure when this paramater is not NULL.
 * @param count size of array in buf.
 * @return on success, available device count on this system or a negative value on error.
 */
int camtape_get_device_list(struct tc_drive_info *buf, int count)
{
	union ccb ccb;
	struct dev_match_pattern patterns[2];
	ssize_t bufsize;
	int fd, buf_index;
	unsigned int i;

	fd = open(XPT_DEVICE, O_RDWR);
	if (fd == -1) {
		ltfsmsg(LTFS_ERR, 31263E, XPT_DEVICE, errno);
		return (-EDEV_DEVICE_UNOPENABLE);
	}

	memset(&ccb, 0, sizeof(ccb));

	ccb.ccb_h.path_id = CAM_XPT_PATH_ID;
	ccb.ccb_h.target_id = CAM_TARGET_WILDCARD;
	ccb.ccb_h.target_lun = CAM_LUN_WILDCARD;

	ccb.ccb_h.func_code = XPT_DEV_MATCH;
	bufsize = sizeof(struct dev_match_result) * 100;
	ccb.cdm.match_buf_len = bufsize;
	ccb.cdm.matches = (struct dev_match_result *)malloc(bufsize);
	if (ccb.cdm.matches == NULL) {
		close(fd);
		return (-EDEV_NO_MEMORY);
	}

	ccb.cdm.num_matches = 0;

	memset(patterns, 0, sizeof(patterns));

	ccb.cdm.num_patterns = 2;
	ccb.cdm.pattern_buf_len = sizeof(patterns);
	ccb.cdm.patterns = patterns;
	patterns[0].type = DEV_MATCH_PERIPH;
	snprintf(patterns[0].pattern.periph_pattern.periph_name,
	    sizeof(patterns[0].pattern.periph_pattern.periph_name), "sa");
	patterns[0].pattern.periph_pattern.flags = PERIPH_MATCH_NAME;
	patterns[1].type = DEV_MATCH_DEVICE;
	patterns[1].pattern.device_pattern.flags = DEV_MATCH_INQUIRY;
	patterns[1].pattern.device_pattern.data.inq_pat.type = T_SEQUENTIAL;
	patterns[1].pattern.device_pattern.data.inq_pat.media_type = SIP_MEDIA_REMOVABLE;
	snprintf(patterns[1].pattern.device_pattern.data.inq_pat.vendor,
	    sizeof(patterns[1].pattern.device_pattern.data.inq_pat.vendor), "*");
	snprintf(patterns[1].pattern.device_pattern.data.inq_pat.product,
	    sizeof(patterns[1].pattern.device_pattern.data.inq_pat.product), "*");
	snprintf(patterns[1].pattern.device_pattern.data.inq_pat.revision,
	    sizeof(patterns[1].pattern.device_pattern.data.inq_pat.revision), "*");

	buf_index = 0;

	do {
		if (ioctl(fd, CAMIOCOMMAND, &ccb) == -1)
			err(1, "error sending CAMIOCOMMAND ioctl to %s",
			    XPT_DEVICE);

		if ((ccb.ccb_h.status != CAM_REQ_CMP)
		 || ((ccb.cdm.status != CAM_DEV_MATCH_LAST)
		    && (ccb.cdm.status != CAM_DEV_MATCH_MORE)))
			errx(1, "got CAM error %#x, CDM error %d\n",
			      ccb.ccb_h.status, ccb.cdm.status);

		for (i = 0; i < ccb.cdm.num_matches; i++) {
			switch (ccb.cdm.matches[i].type) {
			case DEV_MATCH_DEVICE: {
				struct device_match_result *dev_result;
				uint8_t vendor[16], product[48], revision[16];

				dev_result = &ccb.cdm.matches[i].result.device_result;

				if ((dev_result->protocol == PROTO_SCSI) && (buf != NULL)) {
				    cam_strvis(vendor, (const uint8_t *)dev_result->inq_data.vendor,
					   sizeof(dev_result->inq_data.vendor), sizeof(vendor));
				    cam_strvis(product, (const uint8_t *) dev_result->inq_data.product,
					   sizeof(dev_result->inq_data.product), sizeof(product));
				    cam_strvis(revision, (const uint8_t *)dev_result->inq_data.revision,
					  sizeof(dev_result->inq_data.revision), sizeof(revision));
					snprintf(buf[buf_index].vendor, sizeof(buf[buf_index].vendor), "%s", vendor);
					snprintf(buf[buf_index].model, sizeof(buf[buf_index].model), "%s", product);
					snprintf(buf[buf_index].product_name, sizeof(buf[buf_index].product_name), "%s",
						generate_product_name((const char *)product));
				} else {
					/*
					 * XXX KDM what now?  We have a tape device that isn't SCSI??
					 */
				}
				break;
			}
			case DEV_MATCH_PERIPH: {
				struct periph_match_result *periph_result;

				periph_result =
				      &ccb.cdm.matches[i].result.periph_result;

				if (buf != NULL) {
					struct cam_device *dev;

					dev = cam_open_spec_device(
						periph_result->periph_name,
						periph_result->unit_number, O_RDWR, NULL);
					if (dev == NULL) {
						err(1, "unable to open passthrough "
						    "device for %s%d",
						    periph_result->periph_name,
						    periph_result->unit_number);
					}
					dev->serial_num[dev->serial_num_len] = '\0';
					snprintf(buf[buf_index].serial_number, sizeof(buf[buf_index].serial_number),
					    "%s", dev->serial_num);
					snprintf(buf[buf_index].name, sizeof(buf[buf_index].name), "%s%d",
						periph_result->periph_name, periph_result->unit_number);
					cam_close_device(dev);
				}
				buf_index++;
				if (buf != NULL && buf_index >= count)
					goto bailout;
				break;
			}
			default:
				break;
			}
		}

	} while ((ccb.ccb_h.status == CAM_REQ_CMP)
		&& (ccb.cdm.status == CAM_DEV_MATCH_MORE));

bailout:

	close(fd);
	if (ccb.cdm.matches != NULL)
		free(ccb.cdm.matches);

	return buf_index;
}

/**
 * Set the capacity proprotion of the medium
 * @param device a pointer to the camtape backend
 * @param proportion specify the proportion
 * @return 0 on success or a negative value on error
 */
int camtape_setcap(void *device, uint16_t proportion)
{
	int rc;
	char *msg = NULL;
	struct camtape_data *softc = (struct camtape_data *)device;
	unsigned char buf[TC_MP_MEDIUM_SENSE_SIZE];
	union ccb *ccb = NULL;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_SETCAP));
	if (IS_ENTERPRISE(softc->drive_type)) {
		memset(buf, 0, sizeof(buf));

		/* Scale media instead of setcap */
		rc = camtape_modesense(device, TC_MP_MEDIUM_SENSE, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
		if (rc < 0) {
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETCAP));
			return rc;
		}

		/* Check Cartridge type */
		if (IS_SHORT_MEDIUM(buf[2]) || IS_WORM_MEDIUM(buf[2])) {
			/* Short or WORM cartridge cannot be scaled */
			ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETCAP));
			return DEVICE_GOOD;
		}

		buf[0]   = 0x00;
		buf[1]   = 0x00;
		buf[27] |= 0x01;
		buf[28]  = 0x00;

		rc = camtape_modeselect(device, buf, sizeof(buf));
	} else {
		int timeout;

		ccb = cam_getccb(softc->cd);
		if (ccb == NULL) {
			rc = -EDEV_NO_MEMORY;
			goto bailout;
		}

		memset(&(&ccb->ccb_h)[1], 0,
			sizeof(struct ccb_scsiio) - sizeof(struct ccb_hdr));

		timeout = camtape_get_timeout(softc->timeouts, SET_CAPACITY);
		if (timeout < 0) {
			rc = -EDEV_UNSUPPORETD_COMMAND;
			goto bailout;
		}

		scsi_set_capacity(&ccb->csio,
						  /*retries*/ 1,
						  /*cbfcnp*/ NULL,
						  /*tag_action*/ MSG_SIMPLE_Q_TAG,
						  /*byte1*/ 0,
						  /*proportion*/ proportion,
						  /*sense_len*/ SSD_FULL_SIZE,
						  /*timeout*/ timeout);

		ccb->ccb_h.flags |= CAM_DEV_QFRZDIS | CAM_PASS_ERR_RECOVER;

		rc = camtape_send_ccb(softc, ccb, &msg);

		if (rc != DEVICE_GOOD) {
			camtape_process_errors(device, rc, msg, "modeselect", true);
		}
	}

bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);
	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETCAP));
	return rc;
}

/**
 * Get EOD status of a partition.
 * @param device Device handle returned by the backend's open().
 * @param part Partition to read the parameter from.
 * @return enum eod_status or UNSUPPORTED_FUNCTION if not supported.
 */
#define LOG_VOL_STATISTICS         (0x17)
#define LOG_VOL_USED_CAPACITY      (0x203)
#define LOG_VOL_PART_HEADER_SIZE   (4)

int camtape_get_eod_status(void *device, int part)
{
	/*
	 * This feature requires new tape drive firmware
	 * to support logpage 17h correctly
	 */

	struct camtape_data *softc = (struct camtape_data *)device;
	unsigned char logdata[LOGSENSEPAGE];
	unsigned char buf[16] = {0};
	int param_size, rc;
	unsigned int i;
	uint32_t part_cap[2] = {EOD_UNKNOWN, EOD_UNKNOWN};

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETEODSTAT));
	/* Issue LogPage 0x17 */
	rc = camtape_logsense(device, LOG_VOL_STATISTICS, logdata, LOGSENSEPAGE);
	if (rc) {
		ltfsmsg(LTFS_WARN, 31264W, LOG_VOL_STATISTICS, rc);
		ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETEODSTAT));
		return EOD_UNKNOWN;
	}

	/* Parse Approximate used native capacity of partitions (0x203)*/
	if (parse_logPage(logdata, (uint16_t)LOG_VOL_USED_CAPACITY, &param_size, buf, sizeof(buf))
		|| (param_size != sizeof(buf) ) ) {
		ltfsmsg(LTFS_WARN, 31265W);
		ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETEODSTAT));
		return EOD_UNKNOWN;
	}

	i = 0;
	while (i + LOG_VOL_PART_HEADER_SIZE <= sizeof(buf)) {
		unsigned char len;
		uint16_t part_buf;

		len = buf[i];
		part_buf = (uint16_t)(buf[i + 2] << 8) + (uint16_t) buf[i + 3];
		/* actual length - 1 is stored into len */
		if ( (len - LOG_VOL_PART_HEADER_SIZE + 1) == sizeof(uint32_t) && part_buf < 2) {
			part_cap[part_buf] = ((uint32_t) buf[i + 4] << 24) +
				((uint32_t) buf[i + 5] << 16) +
				((uint32_t) buf[i + 6] << 8) +
				(uint32_t) buf[i + 7];
		} else
			ltfsmsg(LTFS_WARN, 31266W, i, part_buf, len);

		i += (len + 1);
	}

	/* Create return value */
	if(part_cap[part] == 0xFFFFFFFF)
		rc = EOD_MISSING;
	else
		rc = EOD_GOOD;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETEODSTAT));
	return rc;
}

/**
 * Get vendor unique backend xattr
 * @param device Device handle returned by the backend's open().
 * @param name   Name of xattr
 * @param buf    On success, fill this value with the pointer of data buffer for xattr
 * @return 0 on success or a negative value on error.
 */
int camtape_get_xattr(void *device, const char *name, char **buf)
{
	struct camtape_data *softc = (struct camtape_data *) device;
	unsigned char logdata[LOGSENSEPAGE];
	unsigned char logbuf[16];
	int param_size;
	int rc = -LTFS_NO_XATTR;
	uint32_t value32;
	struct ltfs_timespec now;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETXATTR));
	if (! strcmp(name, "ltfs.vendor.IBM.mediaCQsLossRate")) {
		rc = DEVICE_GOOD;

		/* If first fetch or cache value is too old and valuie is dirty, refetch value */
		get_current_timespec(&now);
		if ( softc->fetch_sec_acq_loss_w == 0 ||
			 ((softc->fetch_sec_acq_loss_w + 60 < now.tv_sec) && softc->dirty_acq_loss_w)) {
			rc = camtape_logsense_page(device, LOG_PERFORMANCE, LOG_PERFORMANCE_CAPACITY_SUB,
									   logdata, LOGSENSEPAGE);
			if (rc)
				ltfsmsg(LTFS_INFO, 31261I, LOG_PERFORMANCE, rc, "get xattr");
			else {
				if (parse_logPage(logdata, PERF_ACTIVE_CQ_LOSS_W, &param_size, logbuf, 16)) {
					ltfsmsg(LTFS_INFO, 31262I, LOG_PERFORMANCE, "get xattr");
					rc = -LTFS_NO_XATTR;
				} else {
					switch(param_size) {
					case sizeof(uint32_t):
						value32 = (uint32_t)ltfs_betou32(logbuf);
						softc->acq_loss_w = (float)value32 / 65536.0;
						softc->fetch_sec_acq_loss_w = now.tv_sec;
						softc->dirty_acq_loss_w = false;
						break;
					default:
						ltfsmsg(LTFS_INFO, 31267I, param_size);
						rc = -LTFS_NO_XATTR;
						break;
					}
				}
			}
		}

		if(rc == DEVICE_GOOD) {
			/* The buf allocated here shall be freed in xattr_get_virtual() */
			rc = asprintf(buf, "%2.2f", softc->acq_loss_w);
			if (rc < 0) {
				rc = -LTFS_NO_MEMORY;
				ltfsmsg(LTFS_INFO, 31268I, "getting active CQ loss write");
			}
			else
				rc = DEVICE_GOOD;
		} else
			softc->fetch_sec_acq_loss_w = 0;
	}

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETXATTR));
	return rc;
}

/**
 * Get vendor unique backend xattr
 * @param device Device handle returned by the backend's open().
 * @param name   Name of xattr
 * @param buf    Data buffer to set the value
 * @param size   Length of data buffer
 * @return 0 on success or a negative value on error.
 */
int camtape_set_xattr(void *device, const char *name, const char *buf, size_t size)
{
	int rc = -LTFS_NO_XATTR;
	char *null_terminated;
	struct camtape_data *softc = (struct camtape_data *)device;


	if (!size)
		return -LTFS_BAD_ARG;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_SETXATTR));

	null_terminated = malloc(size + 1);
	if (! null_terminated) {
		ltfsmsg(LTFS_ERR, 10001E, "lin_tape_ibmtape_set_xattr: null_term");
		ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETXATTR));
		return -LTFS_NO_MEMORY;
	}
	memcpy(null_terminated, buf, size);
	null_terminated[size] = '\0';

	if (! strcmp(name, "ltfs.vendor.IBM.forceErrorWrite")) {
		softc->force_writeperm = strtoull(null_terminated, NULL, 0);
		if (softc->force_writeperm && softc->force_writeperm < THRESHOLD_FORCE_WRITE_NO_WRITE)
			softc->force_writeperm = THRESHOLD_FORCE_WRITE_NO_WRITE;
		rc = DEVICE_GOOD;
	} else if (! strcmp(name, "ltfs.vendor.IBM.forceErrorType")) {
		softc->force_errortype = strtol(null_terminated, NULL, 0);
		rc = DEVICE_GOOD;
	} else if (! strcmp(name, "ltfs.vendor.IBM.forceErrorRead")) {
		softc->force_readperm = strtoull(null_terminated, NULL, 0);
		softc->read_counter = 0;
		rc = DEVICE_GOOD;
	}
	free(null_terminated);

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETXATTR));
	return rc;
}

void camtape_help_message(void)
{
	ltfsresult(31399I, camtape_default_device);
}

const char *camtape_default_device_name(void)
{
	const char *devname;

	devname = camtape_default_device;
	return devname;
}

char *camtape_enc_state_to_str(camtape_encryption_state encryption_state)
{
	char *state = NULL;

	switch (encryption_state) {
	case CT_ENC_STATE_OFF:
		state = "Off";
		break;
	case CT_ENC_STATE_ON:
		state = "On";
		break;
	case CT_ENC_STATE_NA:
		state = "N/A";
		break;
	case CT_ENC_STATE_UNKNOWN:
	default:
		state = "Unknown";
		break;
	}

	return (state);
}

char *camtape_enc_method_to_str(camtape_encryption_method encryption_method)
{
	char *method = NULL;

	switch (encryption_method) {
	case CT_ENC_METHOD_NONE:
		method = "None";
		break;
	case CT_ENC_METHOD_SYSTEM:
		method = "System";
		break;
	case CT_ENC_METHOD_CONTROLLER:
		method = "Controller";
		break;
	case CT_ENC_METHOD_APPLICATION:
		method = "Application";
		break;
	case CT_ENC_METHOD_LIBRARY:
		method = "Library";
		break;
	case CT_ENC_METHOD_INTERNAL:
		method = "Internal";
		break;
	case CT_ENC_METHOD_CUSTOM:
		method = "Custom";
		break;
	default:
		method = "Unknown";
		break;
	}

	return (method);
}

static void ltfsmsg_encryption_state(const struct camtape_encryption_status * const es,
	const bool set)
{
	char s[128] = {'\0'};
	char *method = NULL;
	char *state = NULL;

	method = camtape_enc_method_to_str(es->encryption_method);

	state = camtape_enc_state_to_str(es->encryption_state);

	sprintf(s, "Capable = %d, Method = %s(%d), State = %s(%d)", es->encryption_capable,
			method, es->encryption_method, state, es->encryption_state);

	ltfsmsg(LTFS_DEBUG, 31392D, set ? "set encryption state:" : "get encryption state:", s);
}

static int camtape_get_encryption_state(void *device, struct camtape_encryption_status * const p,
	uint8_t *rwc_mode_buf, size_t rwc_buf_len, size_t *rwc_fill_len)
{
	int rc = DEVICE_GOOD;
	struct camtape_encryption_status es;
	uint8_t *buf = NULL;
	size_t buf_size = MAX_UINT16;
	struct scsi_mode_header_10 *mode_hdr;
	struct camtape_ibm_rw_control_page *rwc_page;
	struct camtape_ibm_initiator_spec_ext_page *ise_page;
	char *msg = NULL;

	buf = malloc(buf_size);
	if (buf == NULL) {
		rc = -EDEV_NO_MEMORY;
		goto bailout;
	}
	memset(buf, 0, buf_size);
	memset(&es, 0, sizeof(es));

	/*
	 * XXX KDM
	 * There is no way with the API for the modesense routine to figure out how much data
	 * we got in response to our request.  We allocate and ask for buf_size, and we can see how
	 * much data the target says is there, but we can't see if there is an underrun.  The API
	 * also always assumes that the function issues a 10 byte mode sense.
	 */
	rc = camtape_modesense(device, CT_ISE_PAGE_CODE, TC_MP_PC_CURRENT, 0x00, buf, buf_size);
	if (rc != DEVICE_GOOD)
		goto bailout;

	mode_hdr = (struct scsi_mode_header_10 *)buf;
	ise_page = (struct camtape_ibm_initiator_spec_ext_page *)find_mode_page_10(mode_hdr);

	/*
	 * We first check to see whether this drive is capable of encryption.  If not, there is
	 * no point in checking to see whether it is enabled.
	 */
	if (ise_page->support_flags & CT_ISE_ENCRYPTION_CAPABLE)
		es.encryption_capable = CT_ENC_CAPABLE;
	else
		es.encryption_capable = CT_ENC_NOT_CAPABLE;

	if (es.encryption_capable == CT_ENC_NOT_CAPABLE) {
		es.encryption_method = CT_ENC_METHOD_NONE;
		es.encryption_state = CT_ENC_STATE_OFF;
		goto bailout;
	}

	memset(buf, 0, buf_size);

	rc = camtape_modesense(device, CT_RWC_PAGE_CODE, TC_MP_PC_CURRENT, 0x00, buf, buf_size);

	if (rc != DEVICE_GOOD)
		goto bailout;

	/*
	 * Give the caller a copy of the buffer if he requested it.
	 */
	if ((rwc_mode_buf != NULL) && (rwc_buf_len > 0)) {
		*rwc_fill_len = MIN(buf_size, rwc_buf_len);
		memcpy(rwc_mode_buf, buf, *rwc_fill_len);
	}

	mode_hdr = (struct scsi_mode_header_10 *)buf;
	rwc_page = (struct camtape_ibm_rw_control_page *)find_mode_page_10(mode_hdr);

	/*
	 * In the IBM Linux tape driver, the difference between "system" and "application"
	 * encryption appears to be whether an application requested turning encryption on, or
	 * whether encryption was enabled when the driver loaded or via a sysfs/procfs request.
	 *
	 * The defines for encryption_state in struct encryption_status are the values for the
	 * bottom 2 bits of the encryption_state byte in the mode page verbatim.
	 */
	es.encryption_method = rwc_page->encryption_method;
	switch (rwc_page->encryption_method) {
	case CT_RWC_ENC_METHOD_NONE:
		es.encryption_state = CT_ENC_STATE_OFF;
		break;
	case CT_RWC_ENC_METHOD_SYSTEM:
		es.encryption_state = rwc_page->encryption_state & CT_RWC_ENCRYPTION_STATE_MASK;
		break;
	case CT_RWC_ENC_METHOD_APPLICATION:
		es.encryption_state = rwc_page->encryption_state & CT_RWC_ENCRYPTION_STATE_MASK;
		break;
	case CT_RWC_ENC_METHOD_LIBRARY:
	case CT_RWC_ENC_METHOD_CUSTOM:
	case CT_RWC_ENC_METHOD_INTERNAL:
	case CT_RWC_ENC_METHOD_CONTROLLER:
		es.encryption_state = CT_ENC_STATE_NA;
		break;
	default:
		es.encryption_method = CT_ENC_METHOD_UNKNOWN;
		es.encryption_state = CT_ENC_STATE_NA;
		break;
	}

bailout:

	ltfsmsg_encryption_state(&es, false);

	if (rc != DEVICE_GOOD)
		camtape_process_errors(device, rc, msg, "get encryption state", true);

	if (p != NULL) {
		if (rc == DEVICE_GOOD)
			memcpy(p, &es, sizeof(es));
		else
			memset(p, 0, sizeof(es));
	}

	if (buf != NULL)
		free(buf);

	return rc;
}

static int camtape_set_encryption_state(struct camtape_data *softc,
	camtape_encryption_state encryption_state)
{
	int rc = DEVICE_GOOD;
	struct camtape_encryption_status es;
	uint8_t *buf = NULL;
	size_t buf_size = MAX_UINT16, buf_fill_len = 0;
	struct scsi_mode_header_10 *mode_hdr;
	struct camtape_ibm_rw_control_page *rwc_page;
	char *msg = NULL;

	buf = malloc(buf_size);
	if (buf == NULL) {
		rc = -EDEV_NO_MEMORY;
		goto bailout;
	}
	memset(buf, 0, buf_size);

	rc = camtape_get_encryption_state(softc, &es, buf, buf_size, &buf_fill_len);
	if (rc != DEVICE_GOOD)
		goto bailout;

	if (es.encryption_capable == CT_ENC_NOT_CAPABLE) {
		rc = -EDEV_INVALID_ARG;
		goto bailout;
	}

	/* Nothing to do if we're already in the requested state. */
	if (encryption_state == es.encryption_state) {
		rc = DEVICE_GOOD;
		goto bailout;
	}

	mode_hdr = (struct scsi_mode_header_10 *)buf;
	rwc_page = (struct camtape_ibm_rw_control_page *)find_mode_page_10(mode_hdr);

	scsi_ulto2b(0, mode_hdr->data_length);
	rwc_page->encryption_state &= ~CT_RWC_ENCRYPTION_STATE_MASK;
	/*
	 * Note that this assumes the camtape_encryption_state enumeration and the defines for the
	 * mode page are the same.
	 */
	rwc_page->encryption_state |= encryption_state;

	rc = camtape_modeselect(softc, buf, buf_fill_len);

bailout:
	if (rc != DEVICE_GOOD)
		camtape_process_errors(softc, rc, msg, "set encryption state", true);
	else {
		es.encryption_state = encryption_state;
		ltfsmsg_encryption_state(&es, true);
	}

	if (buf != NULL)
		free(buf);
	return rc;
}

static void ltfsmsg_keyalias(const char * const title, const unsigned char * const keyalias)
{
	char s[128] = {'\0'};

	if (keyalias)
		sprintf(s, "keyalias = %c%c%c%02X%02X%02X%02X%02X%02X%02X%02X%02X", keyalias[0],
				keyalias[1], keyalias[2], keyalias[3], keyalias[4], keyalias[5], keyalias[6],
				keyalias[7], keyalias[8], keyalias[9], keyalias[10], keyalias[11]);
	else
		sprintf(s, "keyalias: NULL");

	ltfsmsg(LTFS_DEBUG, 31392D, title, s);
}

/*
 * Determine whether we're using Application Managed Encryption or some other method.
 */
static bool is_ame(struct camtape_data *softc)
{
	unsigned char buf[TC_MP_READ_WRITE_CTRL_SIZE] = {0};
	const int rc = camtape_modesense(softc, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));

	if (rc != 0) {
		char message[100] = {0};
		sprintf(message, "failed to get MP %02Xh (%d)", TC_MP_READ_WRITE_CTRL, rc);
		ltfsmsg(LTFS_DEBUG, 31392D, __FUNCTION__, message);

		return false; /* Consider that the encryption method is not AME */
	} else {
		struct scsi_mode_header_10 *mode_hdr;
		struct camtape_ibm_rw_control_page *rwc_page;
		char message[100] = {0};
		char *method = NULL;

		mode_hdr = (struct scsi_mode_header_10 *)buf;
		rwc_page = (struct camtape_ibm_rw_control_page *)find_mode_page_10(mode_hdr);

		method = camtape_enc_method_to_str(rwc_page->encryption_method);

		sprintf(message, "Encryption Method is %s (0x%02X)", method, rwc_page->encryption_method);
		ltfsmsg(LTFS_DEBUG, 31392D, __FUNCTION__, message);

		if (rwc_page->encryption_method != CT_RWC_ENC_METHOD_APPLICATION) {
			ltfsmsg(LTFS_ERR, 31269E, method, rwc_page->encryption_method);
		}
		return rwc_page->encryption_method == CT_RWC_ENC_METHOD_APPLICATION;
	}
}

static int is_encryption_capable(struct camtape_data *softc)
{

	/*
	 * XXX KDM why does this only support encryption, or at least Application Managed Encryption,
	 * on LTO drives and not on TS drives?
	 */
	if (IS_ENTERPRISE(softc->drive_type)) {
		ltfsmsg(LTFS_ERR, 31270E, softc->drive_type);
		return -EDEV_INTERNAL_ERROR;
	}

	if (! is_ame(softc))
		return -EDEV_INTERNAL_ERROR;

	return DEVICE_GOOD;
}

/*
 * See the comments in the mode page declaration.  This subpage is IBM-proprietary.  They don't
 * document it in any public documents, and the IBM Linux tape driver obfuscates all of the
 * functionality.  The only obvious parameters are the key and key alias/key index.
 */
static void camtape_fill_enc_subpage(struct camtape_ibm_enc_param_subpage *enc_sp,
	int key_index_set, const uint8_t *key, const uint8_t *key_index)
{
	int subpage_length;

	if (key_index_set != 0)
		subpage_length = CT_ENC_PARAM_KI_EXTRA_LENGTH;
	else
		subpage_length = CT_ENC_PARAM_NO_KI_EXTRA_LENGTH;
	scsi_ulto2b(subpage_length, enc_sp->page_length);

	enc_sp->desc1[0] = CT_ENC_PARAM_DESC_1_BYTE_0_VAL;
	enc_sp->desc1[1] = CT_ENC_PARAM_DESC_1_BYTE_1_VAL;
	enc_sp->desc1_length = subpage_length - CT_ENC_PARAM_DESC_1_ADDL_LENGTH_SUB;
	enc_sp->desc2_length = subpage_length - CT_ENC_PARAM_DESC_2_ADDL_LENGTH_SUB;
	enc_sp->desc3_length = subpage_length - CT_ENC_PARAM_DESC_3_ADDL_LENGTH_SUB;
	enc_sp->desc4[57] = CT_ENC_PARAM_DESC_4_BYTE_57_VAL;
	enc_sp->desc4[58] = CT_ENC_PARAM_DESC_4_BYTE_58_VAL;
	enc_sp->desc4_length = subpage_length - CT_ENC_PARAM_DESC_4_ADDL_LENGTH_SUB;
	enc_sp->byte76 &= CT_ENC_PARAM_BYTE_76_MASK;
	enc_sp->byte79 = CT_ENC_PARAM_BYTE_79_VALUE;
	enc_sp->byte80 = CT_ENC_PARAM_BYTE_80_VALUE;
	enc_sp->byte83 = CT_ENC_PARAM_BYTE_83_VALUE;
	memcpy(enc_sp->data_key, key, CT_ENC_PARAM_DATA_KEY_LEN);
	enc_sp->byte116 = CT_ENC_PARAM_BYTE_116_VALUE;
	if (key_index_set != 0)
		enc_sp->byte119 = CT_ENC_PARAM_BYTE_119_VALUE_1;
	else
		enc_sp->byte119 = CT_ENC_PARAM_BYTE_119_VALUE_2;
	enc_sp->byte121 = CT_ENC_PARAM_BYTE_121_VALUE;
	enc_sp->byte124 = CT_ENC_PARAM_BYTE_124_VALUE;
	if (key_index_set != 0) {
		enc_sp->ki_or_not.ki_is_set.byte127 = CT_ENC_PARAM_BYTE127_KI_VALUE;
		memcpy(enc_sp->ki_or_not.ki_is_set.key_index, key_index,
			sizeof(enc_sp->ki_or_not.ki_is_set.key_index));
		enc_sp->ki_or_not.ki_is_set.byte144 = CT_ENC_PARAM_BYTE144_KI_VALUE;
	} else {
		enc_sp->ki_or_not.ki_not_set.byte132 = CT_ENC_PARAM_BYTE132_NO_KI_VALUE;
	}
}

int camtape_set_key(void *device, const unsigned char * const keyalias, const unsigned char * const key)
{
	struct camtape_data *softc = (struct camtape_data *)device;
	camtape_encryption_state encryption_state = CT_ENC_STATE_OFF;
	struct scsi_mode_header_10 *mode_hdr;
	struct camtape_ibm_enc_param_subpage *enc_sp;
	const char * const title = "set key:";
	struct data_key dk = {{0}};
	uint8_t *buf = NULL;
	size_t bufsize = MAX_UINT16;
	char *msg = NULL;
	int rc;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_SETKEY));

	rc = is_encryption_capable(softc);
	if (rc < 0) {
		goto bailout;
	}

	buf = malloc(bufsize);
	if (buf == NULL) {
		rc = -EDEV_NO_MEMORY;
		goto bailout;
	}
	memset(buf, 0, bufsize);

	if (keyalias != NULL) {
		CHECK_ARG_NULL(key, -LTFS_NULL_ARG);
		encryption_state = CT_ENC_STATE_ON;
		memcpy(dk.data_key_index, keyalias, sizeof(dk.data_key_index));
		memcpy(dk.data_key, key, sizeof(dk.data_key));
	}
	dk.data_key_index_length = sizeof(dk.data_key_index);

	rc = camtape_set_encryption_state(device, encryption_state);
	if (rc != DEVICE_GOOD) {
		ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETKEY));
		return rc;
	}

	softc->is_data_key_set = keyalias != NULL;

	ltfsmsg_keyalias(title, keyalias);

	rc = camtape_modesense(device, CT_ENC_PARAM_SUBPAGE_PAGE_CODE, TC_MP_PC_CURRENT,
		CT_ENC_PARAM_SUBPAGE_CODE, buf, bufsize);

	if (rc != DEVICE_GOOD)
		goto bailout;

	mode_hdr = (struct scsi_mode_header_10 *)buf;
	enc_sp = (struct camtape_ibm_enc_param_subpage *)find_mode_page_10(mode_hdr);

	camtape_fill_enc_subpage(enc_sp, dk.data_key_index_length != 0, key, keyalias);

	scsi_ulto2b(0, mode_hdr->data_length);

	rc = camtape_modeselect(device, buf, bufsize);

bailout:
	if (rc != DEVICE_GOOD)
		camtape_process_errors(device, rc, msg, "set data key", true);

	if (buf != NULL)
		free(buf);

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETKEY));

	return rc;
}

static void show_hex_dump(const char * const title, const unsigned char * const buf, const uint size)
{
	/*
	 * "         1         2         3         4         5         6         7         8"
	 * "12345678901234567890123456789012345678901234567890123456789012345678901234567890"
	 * "xxxxxx  00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F  0123456789ABCDEF\n" < 100
	 */
	char * const s = calloc((size / 0x10 + 1) * 100, sizeof(char));
	char *p = s;
	uint i = 0;
	int j = 0;
	int k = 0;

	if (p == NULL)
		return;

	for (i = 0; i < size; ++i) {
		if (i % 0x10 == 0) {
			if (i) {
				for (j = 0x10; 0 < j; --j) {
					p += sprintf(p, "%c", isprint(buf[i-j]) ? buf[i-j] : '.');
				}
			}
			p += sprintf(p, "\n%06X  ", i);
		}
		p += sprintf(p, "%02X %s", buf[i] & 0xFF, i % 8 == 7 ? " " : "");
	}
	for (; (i + k) % 0x10; ++k) {
		p += sprintf(p, "   %s", (i + k) % 8 == 7 ? " " : "");
	}
	for (j = 0x10 - k; 0 < j; --j) {
		p += sprintf(p, "%c", isprint(buf[i-j]) ? buf[i-j] : '.');
	}

	ltfsmsg(LTFS_DEBUG, 31392D, title, s);
}

int camtape_get_keyalias(void *device, unsigned char **keyalias) /* This is not IBM method but T10 method. */
{
	struct camtape_data *softc = (struct camtape_data *)device;
	struct tde_next_block_enc_status_page *status_page;
	int page_header_length = 4;
	int buffer_length = page_header_length;
	uint8_t enc_status;
	union ccb *ccb = NULL;
	uint8_t *buf = NULL;
	char *msg = NULL;
	int i, rc = DEVICE_GOOD;
	int timeout;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETKEYALIAS));

	rc = is_encryption_capable(device);
	if (rc < 0) {
		ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETKEYALIAS));
		return rc;
	}
	memset(softc->dki, 0, sizeof(softc->dki));
	*keyalias = NULL;

	ccb = cam_getccb(softc->cd);
	if (ccb == NULL) {
		rc = -EDEV_NO_MEMORY;
		goto bailout;
	}

	timeout = camtape_get_timeout(softc->timeouts, SECURITY_PROTOCOL_IN);
	if (timeout < 0) {
		rc = -EDEV_UNSUPPORETD_COMMAND;
		goto bailout;
	}

	/*
	 * 1st loop: Get the page length.
	 * 2nd loop: Get full data in the page.
	 */
	for (i = 0; i < 2; ++i) {

		/* Prepare Data Buffer */
		if (buf != NULL) {
			free(buf);
			buf = NULL;
		}
		buf = (uint8_t *) calloc(buffer_length, sizeof(uint8_t));
		if (buf == NULL) {
			ltfsmsg(LTFS_ERR, 10001E, "camtape_get_keyalias: data buffer");
			rc = -EDEV_NO_MEMORY;
			goto bailout;
		}

		scsi_security_protocol_in(&ccb->csio,
								  /*retries*/ 1,
								  /*cbfcnp*/ NULL,
								  /*tag_action*/ MSG_SIMPLE_Q_TAG,
								  /*security_protocol*/ SPI_PROT_TAPE_DATA_ENC,
								  /*sps*/ TDE_NEXT_BLOCK_ENC_STATUS_PAGE,
								  /*byte4*/ 0,
								  /*data_ptr*/ buf,
								  /*dxfer_len*/ buffer_length,
								  /*sense_len*/ SSD_FULL_SIZE,
								  /*timeout*/ timeout);

		ccb->ccb_h.flags |= CAM_DEV_QFRZDIS | CAM_PASS_ERR_RECOVER;

		rc = camtape_send_ccb(softc, ccb, &msg);
		if (rc != DEVICE_GOOD) {
			camtape_process_errors(device, rc, msg, "get key-alias", true);
			goto bailout;
		}

		show_hex_dump("SPIN:", buf, buffer_length);

		status_page = (struct tde_next_block_enc_status_page *)buf;
		buffer_length = page_header_length + scsi_2btoul(status_page->page_length);
	}

	enc_status = status_page->status & TDE_NBES_ENC_STATUS_MASK;
	switch (enc_status) {
	case TDE_NBES_ENC_ALG_NOT_SUPPORTED:
	case TDE_NBES_ENC_SUPPORTED_ALG:
	case TDE_NBES_ENC_NO_KEY: {
		struct tde_data_enc_desc *desc;
		uint8_t *next_desc;

		for (desc = (struct tde_data_enc_desc *)&status_page[1];
			((((uint8_t *)desc - buf) + 4) <= buffer_length);
			desc = (struct tde_data_enc_desc *)next_desc) {
			uint32_t key_length;

			next_desc = (uint8_t *)desc;
			key_length = scsi_2btoul(desc->key_desc_length);
			next_desc += key_length + __offsetof(struct tde_data_enc_desc, key_desc);

			/*
			 * We're looking for the Authenticated Key-Associated Data descriptor.
			 */
			if (desc->key_desc_type != TDE_KEY_DESC_A_KAD)
				continue;

			/*
			 * Make sure that we aren't going off the end of the buffer.
			 */
			if ((next_desc - buf) > buffer_length)
				break;

			/*
			 * Copy the key into the softc and let the caller know we got it.
			 */
			memcpy(softc->dki, desc->key_desc, MIN(key_length, sizeof(softc->dki)));
			*keyalias = softc->dki;

			break;
		}
		break;
	}
	default:
		break;
	}

	const char * const title = "get key-alias:";
	ltfsmsg_keyalias(title, softc->dki);

bailout:

	if (ccb != NULL)
		cam_freeccb(ccb);
	if (buf != NULL)
		free(buf);

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETKEYALIAS));
	return rc;
}

typedef enum {
	CT_PP_LBP_R,
	CT_PP_LBP_W,
	CT_PP_RBDP,
	CT_PP_PI_LENGTH,
	CT_PP_PROT_METHOD
} ct_protect_param;

struct ct_protect_info {
	const char *name;
	struct mt_status_entry *entry;
	uint32_t value;
} ct_protect_list[] = {
	{ "lbp_r", NULL, 0 },
	{ "lbp_w", NULL, 0 },
	{ "rbdp", NULL, 0 },
	{ "pi_length", NULL, 0 },
	{ "prot_method", NULL, 0 }
};

#define CT_NUM_PROTECT_PARAMS   (sizeof(ct_protect_list)/sizeof(ct_protect_list[0]))
#define TC_MP_INIT_EXT_LBP_RS         (0x40)
#define TC_MP_INIT_EXT_LBP_CRC32C     (0x20)

int camtape_set_lbp(void *device, bool enable)
{
	struct camtape_data *softc = (struct camtape_data *)device;
	struct mt_status_data mtinfo;
	struct mt_status_entry *entry, *prot_entry;
	struct ct_protect_info protect_list[CT_NUM_PROTECT_PARAMS];
	struct mtparamset params[CT_NUM_PROTECT_PARAMS];
	struct mtsetlist param_list;
	char tmpname[64];
	char *msg = NULL;
	unsigned char lbp_method = LBP_DISABLE;
	unsigned char buf[TC_MP_INIT_EXT_SIZE];
	int rc = DEVICE_GOOD;
	unsigned int i;

	memset(buf, 0, sizeof(buf));

	/* Check logical block protection capability */
	rc = camtape_modesense(device, TC_MP_INIT_EXT, TC_MP_PC_CURRENT, 0x00, buf, sizeof(buf));
	if (rc < 0)
		return rc;

	if (buf[0x12] & TC_MP_INIT_EXT_LBP_CRC32C)
		lbp_method = CRC32C_CRC;
	else
		lbp_method = REED_SOLOMON_CRC;

	/*
	 * First grab all available parameter data.
	 */
	memset(&mtinfo, 0, sizeof(mtinfo));
	rc = camtape_get_mtinfo(softc, &mtinfo, /*params*/ 1, &msg);
	if (rc != DEVICE_GOOD)
		goto bailout;

	/*
	 * Check to see whether protection is supported.
	 */
	snprintf(tmpname, sizeof(tmpname), "%s.protection_supported", MT_PROTECTION_NAME);
	entry = mt_status_entry_find(&mtinfo, tmpname);
	if (entry == NULL) {
		msg = strdup("Cannot find sa(4) protection.protection_supported parameter");
		rc = -EDEV_INVALID_ARG;
		camtape_process_errors(device, rc, msg, "get lbp", true);
		goto bailout;
	}

	if (entry->value_signed != 1) {
		/*
		 * This device doesn't support logical block protection.  Nothing else to do here.
		 */
		ltfsmsg(LTFS_INFO, 31272I);
		goto bailout;
	}

	/*
	 * Get the base protection node.
	 */
	prot_entry = mt_status_entry_find(&mtinfo, MT_PROTECTION_NAME);
	if (prot_entry == NULL) {
		msg = strdup("Cannot find sa(4) protection node!");
		rc = -EDEV_INVALID_ARG;
		camtape_process_errors(device, rc, msg, "get lbp", true);
		goto bailout;
	}

	/* set logical block protection */
	ltfsmsg(LTFS_DEBUG, 31393D, "LBP Enable", enable, "");
	ltfsmsg(LTFS_DEBUG, 31393D, "LBP Method", lbp_method, "");

	memcpy(protect_list, ct_protect_list, MIN(sizeof(protect_list), sizeof(ct_protect_list)));

	if (enable) {
		protect_list[CT_PP_LBP_R].value = 1;
		protect_list[CT_PP_LBP_W].value = 1;
		/* IBM drives at least don't support RBDP */
		protect_list[CT_PP_RBDP].value = 0;
		/*
		 * XXX KDM add sa(4) defines for CRC32
		 */
		protect_list[CT_PP_PI_LENGTH].value = SA_CTRL_DP_RS_LENGTH;
		protect_list[CT_PP_PROT_METHOD].value = lbp_method;
	} else {
		protect_list[CT_PP_LBP_R].value = 0;
		protect_list[CT_PP_LBP_W].value = 0;
		protect_list[CT_PP_RBDP].value = 0;
		protect_list[CT_PP_PI_LENGTH].value = 0;
		protect_list[CT_PP_PROT_METHOD].value = 0;
	}

	for (i = 0; i < CT_NUM_PROTECT_PARAMS; i++) {
		entry = mt_entry_find(prot_entry, __DECONST(char *, protect_list[i].name));
		if (entry == NULL) {
			msg = strdup("Cannot find all protection information entries");
			rc = -EDEV_INVALID_ARG;
			camtape_process_errors(device, rc, msg, "get lbp", true);
			goto bailout;
		}
		protect_list[i].entry = entry;
		snprintf(params[i].value_name, sizeof(params[i].value_name), "%s.%s", MT_PROTECTION_NAME,
		    protect_list[i].name);
		params[i].value_type = MT_PARAM_SET_UNSIGNED;
		params[i].value_len = sizeof(protect_list[i].value);
		params[i].value.value_unsigned = protect_list[i].value;
	}

	param_list.num_params = CT_NUM_PROTECT_PARAMS;
	param_list.param_len = sizeof(params);
	param_list.params = params;

	if (ioctl(softc->fd_sa, MTIOCSETLIST, &param_list) == -1) {
		char tmpstr[512];

		snprintf(tmpstr, sizeof(tmpstr), "Error returned from MTIOCSETLIST ioctl to set "
			"protection parameters: %s", strerror(errno));
		msg = strdup(tmpstr);
		rc = -errno;
		camtape_process_errors(device, rc, msg, "get lbp", true);
		goto bailout;
	}

	for (i = 0; i < CT_NUM_PROTECT_PARAMS; i++) {
		if (params[i].status != MT_PARAM_STATUS_OK) {
			rc = -EDEV_DRIVER_ERROR;
			camtape_process_errors(device, rc, params[i].error_str, "get lbp", true);
			goto bailout;
		}
	}

	if (enable) {
		switch (lbp_method) {
		case CRC32C_CRC:
			softc->f_crc_enc = crc32c_enc;
			softc->f_crc_check = crc32c_check;
			break;
		case REED_SOLOMON_CRC:
			softc->f_crc_enc = rs_gf256_enc;
			softc->f_crc_check = rs_gf256_check;
			break;
		default:
			softc->f_crc_enc   = NULL;
			softc->f_crc_check = NULL;
			break;
		}
		ltfsmsg(LTFS_INFO, 31271I);
	} else {
		ltfsmsg(LTFS_INFO, 31272I);
	}

bailout:
	camtape_free_mtinfo(softc, &mtinfo);

	return rc;
}

int camtape_is_mountable(void *device, const char *barcode, const unsigned char cart_type,
						 const unsigned char density)
{
	int ret;
	struct camtape_data *softc = (struct camtape_data *)device;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_ISMOUNTABLE));

	ret = ibm_tape_is_mountable(softc->drive_type,
							   barcode,
							   cart_type,
							   density,
							   global_data.strict_drive);

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_ISMOUNTABLE));

	return ret;
}

bool camtape_is_readonly(void *device)
{
	int ret;
	struct camtape_data *softc = (struct camtape_data *)device;

	ret = ibm_tape_is_mountable(softc->drive_type,
							   NULL,
							   softc->cart_type,
							   softc->density_code,
							   global_data.strict_drive);

	if (ret == MEDIUM_READONLY)
		return true;
	else
		return false;
}

/* This function should be called after the cartridge is loaded. */
int camtape_get_worm_status(void *device, bool *is_worm)
{
	int rc = 0;
	struct camtape_data *softc = (struct camtape_data *) device;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETWORMSTAT));
	if (softc->loaded) {
		*is_worm = softc->is_worm;
	}
	else {
		ltfsmsg(LTFS_INFO, 31289I);
		*is_worm = false;
		rc = -1;
	}

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETWORMSTAT));
	return rc;
}

struct tape_ops camtape_drive_handler = {
	.open                   = camtape_open,
	.reopen                 = camtape_reopen,
	.close                  = camtape_close,
	.close_raw              = camtape_close_raw,
	.is_connected           = camtape_is_connected,
	.inquiry                = camtape_inquiry,
	.inquiry_page           = camtape_inquiry_page,
	.test_unit_ready        = camtape_test_unit_ready,
	.read                   = camtape_read,
	.write                  = camtape_write,
	.writefm                = camtape_writefm,
	.rewind                 = camtape_rewind,
	.locate                 = camtape_locate,
	.space                  = camtape_space,
	.erase                  = camtape_erase,
	.load                   = camtape_load,
	.unload                 = camtape_unload,
	.readpos                = camtape_readpos,
	.setcap                 = camtape_setcap,
	.format                 = camtape_format,
	.remaining_capacity     = camtape_remaining_capacity,
	.logsense               = camtape_logsense,
	.modesense              = camtape_modesense,
	.modeselect             = camtape_modeselect,
	.reserve_unit           = camtape_reserve_unit,
	.release_unit           = camtape_release_unit,
	.prevent_medium_removal = camtape_prevent_medium_removal,
	.allow_medium_removal   = camtape_allow_medium_removal,
	.write_attribute        = camtape_write_attribute,
	.read_attribute         = camtape_read_attribute,
	.allow_overwrite        = camtape_allow_overwrite,
	// May be command combination
	.set_compression        = camtape_set_compression,
	.set_default            = camtape_set_default,
	.get_cartridge_health   = camtape_get_cartridge_health,
	.get_tape_alert         = camtape_get_tape_alert,
	.clear_tape_alert       = camtape_clear_tape_alert,
	.get_xattr              = camtape_get_xattr,
	.set_xattr              = camtape_set_xattr,
	.get_parameters         = camtape_get_parameters,
	.get_eod_status         = camtape_get_eod_status,
	.get_device_list        = camtape_get_device_list,
	.help_message           = camtape_help_message,
	.parse_opts             = camtape_parse_opts,
	.default_device_name    = camtape_default_device_name,
	.set_key                = camtape_set_key,
	.get_keyalias           = camtape_get_keyalias,
	.takedump_drive         = camtape_takedump_drive,
	.is_mountable           = camtape_is_mountable,
	.get_worm_status		= camtape_get_worm_status,
	.get_serialnumber		= camtape_get_serialnumber,
	.set_profiler			= camtape_set_profiler,
	.get_block_in_buffer	= camtape_get_block_in_buffer,
	.is_readonly			= camtape_is_readonly
};

struct tape_ops *tape_dev_get_ops(void)
{
	return &camtape_drive_handler;
}

extern char tape_freebsd_cam_dat[];

const char *tape_dev_get_message_bundle_name(void **message_data)
{
	*message_data = tape_freebsd_cam_dat;
	return "tape_freebsd_cam";
}

/**
 * Given the SA device, opens the SA and Pass Thru devices for
 * the specified tape drive.
 *
 * @author Ryan Guthrie (ryang@spectralogic.com) (5/2/2013)
 *
 * @param softc the struct containing persistent information.
 *  						It is where the file descriptors for the pass
 *  						and SA are stored.
 * @param saDeviceName the SA (Sequential Access) device for the tape driver
 *
 * @return int 0 on success, or a negative value on error
 *  			 opening either the SA or PASS devices.
 */
int open_sa_pass(struct camtape_data *softc, const char *saDeviceName)
{
	int ret = 0;

	struct cam_device *cd_pass = cam_open_device(saDeviceName, O_RDWR);

	if (cd_pass == NULL) {
		ltfsmsg(LTFS_INFO, 31225I, saDeviceName, errno);
		return -EDEV_DEVICE_UNOPENABLE;
	}

	char passDeviceName[MAXPATHLEN+1];
	snprintf(passDeviceName, MAXPATHLEN, "/dev/%s%d", cd_pass->device_name, cd_pass->dev_unit_num);

	ret = open_sa_device(softc, saDeviceName);
	if (ret) {
		cam_close_device(cd_pass);
		ltfsmsg(LTFS_INFO, 31225I, saDeviceName, errno);
		return ret;
	}

	softc->cd = cd_pass;

	return 0;
}

int open_sa_device(struct camtape_data *softc, const char* saDeviceName)
{
	int ret = 0;

	softc->fd_sa = open(saDeviceName, O_RDWR | O_NDELAY);
	if (softc->fd_sa < 0) {
		softc->fd_sa = open(saDeviceName, O_RDONLY | O_NDELAY);
		if (softc->fd_sa < 0) {
			if (errno == EAGAIN) {
				ltfsmsg(LTFS_ERR, 31224E, saDeviceName);
				ret = -EDEV_DEVICE_BUSY;
			} else {
				ltfsmsg(LTFS_INFO, 31225I, saDeviceName, errno);
				ret = -EDEV_DEVICE_UNOPENABLE;
			}
			return ret;
		}
		ltfsmsg(LTFS_WARN, 31226W, saDeviceName);
	}

	return ret;
}

void close_sa_device(struct camtape_data *softc)
{
	if (softc->fd_sa > 0) {
		close(softc->fd_sa);
		softc->fd_sa = 0;
	}
}
void close_cd_pass_device(struct camtape_data *softc)
{
	if (softc->cd != NULL) {
		cam_close_device(softc->cd);
		softc->cd = NULL;
	}
}

#undef __camtape_tc_c
