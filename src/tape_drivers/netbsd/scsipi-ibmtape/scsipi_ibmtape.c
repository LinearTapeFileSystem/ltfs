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
** FILE NAME:       tape_drivers/scsipi/scsipi_ibmtape.c
**
** DESCRIPTION:     LTFS IBM tape drive backend implementation for scsipi driver
**
** AUTHORS:         Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/

#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/ioctl.h>

#include "ltfs_copyright.h"
#include "libltfs/ltfslogging.h"
#include "libltfs/fs.h"
#include "libltfs/ltfs_endian.h"
#include "libltfs/arch/time_internal.h"
#include "kmi/key_format_ltfs.h"

/* Common header of backend */
#include "reed_solomon_crc.h"
#include "crc32c_crc.h"
#include "ibm_tape.h"

/* SCSI command handling functions */
#include "scsipi_scsi_tape.h"

/* Definitions of this backend */
#include "scsipi_ibmtape.h"

#include "libltfs/ltfs_fuse_version.h"
#include <fuse.h>

volatile char *copyright = LTFS_COPYRIGHT_0"\n"LTFS_COPYRIGHT_1"\n"LTFS_COPYRIGHT_2"\n" \
	LTFS_COPYRIGHT_3"\n"LTFS_COPYRIGHT_4"\n"LTFS_COPYRIGHT_5"\n";

/* Default device name */
const char *default_device = "0";

/* Global values */
struct scsipi_ibmtape_global_data global_data;

/* Definitions */
#define LOG_PAGE_HEADER_SIZE      (4)
#define LOG_PAGE_PARAMSIZE_OFFSET (3)
#define LOG_PAGE_PARAM_OFFSET     (4)

#define SG_MAX_BLOCK_SIZE (64 * 1024)
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define TU_DEFAULT_TIMEOUT (60)
#define MAX_RETRY          (100)

/* Forward references (For keep function order to struct tape_ops) */
int scsipi_ibmtape_readpos(void *device, struct tc_position *pos);
int scsipi_ibmtape_locate(void *device, struct tc_position dest, struct tc_position *pos);
int scsipi_ibmtape_space(void *device, size_t count, TC_SPACE_TYPE type, struct tc_position *pos);
int scsipi_ibmtape_logsense(void *device, const unsigned char page, unsigned char *buf, const size_t size);
int scsipi_ibmtape_modesense(void *device, const unsigned char page, const TC_MP_PC_TYPE pc,
						 const unsigned char subpage, unsigned char *buf, const size_t size);
int scsipi_ibmtape_modeselect(void *device, unsigned char *buf, const size_t size);

/* Local functions */
static inline int _parse_logPage(const unsigned char *logdata,
								 const uint16_t param, uint32_t *param_size,
								 unsigned char *buf, const size_t bufsize)
{
	uint16_t page_len, param_code, param_len;
	uint32_t i;
	uint32_t ret = -EDEV_INTERNAL_ERROR;

	page_len = ((uint16_t)logdata[2] << 8) + (uint16_t)logdata[3];
	i = LOG_PAGE_HEADER_SIZE;

	while(i < page_len)
	{
		param_code = ((uint16_t)logdata[i] << 8) + (uint16_t)logdata[i+1];
		param_len  = (uint16_t)logdata[i + LOG_PAGE_PARAMSIZE_OFFSET];

		if(param_code == param)
		{
			*param_size = param_len;
			if(bufsize < param_len){
				memcpy(buf, &logdata[i + LOG_PAGE_PARAM_OFFSET], bufsize);
				ret = -EDEV_INTERNAL_ERROR;
				break;
			} else {
				memcpy(buf, &logdata[i + LOG_PAGE_PARAM_OFFSET], param_len);
				ret = DEVICE_GOOD;
				break;
			}
		}
		i += param_len + LOG_PAGE_PARAM_OFFSET;
	}

	return ret;
}

/**
 * Parse option for scsipi driver
 * Option value cannot be stored into device instance because option parser calls
 * before opening a device.
 * @param devname device name
 * @return a pointer to the backend on success or NULL on error
 */

#define scsipi_ibmtape_opt(templ,offset,value)								\
	{ templ, offsetof(struct scsipi_ibmtape_global_data, offset), value }

static struct fuse_opt scsipi_ibmtape_global_opts[] = {
	scsipi_ibmtape_opt("scsi_lbprotect=%s", str_crc_checking, 0),
	scsipi_ibmtape_opt("strict_drive",      strict_drive, 1),
	scsipi_ibmtape_opt("nostrict_drive",    strict_drive, 0),
	scsipi_ibmtape_opt("autodump",          disable_auto_dump, 0),
	scsipi_ibmtape_opt("noautodump",        disable_auto_dump, 1),
	FUSE_OPT_END
};

static int null_parser(void *priv, const char *arg, int key, struct fuse_args *outargs)
{
	return 1;
}

#define LBP_DISABLE             (0x00)
#define REED_SOLOMON_CRC        (0x01)
#define CRC32C_CRC              (0x02)

#define TC_MP_INIT_EXT_LBP_RS         (0x40)
#define TC_MP_INIT_EXT_LBP_CRC32C     (0x20)

static int _set_lbp(void *device, bool enable)
{
	int ret = -EDEV_UNKNOWN;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	unsigned char buf[TC_MP_SUB_DP_CTRL_SIZE];
	unsigned char buf_ext[TC_MP_INIT_EXT_SIZE];
	unsigned char lbp_method = LBP_DISABLE;

	/* Check logical block protection capability */
	ret = scsipi_ibmtape_modesense(device, TC_MP_INIT_EXT, TC_MP_PC_CURRENT, 0x00, buf_ext, sizeof(buf_ext));
	if (ret < 0)
		return ret;

	if (buf_ext[0x12] & TC_MP_INIT_EXT_LBP_CRC32C)
		lbp_method = CRC32C_CRC;
	else
		lbp_method = REED_SOLOMON_CRC;

	/* set logical block protection */
	ltfsmsg(LTFS_DEBUG, 30393D, "LBP Enable", enable, "");
	ltfsmsg(LTFS_DEBUG, 30393D, "LBP Method", lbp_method, "");
	ret = scsipi_ibmtape_modesense(device, TC_MP_CTRL, TC_MP_PC_CURRENT,
							   TC_MP_SUB_DP_CTRL, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	buf[0]  = 0x00;
	buf[1]  = 0x00;
	if (enable) {
		buf[20] = lbp_method;
		buf[21] = 0x04;
		buf[22] = 0xc0;
	} else {
		buf[20] = LBP_DISABLE;
		buf[21] = 0;
		buf[22] = 0;
	}

	ret = scsipi_ibmtape_modeselect(device, buf, sizeof(buf));

	if (ret == DEVICE_GOOD) {
		if (enable) {
			switch (lbp_method) {
				case CRC32C_CRC:
					priv->f_crc_enc = crc32c_enc;
					priv->f_crc_check = crc32c_check;
					break;
				case REED_SOLOMON_CRC:
					priv->f_crc_enc = rs_gf256_enc;
					priv->f_crc_check = rs_gf256_check;
					break;
				default:
					priv->f_crc_enc   = NULL;
					priv->f_crc_check = NULL;
					break;
			}
			ltfsmsg(LTFS_INFO, 30251I);
		} else {
			priv->f_crc_enc   = NULL;
			priv->f_crc_check = NULL;
			ltfsmsg(LTFS_INFO, 30252I);
		}
	}

	return ret;
}

static bool is_dump_required(struct scsipi_ibmtape_data *priv, int ret, bool *capture_unforced)
{
	bool ans = false;
	int err = -ret;

	if (err >= EDEV_NOT_READY && err < EDEV_INTERNAL_ERROR) {
		ans = true;
	}

	*capture_unforced = (IS_MEDIUM_ERROR(err) || IS_HARDWARE_ERROR(err));

	return ans;
}

#define DUMP_HEADER_SIZE   (4)
#define DUMP_TRANSFER_SIZE (512 * KB)

static int _cdb_read_buffer(void *device, int id, unsigned char *buf, size_t offset, size_t len, int type);
static int _cdb_force_dump(struct scsipi_ibmtape_data *priv);

static int _get_dump(struct scsipi_ibmtape_data *priv, char *fname)
{
	int ret = 0;

	long long               data_length, buf_offset;
	int                     dumpfd = -1;
	int                     transfer_size, num_transfers, excess_transfer;
	int                     i, bytes;
	unsigned char           cap_buf[DUMP_HEADER_SIZE];
	unsigned char           *dump_buf;
	int                     buf_id;

	ltfsmsg(LTFS_INFO, 30253I, fname);

	/* Set transfer size */
	transfer_size = DUMP_TRANSFER_SIZE;
	dump_buf = calloc(1, DUMP_TRANSFER_SIZE);
	if(!dump_buf){
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return -EDEV_NO_MEMORY;
	}

	/* Set buffer ID */
	if (IS_ENTERPRISE(priv->drive_type)) {
		buf_id = 0x00;
	} else {
		buf_id = 0x01;
	}

	/* Get buffer capacity */
	_cdb_read_buffer(priv, buf_id, cap_buf, 0, sizeof(cap_buf), 0x03);
	data_length = (cap_buf[1] << 16) + (cap_buf[2] << 8) + (int)cap_buf[3];

	/* Open dump file for write only*/
	dumpfd = open(fname, O_WRONLY|O_CREAT|O_TRUNC, 0666);
	if(dumpfd < 0){
		ltfsmsg(LTFS_WARN, 30254W, errno);
		free(dump_buf);
		return -2;
	}

	/* get the total number of transfers */
	num_transfers   = data_length / transfer_size;
	excess_transfer = data_length % transfer_size;
	if(excess_transfer)
		num_transfers += 1;

	/* Total dump data length is %lld. Total number of transfers is %d. */
	ltfsmsg(LTFS_DEBUG, 30255D, data_length);
	ltfsmsg(LTFS_DEBUG, 30256D, num_transfers);

	/* start to transfer data */
	buf_offset = 0;
	i = 0;
	ltfsmsg(LTFS_DEBUG, 30257D);
	while(num_transfers)
	{
		int length;

		i++;

		/* Allocation Length is transfer_size or excess_transfer*/
		if(excess_transfer && num_transfers == 1)
			length = excess_transfer;
		else
			length = transfer_size;

		ret = _cdb_read_buffer(priv, buf_id, dump_buf, buf_offset, length, 0x02);
		if (ret) {
			ltfsmsg(LTFS_WARN, 30258W, ret);
			free(dump_buf);
			close(dumpfd);
			return ret;
		}

		/* write buffer data into dump file */
		bytes = write(dumpfd, dump_buf, length);
		if(bytes == -1)
		{
			ltfsmsg(LTFS_WARN, 30259W, ret);
			free(dump_buf);
			close(dumpfd);
			return -1;
		}

		if(bytes != length)
		{
			ltfsmsg(LTFS_WARN, 30260W, bytes, length);
			free(dump_buf);
			close(dumpfd);
			return -2;
		}

		/* update offset and num_transfers, free buffer */
		buf_offset += transfer_size;
		num_transfers -= 1;

	} /* end of while(num_transfers) */

	close(dumpfd);

	return ret;
}

static int _take_dump(struct scsipi_ibmtape_data *priv, bool capture_unforced)
{
	char      fname_base[1024];
	char      fname[1024];
	time_t    now;
	struct tm *tm_now;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_TAKEDUMPDRV));

	/* Make base filename */
	time(&now);
	tm_now = localtime(&now);
	sprintf(fname_base, "/tmp/ltfs_%s_%d_%02d%02d_%02d%02d%02d"
			, priv->drive_serial
			, tm_now->tm_year+1900
			, tm_now->tm_mon+1
			, tm_now->tm_mday
			, tm_now->tm_hour
			, tm_now->tm_min
			, tm_now->tm_sec);

	if (capture_unforced) {
		ltfsmsg(LTFS_INFO, 30261I);
		strcpy(fname, fname_base);
		strcat(fname, ".dmp");
		_get_dump(priv, fname);
	}

	ltfsmsg(LTFS_INFO, 30262I);
	_cdb_force_dump(priv);
	strcpy(fname, fname_base);
	strcat(fname, "_f.dmp");
	_get_dump(priv, fname);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_TAKEDUMPDRV));

	return 0;
}

static int _raw_dev_open(const char *devname)
{
	int fd = -1;
	int flags = 0;

	/*
	 *  Open the device file exclusively with non-blocking first to fail another LTFS instance
	 *  to try to mount the same device.
	 */
	fd = open(devname, O_RDWR | O_EXCL | O_NONBLOCK);
	if (fd < 0) {
		ltfsmsg(LTFS_INFO, 30210I, devname, errno);
		return -EDEV_DEVICE_UNOPENABLE;
	}

	/* Get the device back to blocking mode */
	flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) {
		ltfsmsg(LTFS_INFO, 30211I, "get", errno);
		close(fd);
		return -EDEV_DEVICE_UNOPENABLE;
	}
	flags = (flags & (~O_NONBLOCK));
	flags = fcntl(fd, F_SETFL, flags);
	if (flags < 0) {
		ltfsmsg(LTFS_INFO, 30211I, "set", errno);
	}

	return fd;
}

static int _raw_open(struct scsipi_ibmtape_data *priv)
{
	int ret = -EDEV_UNKNOWN;
	int drive_type = DRIVE_UNSUPPORTED;

	scsi_device_identifier id_data;

	/* Open device */
	ret = _raw_dev_open(priv->devname);
	if (ret < 0) {
		priv->dev.fd = -1;
		return ret;
	}
	priv->dev.fd = ret;
	ret = -EDEV_UNKNOWN;

	/* Check the drive is supportable */
	ret = scsipi_get_drive_identifier(&priv->dev, &id_data);
	if (ret < 0) {
		ltfsmsg(LTFS_INFO, 30212I, priv->devname);
		close(priv->dev.fd);
		priv->dev.fd = -1;
		return ret;
	}

	struct supported_device **cur = ibm_supported_drives;
	while(*cur) {
		if((! strncmp(id_data.vendor_id, (*cur)->vendor_id, strlen((*cur)->vendor_id)) ) &&
		   (! strncmp(id_data.product_id, (*cur)->product_id, strlen((*cur)->product_id)) ) ) {
			drive_type = (*cur)->drive_type;
			break;
		}
		cur++;
	}

	if(drive_type > 0) {
		if (!ibm_tape_is_supported_firmware(drive_type, (unsigned char*)id_data.product_rev)) {
			close(priv->dev.fd);
			priv->dev.fd = -1;
			return -EDEV_UNSUPPORTED_FIRMWARE;
		} else
			priv->drive_type = drive_type;
	} else {
		ltfsmsg(LTFS_INFO, 30213I, id_data.product_id);
		close(priv->dev.fd);
		priv->dev.fd = -1;
		return -EDEV_DEVICE_UNSUPPORTABLE; /* Unsupported device */
	}

	if (priv->drive_serial[0]) {
		/* if serial number is already set, compare it */
		if (strcmp(priv->drive_serial, id_data.unit_serial)) {
			ltfsmsg(LTFS_INFO, 30248I, priv->drive_serial, id_data.unit_serial);
			close(priv->dev.fd);
			priv->dev.fd = -1;
			return -EDEV_DEVICE_UNOPENABLE; /* Unexpected device is opened */
		}
	} else
		strncpy(priv->drive_serial, id_data.unit_serial, sizeof(priv->drive_serial) - 1);

	ltfsmsg(LTFS_INFO, 30207I, id_data.vendor_id);
	ltfsmsg(LTFS_INFO, 30208I, id_data.product_id);
	ltfsmsg(LTFS_INFO, 30214I, id_data.product_rev);
	ltfsmsg(LTFS_INFO, 30215I, priv->drive_serial);

	return 0;
}

int _raw_tur(const int fd)
{
	int ret = -EDEV_UNKNOWN;
	struct scsipi_tape dev = {fd, false};

	scsireq_t req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "TEST_UNIT_READY";
	char *msg = NULL;

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0] = TEST_UNIT_READY;
	timeout = TU_DEFAULT_TIMEOUT;

	/* Build request */
	req.flags           = 0;
	req.cmdlen          = sizeof(cdb);
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&dev, &req, cmd_desc, &msg);
	if (ret < 0) {
		/* Print debug message */
		ltfsmsg(LTFS_DEBUG, 30245D, ret);
	}

	return ret;
}

#define _clear_por(p) _clear_por_raw((p)->dev.fd);

void _clear_por_raw(const int fd)
{
	int i = 0, ret = -1;

	while (ret && i < 3) {
		ret = _raw_tur(fd);
		switch (ret) {
			case -EDEV_NO_MEDIUM:
				/*
				 * The enterprise tape will return this error code
				 * when a tape is on the lock position.
				 * Just ignore this on both the LTO and the enterprise tape
				 */
				ret = 0;
				break;
			default:
				break;
		}
		i++;
	}
}

/* Forward reference */
int scsipi_ibmtape_get_device_list(struct tc_drive_info *buf, int count);
int scsipi_ibmtape_reserve(void *device);
static int _register_key(void *device, unsigned char *key);
static int _fetch_reservation_key(void *device, struct reservation_info *r);
static int _cdb_pro(void *device,
					enum pro_action action, enum pro_type type,
					unsigned char *key, unsigned char *sakey);

static int _reconnect_device(void *device)
{
	int ret = -EDEV_UNKNOWN, f_ret;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;
	int i, devs = 0, info_devs = 0;
	struct tc_drive_info *buf = NULL;
	struct reservation_info r_info;

	/* Close disconnected file descriptor */
	if (priv->dev.fd >= 0)
		close(priv->dev.fd);
	priv->dev.fd = -1;

	if (priv->devname)
		free(priv->devname);
	priv->devname = NULL;

	/* Search another device files which has same serial number */
	devs = scsipi_ibmtape_get_device_list(NULL, 0);
	if (devs) {
		buf = (struct tc_drive_info *)calloc(devs * 2, sizeof(struct tc_drive_info));
		if (! buf) {
			ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
			return -LTFS_NO_MEMORY;
		}
		info_devs = scsipi_ibmtape_get_device_list(buf, devs * 2);
	}

	for (i = 0; i < info_devs; i++) {
		if (! strncmp(buf[i].serial_number, priv->drive_serial, TAPE_SERIAL_LEN_MAX) ) {
			priv->devname = strdup(buf[i].name);
			break;
		}
	}

	if (buf) {
		free(buf);
		buf = NULL;
	}

	/* Open another device file found in the previous step */
	if (!priv->devname) {
		ltfsmsg(LTFS_INFO, 30247I, priv->drive_serial);
		return -LTFS_NO_DEVICE;
	}

	ltfsmsg(LTFS_INFO, 30249I, priv->drive_serial, priv->devname);
	ret = _raw_open(priv);
	if (ret < 0) {
		ltfsmsg(LTFS_INFO, 30210I, priv->drive_serial, ret);
		return ret;
	}

	/* Issue TUR and check reservation conflict happens or not */
	_clear_por(priv);
	ret = _raw_tur(priv->dev.fd);
	if (ret == -EDEV_RESERVATION_CONFLICT) {
		/* Select another path, recover reservation */
		ltfsmsg(LTFS_INFO, 30269I, priv->drive_serial);
		_register_key(priv, priv->key);
		ret = _cdb_pro(device, PRO_ACT_PREEMPT_ABORT, PRO_TYPE_EXCLUSIVE,
					   priv->key, priv->key);
		if (!ret) {
			ltfsmsg(LTFS_INFO, 30272I, priv->drive_serial);
			_clear_por(priv);
			ret = -EDEV_NEED_FAILOVER;
		}
	} else {
		/* Read reservation information and print */
		_clear_por(priv);
		memset(&r_info, 0x00, sizeof(r_info));
		f_ret = _fetch_reservation_key(device, &r_info);
		if (f_ret == -EDEV_NO_RESERVATION_HOLDER) {
			/* Real POR may happens */
			ltfsmsg(LTFS_INFO, 30270I, priv->drive_serial);
			_register_key(priv, priv->key);
			ret = scsipi_ibmtape_reserve(device);
			if (!ret) {
				ltfsmsg(LTFS_INFO, 30272I, priv->drive_serial);
				_clear_por(priv);
				ret = -EDEV_REAL_POWER_ON_RESET;
			}
		} else {
			/* Select same path */
			ltfsmsg(LTFS_INFO, 30271I, priv->drive_serial);
			_clear_por(priv);
			ret = -EDEV_NEED_FAILOVER;
		}
	}

	return ret;
}

static int _process_errors(struct scsipi_ibmtape_data *priv, int ret, char *msg, char *cmd, bool print, bool take_dump)
{
	int ret_fo = 0; /* Error code while reconnecting process */
	bool unforced_dump;

	if (print) {
		if (msg != NULL) {
			ltfsmsg(LTFS_INFO, 30263I, cmd, msg, ret, priv->devname);
		} else {
			ltfsmsg(LTFS_ERR, 30264E, cmd, ret, priv->devname);
		}
	}

	if (!priv->is_reconnecting && ret == -EDEV_CONNECTION_LOST) {
		/* Reconnecting */
		ltfsmsg(LTFS_INFO, 30246I, priv->drive_serial);
		priv->is_reconnecting = true;
		ret_fo = _reconnect_device(priv);
		priv->is_reconnecting = false;
	}

	if (priv && !ret_fo) {
		if (print && take_dump && !global_data.disable_auto_dump
			&& is_dump_required(priv, ret, &unforced_dump)) {
			(void)_take_dump(priv, unforced_dump);
		}
	}

	return ret_fo;
}

static int _cdb_read_buffer(void *device, int id, unsigned char *buf, size_t offset, size_t len, int type)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB10_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "READ_BUFFER";
	char *msg = NULL;

	ltfsmsg(LTFS_DEBUG, 30393D, "read buffer", id, priv->drive_serial);

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0] = READ_BUFFER;
	cdb[1] = type;
	cdb[2] = id;
	cdb[3] = (unsigned char)(offset >> 16) & 0xFF;
	cdb[4] = (unsigned char)(offset >> 8)  & 0xFF;
	cdb[5] = (unsigned char) offset        & 0xFF;
	cdb[6] = (unsigned char)(len >> 16)    & 0xFF;
	cdb[7] = (unsigned char)(len >> 8)     & 0xFF;
	cdb[8] = (unsigned char) len           & 0xFF;

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = SCCMD_READ;
	req.cmdlen          = sizeof(cdb);
	req.datalen         = len;
	req.databuf         = buf;
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0){
		ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
		if (ret_ep < 0)
			ret = ret_ep;
	}

	return ret;
}

static int _cdb_force_dump(struct scsipi_ibmtape_data *priv)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;

	scsireq_t req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "FORCE_DUMP";
	char *msg = NULL;

	unsigned char buf[SENDDIAG_BUF_LEN];

	ltfsmsg(LTFS_DEBUG, 30393D, "force dump", 0, priv->drive_serial);

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));
	memset(buf, 0, sizeof(buf));

	/* Build CDB */
	cdb[0] = SEND_DIAGNOSTIC;
	cdb[1] = 0x10; /* Set PF bit */
	cdb[3] = 0x00;
	cdb[4] = 0x08; /* Param length = 8 */

	buf[0] = 0x80; /* Page code */
	buf[2] = 0x00;
	buf[3] = 0x04; /* page length */
	buf[4] = 0x01;
	buf[5] = 0x60; /* Diag ID */

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = SCCMD_WRITE;
	req.cmdlen          = sizeof(cdb);
	req.datalen         = SENDDIAG_BUF_LEN;
	req.databuf         = buf;
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0){
		ret_ep = _process_errors(priv, ret, msg, cmd_desc, true, true);
		if (ret_ep < 0)
			ret = ret_ep;
	}

	return ret;
}

static int _cdb_pri(void *device, unsigned char *buf, int size)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB10_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "PRI";
	char *msg = NULL;

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));
	memset(buf, 0, size);

	/* Build CDB */
	cdb[0] = PERSISTENT_RESERVE_IN;
	cdb[1] = 0x03; /* Full Info */
	cdb[6] = (unsigned char)(size >> 16) & 0xFF;
	cdb[7] = (unsigned char)(size >> 8)  & 0xFF;
	cdb[8] = (unsigned char) size        & 0xFF;

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = SCCMD_READ;
	req.cmdlen          = sizeof(cdb);
	req.datalen         = size;
	req.databuf         = buf;
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0){
		ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
		if (ret_ep < 0)
			ret = ret_ep;
	}

	return ret;
}

static int _fetch_reservation_key(void *device, struct reservation_info *r)
{
	int ret = -EDEV_UNKNOWN;

	unsigned char *buf = NULL, *cur = NULL;
	unsigned int offset = 0, addlen;
	unsigned int bufsize = PRI_BUF_LEN;
	unsigned int pri_len = 0;
	bool holder = false;

start:
	buf = calloc(1, bufsize);
	if (!buf) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return -EDEV_NO_MEMORY;
	}

	ret = _cdb_pri(device, buf, bufsize);
	if (!ret) {
		pri_len = ltfs_betou32(buf + 4);
		if (pri_len + PRI_BUF_HEADER > bufsize) {
			free(buf);
			bufsize = pri_len + PRI_BUF_HEADER;
			goto start;
		}

		/* Parse PRI output and search reservation holder */
		offset = PRI_BUF_HEADER;
		while (offset < (pri_len + PRI_BUF_HEADER) - 1) {
			cur = buf + offset;

			if (cur[12] & 0x01) {
				holder = true;
				break;
			}

			addlen = ltfs_betou32(cur + 20);
			offset += (PRI_FULL_LEN_BASE + addlen);
		}

	}

	/* Print holder information here */
	if (!ret) {
		if (holder) {
			memcpy(r->key, cur, KEYLEN);
			ibm_tape_parsekey(cur, r);
		} else
			ret = -EDEV_NO_RESERVATION_HOLDER;
	}

	free(buf);

	return ret;
}

static int _cdb_pro(void *device,
					enum pro_action action, enum pro_type type,
					unsigned char *key, unsigned char *sakey)
{
	int ret = -EDEV_UNKNOWN, f_ret;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB10_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "PRO";
	unsigned char buf[PRO_BUF_LEN];
	char *msg = NULL;

	struct reservation_info r_info;

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));
	memset(buf, 0, sizeof(buf));

	/* Build CDB */
	cdb[0] = PERSISTENT_RESERVE_OUT;
	cdb[1] = action;
	cdb[2] = type;
	cdb[8] = PRO_BUF_LEN; /* parameter length */

	/* Build parameter list, clear key when key is NULL */
	if (key)
		memcpy(buf, key, KEYLEN);

	if (sakey)
		memcpy(buf + 8, sakey, KEYLEN);

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = SCCMD_WRITE;
	req.cmdlen          = sizeof(cdb);
	req.datalen         = PRO_BUF_LEN;
	req.databuf         = buf;
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0){
		if (ret == -EDEV_RESERVATION_CONFLICT && action == PRO_ACT_RESERVE) {
			/* Read reservation information and print */
			memset(&r_info, 0x00, sizeof(r_info));
			f_ret = _fetch_reservation_key(device, &r_info);
			if (!f_ret) {
				ltfsmsg(LTFS_WARN, 30266W, r_info.hint, priv->drive_serial);
				ltfsmsg(LTFS_WARN, 30267W,
						r_info.wwid[0], r_info.wwid[1], r_info.wwid[2], r_info.wwid[3],
						r_info.wwid[6], r_info.wwid[5], r_info.wwid[6], r_info.wwid[7],
						priv->drive_serial);
			} else {
				ltfsmsg(LTFS_WARN, 30266W, "unknown host (reserve command)", priv->drive_serial);
			}
		} else {
			ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
			if (ret_ep < 0)
				ret = ret_ep;
		}
	}

	return ret;
}

static int _register_key(void *device, unsigned char *key)
{
	int ret = -EDEV_UNKNOWN;

start:
	ret = _cdb_pro(device, PRO_ACT_REGISTER_IGNORE, PRO_TYPE_NONE,
			 NULL, key);

	if (ret == -EDEV_RESERVATION_PREEMPTED ||
		ret == -EDEV_RESERVATION_RELEASED ||
		ret == -EDEV_REGISTRATION_PREEMPTED )
		goto start;

	return ret;
}

/* Global functions */
int scsipi_ibmtape_open(const char *devname, void **handle)
{
	int ret = -1;

	struct scsipi_ibmtape_data *priv;

	CHECK_ARG_NULL(devname, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(handle, -LTFS_NULL_ARG);

	*handle = NULL;

	ltfsmsg(LTFS_INFO, 30209I, devname);

	priv = calloc(1, sizeof(struct scsipi_ibmtape_data));
	if(!priv) {
		ltfsmsg(LTFS_ERR, 10001E, "scsipi_ibmtape_open: device private data");
		return -EDEV_NO_MEMORY;
	}

	priv->devname = strdup(devname);
	if (!priv->devname) {
		ltfsmsg(LTFS_ERR, 10001E, "scsipi_ibmtape_open: devname");
		free(priv);
		return -EDEV_NO_MEMORY;
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_OPEN));

	ret = _raw_open(priv);
	if (ret < 0)
		goto free;

	/* Setup IBM tape specific parameters */
	standard_table = standard_tape_errors;
	vendor_table   = ibm_tape_errors;
	ibm_tape_init_timeout(&priv->timeouts, priv->drive_type);

	/* Issue TURs to clear POR sense */
	_clear_por(priv);

	/* Register reservation key */
	ibm_tape_genkey(priv->key);
	_register_key(priv, priv->key);

	/* Initial setting of force perm */
	priv->clear_by_pc     = false;
	priv->force_writeperm = DEFAULT_WRITEPERM;
	priv->force_readperm  = DEFAULT_READPERM;
	priv->force_errortype = DEFAULT_ERRORTYPE;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_OPEN));

	*handle = (void *)priv;

	return ret;

free:
	if (priv->devname)
		free(priv->devname);
	free(priv);
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_OPEN));
	return ret;
}

int scsipi_ibmtape_reopen(const char *devname, void *device)
{
	/* Do nothing */
	return 0;
}

int scsipi_ibmtape_close(void *device)
{
	int ret = -EDEV_UNKNOWN;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_CLOSE));

	_set_lbp(device, false);
	_register_key(device, NULL);

	close(priv->dev.fd);

	ibm_tape_destroy_timeout(&priv->timeouts);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_CLOSE));

	if (priv->profiler) {
		fclose(priv->profiler);
		priv->profiler = NULL;
	}

	if (priv->devname)
		free(priv->devname);

	free(device);

	return ret;
}

int scsipi_ibmtape_close_raw(void *device)
{
	int ret = 0;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_CLOSERAW));

	close(priv->dev.fd);
	priv->dev.fd = -1;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_CLOSERAW));
	return ret;
}

int scsipi_ibmtape_is_connected(const char *devname)
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

int scsipi_ibmtape_inquiry_page(void *device, unsigned char page, struct tc_inq_page *inq)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "INQUIRY";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_INQUIRYPAGE));
	ltfsmsg(LTFS_DEBUG, 30393D, "inquiry", page, priv->drive_serial);

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0] = INQUIRY;
	if(page)
		cdb[1] = 0x01;
	cdb[2] = page;
	ltfs_u16tobe(cdb + 3, sizeof(inq->data));

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = SCCMD_READ;
	req.cmdlen          = sizeof(cdb);
	req.datalen         = sizeof(inq->data);
	req.databuf         = inq->data;
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0){
		ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
		if (ret_ep < 0)
			ret = ret_ep;
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_INQUIRYPAGE));

	return ret;
}

int scsipi_ibmtape_inquiry(void *device, struct tc_inq *inq)
{
	int ret = -EDEV_UNKNOWN;
	int vendor_length = 0;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;
	struct tc_inq_page inq_page;

	ret = scsipi_ibmtape_inquiry_page(device, 0x00, &inq_page);
	if (ret < 0)
		return ret;

	memset(inq, 0, sizeof(struct tc_inq));
	strncpy((char*)inq->vid,      (char*)inq_page.data + 8,  VENDOR_ID_LENGTH);
	strncpy((char*)inq->pid,      (char*)inq_page.data + 16, PRODUCT_ID_LENGTH);
	strncpy((char*)inq->revision, (char*)inq_page.data + 32, PRODUCT_REV_LENGTH);

	inq->devicetype = priv->drive_type;

	if (IS_ENTERPRISE(priv->drive_type))
		vendor_length = 18;
	else
		vendor_length = 20;

	strncpy((char*)inq->vendor, (char*)inq_page.data + 36, vendor_length);
	inq->vendor[vendor_length] = '\0';

	return ret;
}

int scsipi_ibmtape_test_unit_ready(void *device)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "TEST_UNIT_READY";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_TUR));
	ltfsmsg(LTFS_DEBUG3, 30392D, "test unit ready", priv->drive_serial);

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0] = TEST_UNIT_READY;

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = 0;
	req.cmdlen          = sizeof(cdb);
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0){
		bool print_msg = false, take_dump = false;

		switch (ret) {
			case -EDEV_NEED_INITIALIZE:
			case -EDEV_CONFIGURE_CHANGED:
				print_msg = false;
				/* fall throuh */
			case -EDEV_NO_MEDIUM:
			case -EDEV_BECOMING_READY:
			case -EDEV_MEDIUM_MAY_BE_CHANGED:
			case -EDEV_NOT_READY:
			case -EDEV_NOT_REPORTABLE:
			case -EDEV_MEDIUM_REMOVAL_REQ:
			case -EDEV_CLEANING_IN_PROGRESS:
				take_dump = false;
				break;
			default:
				break;
		}

		ret_ep = _process_errors(device, ret, msg, cmd_desc, print_msg, take_dump);
		if (ret_ep < 0)
			ret = ret_ep;
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_TUR));

	return ret;
}

static int _cdb_read(void *device, char *buf, size_t size, bool sili)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "READ";
	char *msg = NULL;
	size_t length = -EDEV_UNKNOWN;

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0] = READ;
	if(sili && priv->use_sili)
		cdb[1] = 0x02;
	cdb[2] = (size >> 16) & 0xFF;
	cdb[3] = (size >> 8)  & 0xFF;
	cdb[4] =  size        & 0xFF;

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = SCCMD_READ;
	req.cmdlen          = sizeof(cdb);
	req.datalen         = size;
	req.databuf         = (unsigned char*)buf;
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0){
		int32_t diff_len = 0;
		unsigned char *sense = req.sense;

		switch (ret) {
			case DEVICE_GOOD:
			case -EDEV_NO_SENSE:
				if ((*(sense + 2)) & SK_ILI_SET) {
					long resid = req.datalen - req.datalen_used;
					diff_len = ltfs_betou32(sense + 3);
					if (!req.datalen || diff_len != resid) {
#if SUPPORT_BUGGY_IFS
						/*
						 * A few I/Fs, like thunderbolt/SAS converter or USB/SAS converter,
						 * cannot handle actual transfer length and residual length correctly
						 * In this case, LTFS will trust SCSI sense.
						 */
						if (diff_len < 0) {
							ltfsmsg(LTFS_INFO, 30820I, diff_len, size - diff_len); // "Detect overrun condition"
							ret = -EDEV_OVERRUN;
						} else {
							ltfsmsg(LTFS_DEBUG, 30821D, diff_len, size - diff_len); // "Detect underrun condition"
							length = size - diff_len;
							ret = DEVICE_GOOD;
						}
#else
						ltfsmsg(LTFS_WARN, 30216W, req.datalen  , resid, diff_len);
						return -EDEV_LENGTH_MISMATCH;
#endif
					} else {
						if (diff_len < 0) {
							/* Over-run condition */
							ltfsmsg(LTFS_INFO, 30217I, diff_len, (int)size - diff_len);
							ret = -EDEV_OVERRUN;
						} else {
							/* Under-run condition */
							ltfsmsg(LTFS_DEBUG, 30218D, diff_len, (int)size - diff_len);
							length = size - diff_len;
							ret = DEVICE_GOOD;
						}
					}
				} else if ((*(sense + 2)) & SK_FM_SET) {
					ltfsmsg(LTFS_DEBUG, 30219D);
					ret = -EDEV_FILEMARK_DETECTED;
					length = -EDEV_FILEMARK_DETECTED;
				}
				break;
			case -EDEV_FILEMARK_DETECTED:
				ltfsmsg(LTFS_DEBUG, 30219D);
				ret = -EDEV_FILEMARK_DETECTED;
				length = -EDEV_FILEMARK_DETECTED;
				break;
			case -EDEV_CLEANING_REQUIRED:
				ltfsmsg(LTFS_INFO, 30220I);
				length = 0;
				ret = DEVICE_GOOD;
				break;
		}

		if (ret != DEVICE_GOOD && ret != -EDEV_FILEMARK_DETECTED) {
			if ((ret != -EDEV_CRYPTO_ERROR && ret != -EDEV_KEY_REQUIRED) || priv->dev.is_data_key_set) {
				ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
			}

			if (ret_ep < 0)
				length = ret_ep;
			else
				length = ret;
		}
	} else {
		/* check condition is not set so we have a good read and can trust the length value */
		length = req.datalen  ;
	}

	return length;
}

static inline int _handle_block_allocation_failure(void *device, struct tc_position *pos,
												   int *retry, char *op)
{
	int ret = 0;
	struct tc_position tmp_pos = {0, 0};

	/* Sleep 3 secs to wait garbage correction in kernel side and retry */
	ltfsmsg(LTFS_WARN, 30277W, ++(*retry));
	sleep(3);

	ret = scsipi_ibmtape_readpos(device, &tmp_pos);
	if (ret == DEVICE_GOOD && pos->partition == tmp_pos.partition) {
		if (pos->block == tmp_pos.block) {
			/* Command is not reached to the drive */
			ltfsmsg(LTFS_INFO, 30278I, op,
					(unsigned int)tmp_pos.partition, (unsigned long long)tmp_pos.block);
			ret = -EDEV_RETRY;
		} else if (pos->block == tmp_pos.block - 1) {
			/* The drive received the command */
			ltfsmsg(LTFS_INFO, 30279I, op,
					(unsigned int)pos->partition, (unsigned long long)pos->block,
					(unsigned int)tmp_pos.partition, (unsigned long long)tmp_pos.block);
			ret = scsipi_ibmtape_space(device, 1, TC_SPACE_B, pos);
			if (!ret) {
				ret = scsipi_ibmtape_readpos(device, &tmp_pos);
				if (!ret && pos->block == tmp_pos.block) {
					/* Skip back was successfully done */
					ret = -EDEV_RETRY;
				} else if (!ret) {
					/* Skip back was successfully done, but not a expected position */
					ltfsmsg(LTFS_WARN, 30282W, op,
							(unsigned int)pos->partition, (unsigned long long)pos->block,
							(unsigned int)tmp_pos.partition, (unsigned long long)tmp_pos.block);
					ret = -LTFS_BAD_LOCATE;
				} else {
					ltfsmsg(LTFS_WARN, 30281W, op, ret,
							(unsigned int)pos->partition, (unsigned long long)pos->block,
							(unsigned int)tmp_pos.partition, (unsigned long long)tmp_pos.block);
				}
			} else {
				ltfsmsg(LTFS_WARN, 30283W, op, ret,
							(unsigned int)pos->partition, (unsigned long long)pos->block,
							(unsigned int)tmp_pos.partition, (unsigned long long)tmp_pos.block);
			}
		} else {
			/* Unexpected position */
			ltfsmsg(LTFS_WARN, 30280W, op, ret,
					(unsigned int)pos->partition, (unsigned long long)pos->block,
					(unsigned int)tmp_pos.partition, (unsigned long long)tmp_pos.block);
			ret = -EDEV_BUFFER_ALLOCATE_ERROR;
		}
	} else
		ltfsmsg(LTFS_WARN, 30281W, op, ret,
				(unsigned int)pos->partition, (unsigned long long)pos->block,
				(unsigned int)tmp_pos.partition, (unsigned long long)tmp_pos.block);

	return ret;
}

int scsipi_ibmtape_read(void *device, char *buf, size_t size,
					struct tc_position *pos, const bool unusual_size)
{
	int32_t ret = -EDEV_UNKNOWN;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;
	size_t datacount = size;
	struct tc_position pos_retry = {0, 0};
	int retry_count = 0;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_READ));
	ltfsmsg(LTFS_DEBUG3, 30395D, "read", size, priv->drive_serial);

	if (priv->force_readperm) {
		priv->read_counter++;
		if (priv->read_counter > priv->force_readperm) {
			ltfsmsg(LTFS_INFO, 30274I, "read");
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READ));
			if (priv->force_errortype)
				return -EDEV_NO_SENSE;
			else
				return -EDEV_READ_PERM;
		}
	}

	if(global_data.crc_checking) {
		datacount = size + 4;
		/* Never fall into this block, fail safe to adjust record length*/
		if (datacount > SG_MAX_BLOCK_SIZE)
			datacount = SG_MAX_BLOCK_SIZE;
	}

start_read:
	ret = _cdb_read(device, buf, datacount, unusual_size);
	if (ret == -EDEV_LENGTH_MISMATCH) {
		if (pos_retry.partition || pos_retry.block) {
			/* Return error when retry is already executed */
			scsipi_ibmtape_readpos(device, pos);
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READ));
			return ret;
		}
		pos_retry.partition = pos->partition;
		pos_retry.block     = pos->block;
		ret = scsipi_ibmtape_locate(device, pos_retry, pos);
		if (ret) {
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READ));
			return ret;
		}
		goto start_read;
	} else if ( !(pos->block) && unusual_size && (unsigned int)ret == size) {
		/*
		 *  Try to read again without sili bit, because some I/F doesn't support SILION read correctly
		 *  like USB connected LTO drive.
		 *  This recovery procedure is executed only when reading VOL1 on both partiton. Once this memod
		 *  is completed successfully, the backend uses SILI off read always.
		 */
		pos_retry.partition = pos->partition;
		ret = scsipi_ibmtape_locate(device, pos_retry, pos);
		if (ret) {
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READ));
			return ret;
		}
		priv->use_sili = false;
		ret = _cdb_read(device, buf, datacount, unusual_size);
	} else if (ret == -EDEV_BUFFER_ALLOCATE_ERROR && retry_count < MAX_RETRY) {
		ret = _handle_block_allocation_failure(device, pos, &retry_count, "read");
		if (ret == -EDEV_RETRY)
			goto start_read;
	}

	if(ret == -EDEV_FILEMARK_DETECTED)
	{
		pos->filemarks++;
		ret = DEVICE_GOOD;
	}

	if(ret >= 0) {
		pos->block++;
		if(global_data.crc_checking && ret > 4) {
			if (priv->f_crc_check)
				ret = priv->f_crc_check(buf, ret - 4);
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, 30221E);
				_take_dump(priv, false);
				ret = -EDEV_LBP_READ_ERROR;
			}
		}
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READ));
	return ret;
}

static int _cdb_write(void *device, uint8_t *buf, size_t size, bool *ew, bool *pew)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "WRITE";
	char *msg = NULL;

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0] = WRITE;
	cdb[1] = 0x00; /* Always variable in LTFS */
	cdb[2] = (size >> 16) & 0xFF;
	cdb[3] = (size >> 8)  & 0xFF;
	cdb[4] =  size        & 0xFF;

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = SCCMD_WRITE;
	req.cmdlen          = sizeof(cdb);
	req.datalen         = size;
	req.databuf         = (unsigned char*)buf;
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	*ew = false;
	*pew = false;

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0){
		switch (ret) {
			case -EDEV_EARLY_WARNING:
				ltfsmsg(LTFS_WARN, 30222W, "write");
				*ew = true;
				*pew = true;
				ret = DEVICE_GOOD;
				break;
			case -EDEV_PROG_EARLY_WARNING:
				ltfsmsg(LTFS_WARN, 30223W, "write");
				*pew = true;
				ret = DEVICE_GOOD;
				break;
			case -EDEV_CLEANING_REQUIRED:
				ltfsmsg(LTFS_INFO, 30220I);
				ret = DEVICE_GOOD;
				break;
			default:
				break;
		}

		if (ret < 0) {
			ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
			if (ret_ep < 0)
				ret = ret_ep;
		}
	}

	return ret;
}

int scsipi_ibmtape_write(void *device, const char *buf, size_t count, struct tc_position *pos)
{
	int ret, ret_fo;
	bool ew = false, pew = false;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;
	struct tc_position cur_pos;
	size_t datacount = count;
	int retry_count = 0;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_WRITE));

	ltfsmsg(LTFS_DEBUG3, 30395D, "write", count, priv->drive_serial);

	if ( priv->force_writeperm ) {
		priv->write_counter++;
		if ( priv->write_counter > priv->force_writeperm ) {
			ltfsmsg(LTFS_INFO, 30274I, "write");
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_WRITE));
			if (priv->force_errortype)
				return -EDEV_NO_SENSE;
			else
				return -EDEV_WRITE_PERM;
		} else if ( priv->write_counter > (priv->force_writeperm - THRESHOLD_FORCE_WRITE_NO_WRITE) ) {
			ltfsmsg(LTFS_INFO, 30275I);
			pos->block++;
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_WRITE));
			return DEVICE_GOOD;
		}
	}

	if(global_data.crc_checking) {
		if (priv->f_crc_enc)
			priv->f_crc_enc((void *)buf, count);
		datacount = count + 4;
	}

start_write:
	ret = _cdb_write(device, (uint8_t *)buf, datacount, &ew, &pew);
	if (ret == DEVICE_GOOD) {
		pos->block++;
		pos->early_warning = ew;
		pos->programmable_early_warning = pew;
	} else if (ret == -EDEV_NEED_FAILOVER) {
		ret_fo = scsipi_ibmtape_readpos(device, &cur_pos);
		if (!ret_fo) {
			if (pos->partition == cur_pos.partition
				&& pos->block + 1 == cur_pos.block) {
				pos->block++;
				pos->early_warning = cur_pos.early_warning;
				pos->programmable_early_warning = cur_pos.programmable_early_warning;
				ret = DEVICE_GOOD;
			} else
				ret = -EDEV_POR_OR_BUS_RESET;
		}
	} else if (ret == -EDEV_BUFFER_ALLOCATE_ERROR && retry_count < MAX_RETRY) {
		ret = _handle_block_allocation_failure(device, pos, &retry_count, "write");
		if (ret == -EDEV_RETRY)
			goto start_write;
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_WRITE));

	return ret;
}

int scsipi_ibmtape_writefm(void *device, size_t count, struct tc_position *pos, bool immed)
{
	int ret = -EDEV_UNKNOWN, ret_fo;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "WRITEFM";
	char *msg = NULL;

	struct tc_position cur_pos;
	bool ew = false, pew = false;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_WRITEFM));
	ltfsmsg(LTFS_DEBUG, 30394D, "write file marks", count, priv->drive_serial);

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0] = WRITE_FILEMARKS6;
	if (immed)
		cdb[1] = 0x01;
	cdb[2] = (count >> 16) & 0xFF;
	cdb[3] = (count >> 8)  & 0xFF;
	cdb[4] =  count        & 0xFF;

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = 0;
	req.cmdlen          = sizeof(cdb);
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0){
		switch (ret) {
			case -EDEV_EARLY_WARNING:
				ltfsmsg(LTFS_WARN, 30222W, "write filemarks");
				ew = true;
				pew = true;
				ret = DEVICE_GOOD;
				break;
			case -EDEV_PROG_EARLY_WARNING:
				ltfsmsg(LTFS_WARN, 30223W, "write filemarks");
				pew = true;
				ret = DEVICE_GOOD;
				break;
			case -EDEV_CLEANING_REQUIRED:
				ltfsmsg(LTFS_INFO, 30220I);
				ret = DEVICE_GOOD;
				break;
			default:
				break;
		}

		if (ret < 0) {
			ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
			if (ret_ep < 0)
				ret = ret_ep;
		}
	}

	if(ret == DEVICE_GOOD) {
		ret = scsipi_ibmtape_readpos(device, pos);
		if(ret == DEVICE_GOOD) {
			if (ew && !pos->early_warning)
				pos->early_warning = ew;
			if (pew && !pos->programmable_early_warning)
				pos->programmable_early_warning = pew;
		}
	} else if (ret == -EDEV_NEED_FAILOVER) {
		ret_fo = scsipi_ibmtape_readpos(device, &cur_pos);
		if (!ret_fo) {
			if (pos->partition == cur_pos.partition
				&& pos->block + count == cur_pos.block) {
				pos->block++;
				pos->early_warning = cur_pos.early_warning;
				pos->programmable_early_warning = cur_pos.programmable_early_warning;
				ret = DEVICE_GOOD;
			} else
				ret = -EDEV_POR_OR_BUS_RESET;
		}
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_WRITEFM));

	return ret;
}

int scsipi_ibmtape_rewind(void *device, struct tc_position *pos)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "REWIND";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_REWIND));
	ltfsmsg(LTFS_DEBUG, 30397D, "rewind", (unsigned long long)0, (unsigned long long)0, priv->drive_serial);

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0] = REWIND;

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = 0;
	req.cmdlen          = sizeof(cdb);
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0){
		ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
		if (ret_ep < 0)
			ret = ret_ep;
	}

	if(ret == DEVICE_GOOD) {
		/* Clear force perm setting */
		priv->clear_by_pc     = false;
		priv->force_writeperm = DEFAULT_WRITEPERM;
		priv->force_readperm  = DEFAULT_READPERM;
		priv->write_counter   = 0;
		priv->read_counter    = 0;

		ret = scsipi_ibmtape_readpos(device, pos);

		if(ret == DEVICE_GOOD) {
			if(pos->early_warning)
				ltfsmsg(LTFS_WARN, 30222W, "rewind");
			else if(pos->programmable_early_warning)
				ltfsmsg(LTFS_WARN, 30223W, "rewind");
		}
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_REWIND));

	return ret;
}

int scsipi_ibmtape_locate(void *device, struct tc_position dest, struct tc_position *pos)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	int ret_rp = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB16_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "LOCATE";
	char *msg = NULL;
	bool pc = false;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_LOCATE));
	ltfsmsg(LTFS_DEBUG, 30397D, "locate",
			(unsigned long long)dest.partition,
			(unsigned long long)dest.block,
			priv->drive_serial);

	if (pos->partition != dest.partition) {
		if (priv->clear_by_pc) {
			/* Clear force perm setting */
			priv->clear_by_pc     = false;
			priv->force_writeperm = DEFAULT_WRITEPERM;
			priv->force_readperm  = DEFAULT_READPERM;
			priv->write_counter   = 0;
			priv->read_counter    = 0;
		}
		pc = true;
	}

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0]  = LOCATE16;
	if (pc)
		cdb[1]  = 0x02; /* Set Change partition(CP) flag */
	cdb[3]  = (unsigned char)(dest.partition & 0xff);
	ltfs_u64tobe(cdb + 4, dest.block);

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = 0;
	req.cmdlen          = sizeof(cdb);
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0){
		if (dest.block == TAPE_BLOCK_MAX && ret == -EDEV_EOD_DETECTED) {
			ltfsmsg(LTFS_DEBUG, 30224D, "Locate");
			ret = DEVICE_GOOD;
		} else {
			ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
			if (ret_ep < 0)
				ret = ret_ep;
		}
	}

	ret_rp = scsipi_ibmtape_readpos(device, pos);
	if (ret_rp == DEVICE_GOOD) {
		if(pos->early_warning)
			ltfsmsg(LTFS_WARN, 30222W, "locate");
		else if(pos->programmable_early_warning)
			ltfsmsg(LTFS_WARN, 30223W, "locate");
	} else {
		if (!ret)
			ret = ret_rp;
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_LOCATE));

	return ret;
}

int scsipi_ibmtape_space(void *device, size_t count, TC_SPACE_TYPE type, struct tc_position *pos)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB16_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "SPACE";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_SPACE));

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0] = SPACE16;
	switch(type) {
		case TC_SPACE_EOD:
			ltfsmsg(LTFS_DEBUG, 30392D, "space to EOD", priv->drive_serial);
			cdb[1] = 0x03;
			break;
		case TC_SPACE_FM_F:
			ltfsmsg(LTFS_DEBUG, 30396D, "space forward file marks", (unsigned long long)count,
					priv->drive_serial);
			cdb[1] = 0x01;
			ltfs_u64tobe(cdb + 4, count);
			break;
		case TC_SPACE_FM_B:
			ltfsmsg(LTFS_DEBUG, 30396D, "space back file marks", (unsigned long long)count,
					priv->drive_serial);
			cdb[1] = 0x01;
			ltfs_u64tobe(cdb + 4, -count);
			break;
		case TC_SPACE_F:
			ltfsmsg(LTFS_DEBUG, 30396D, "space forward records", (unsigned long long)count,
					priv->drive_serial);
			cdb[1] = 0x00;
			ltfs_u64tobe(cdb + 4, count);
			break;
		case TC_SPACE_B:
			cdb[1] = 0x00;
			ltfs_u64tobe(cdb + 4, -count);
			break;
		default:
			/* unexpected space type */
			ltfsmsg(LTFS_INFO, 30225I);
			ret = -EDEV_INVALID_ARG;
			break;
	}

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = 0;
	req.cmdlen          = sizeof(cdb);
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0){
		/* TODO: Need to confirm additional operation is required or not */
		ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
		if (ret_ep < 0)
			ret = ret_ep;
	}

	if(ret == DEVICE_GOOD)
		ret = scsipi_ibmtape_readpos(device, pos);

	if(ret == DEVICE_GOOD) {
		if(pos->early_warning)
			ltfsmsg(LTFS_WARN, 30222W, "space");
		else if(pos->programmable_early_warning)
			ltfsmsg(LTFS_WARN, 30223W, "space");
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SPACE));

	return ret;
}

static int _cdb_request_sense(void *device, unsigned char *buf, unsigned char size)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "REQUEST_SENSE";
	char *msg = NULL;

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0] = REQUEST_SENSE;
	cdb[4] = (unsigned char)size;

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = SCCMD_READ;
	req.cmdlen          = sizeof(cdb);
	req.datalen         = size;
	req.databuf         = buf;
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0) {
		ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
		if (ret_ep < 0)
			ret = ret_ep;
	}

	return ret;
}

int scsipi_ibmtape_erase(void *device, struct tc_position *pos, bool long_erase)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "ERASE";
	char *msg = NULL;
	struct ltfs_timespec ts_start, ts_now;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_ERASE));
	if (long_erase)
		ltfsmsg(LTFS_DEBUG, 30392D, "long erase", priv->drive_serial);
	else
		ltfsmsg(LTFS_DEBUG, 30392D, "short erase", priv->drive_serial);

	get_current_timespec(&ts_start);

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0] = ERASE;
	if (long_erase)
		cdb[1] = 0x03;

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = 0;
	req.cmdlen          = sizeof(cdb);
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);

	if (long_erase) {
		unsigned char sense_buf[MAXSENSE];
		uint32_t      sense_data;
		uint32_t      progress;

		while (ret == DEVICE_GOOD) {
			memset(sense_buf, 0, sizeof(sense_buf));
			ret= _cdb_request_sense(device, sense_buf, sizeof(sense_buf));

			sense_data = ((uint32_t) sense_buf[2] & 0x0F) << 16;
			sense_data += ((uint32_t) sense_buf[12] & 0xFF) << 8;
			sense_data += ((uint32_t) sense_buf[13] & 0xFF);

			if (sense_data != 0x000016 && sense_data != 0x000018) {
				/* Erase operation is NOT in progress */
				break;
			}

			if (IS_ENTERPRISE(priv->drive_type)) {
				get_current_timespec(&ts_now);
				ltfsmsg(LTFS_INFO, 30226I, (int)(ts_now.tv_sec - ts_start.tv_sec)/60);
			} else {
				progress = ((uint32_t) sense_buf[16] & 0xFF) << 8;
				progress += ((uint32_t) sense_buf[17] & 0xFF);
				ltfsmsg(LTFS_INFO, 30227I, (progress*100/0xFFFF));
			}

			sleep(60);
		}
	}

	if (ret < 0){
		ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
		if (ret_ep < 0)
			ret = ret_ep;
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_ERASE));

	return ret;
}

static int _cdb_load_unload(void *device, bool load)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "LOAD_UNLOAD";
	char *msg = NULL;

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0] = LOAD_UNLOAD;
	if (load)
		cdb[4] = 0x01;

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = 0;
	req.cmdlen          = sizeof(cdb);
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0){
		if (ret == -EDEV_MEDIUM_MAY_BE_CHANGED) {
			ret = DEVICE_GOOD;
		} else {
			ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
			if (ret_ep < 0)
				ret = ret_ep;
		}
	}

	return ret;
}


#ifndef TC_MP_MEDIUM_CONFIGURATION
#define TC_MP_MEDIUM_CONFIGURATION  (0x1D) // ModePage 0x1D (Mediat type)
#define TC_MP_MEDIUM_CONFIGURATION_SIZE    (64)
#endif
static int scsipi_ibmtape_medium_configuration(void *device)
{
	int ret;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;
	unsigned char buf[TC_MP_MEDIUM_CONFIGURATION_SIZE];

	ret = scsipi_ibmtape_modesense(device, TC_MP_MEDIUM_CONFIGURATION, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
	if (ret < 0) {
		return ret;
	}

	priv->density_code = buf[8];
	priv->is_worm = buf[18] & 0x01;
	switch(priv->density_code) {
	case TC_DC_LTO5:
		priv->cart_type = TC_MP_LTO5D_CART;
		break;
	case TC_DC_LTO6:
		priv->cart_type = TC_MP_LTO6D_CART;
		break;
	case TC_DC_LTO7:
		priv->cart_type = TC_MP_LTO7D_CART;
		break;
	case TC_DC_LTOM8: 
	case TC_DC_LTO8:
		priv->cart_type = TC_MP_LTO8D_CART;
		break;
	default:
		break;
	}
	
	return 0;
}


int scsipi_ibmtape_load(void *device, struct tc_position *pos)
{
	int ret = -EDEV_UNKNOWN;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;
	unsigned char buf[TC_MP_SUPPORTEDPAGE_SIZE];

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_LOAD));
	ltfsmsg(LTFS_DEBUG, 30392D, "load", priv->drive_serial);

	ret = _cdb_load_unload(device, true);

	/* Clear force perm setting */
	priv->clear_by_pc     = false;
	priv->force_writeperm = DEFAULT_WRITEPERM;
	priv->force_readperm  = DEFAULT_READPERM;
	priv->write_counter   = 0;
	priv->read_counter    = 0;

	scsipi_ibmtape_readpos(device, pos);
	if (ret < 0) {
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_LOAD));
		return ret;
	} else {
		if(ret == DEVICE_GOOD) {
			if(pos->early_warning)
				ltfsmsg(LTFS_WARN, 30222W, "load");
			else if(pos->programmable_early_warning)
				ltfsmsg(LTFS_WARN, 30223W, "load");
		}

		priv->loaded = true;
	}

	priv->tape_alert = 0;

	/* Check Cartridge type */
	ret = scsipi_ibmtape_modesense(device, TC_MP_SUPPORTEDPAGE, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
	if (ret < 0) {
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_LOAD));
		return ret;
	}

	priv->cart_type = buf[2];
	priv->density_code = buf[8];

	if (priv->cart_type == 0x00)
		(void)scsipi_ibmtape_medium_configuration(device);

	if (priv->cart_type == 0x00) {
		ltfsmsg(LTFS_WARN, 30265W);
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_LOAD));
		return 0;
	}

	ret = ibm_tape_is_supported_tape(priv->cart_type, priv->density_code, &(priv->is_worm));
	if(ret == -LTFS_UNSUPPORTED_MEDIUM)
		ltfsmsg(LTFS_INFO, 30228I, priv->cart_type, priv->density_code);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_LOAD));

	return ret;
}

int scsipi_ibmtape_unload(void *device, struct tc_position *pos)
{
	int ret = -EDEV_UNKNOWN;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_UNLOAD));
	ltfsmsg(LTFS_DEBUG, 30392D, "unload", priv->drive_serial);

	ret = _cdb_load_unload(device, false);

	/* Clear force perm setting */
	priv->clear_by_pc     = false;
	priv->force_writeperm = DEFAULT_WRITEPERM;
	priv->force_readperm  = DEFAULT_READPERM;
	priv->write_counter   = 0;
	priv->read_counter    = 0;

	if (ret < 0) {
		scsipi_ibmtape_readpos(device, pos);
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_UNLOAD));
		return ret;
	}

	priv->loaded       = false;
	priv->cart_type    = 0;
	priv->density_code = 0;
	priv->tape_alert   = 0;
	pos->partition     = 0;
	pos->block         = 0;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_UNLOAD));

	return ret;
}

int scsipi_ibmtape_readpos(void *device, struct tc_position *pos)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "READPOS";
	char *msg = NULL;
	unsigned char buf[REDPOS_LONG_LEN];

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_READPOS));

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0] = READ_POSITION;
	cdb[1] = 0x08; /* Long Format */

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = SCCMD_READ;
	req.cmdlen          = sizeof(cdb);
	req.datalen         = sizeof(buf);
	req.databuf         = buf;
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret == DEVICE_GOOD) {
		pos->partition = ltfs_betou32(buf + 4);
		pos->block     = ltfs_betou64(buf + 8);
		pos->filemarks = ltfs_betou64(buf + 16);
		pos->early_warning = buf[0] & 0x40;
		pos->programmable_early_warning = buf[0] & 0x01;

		ltfsmsg(LTFS_DEBUG, 30398D, "readpos", (unsigned long long)pos->partition,
				(unsigned long long)pos->block, (unsigned long long)pos->filemarks,
				priv->drive_serial);
	} else {
		ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
		if (ret_ep < 0)
			ret = ret_ep;
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READPOS));

	return ret;
}

int scsipi_ibmtape_setcap(void *device, uint16_t proportion)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "SETCAP";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_SETCAP));
	ltfsmsg(LTFS_DEBUG, 30393D, "setcap", proportion, priv->drive_serial);

	if (IS_ENTERPRISE(priv->drive_type)) {
		unsigned char buf[TC_MP_MEDIUM_SENSE_SIZE];

		/* scale media instead of setcap */
		ret = scsipi_ibmtape_modesense(device, TC_MP_MEDIUM_SENSE, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
		if (ret < 0) {
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETCAP));
			return ret;
		}

		if (IS_SHORT_MEDIUM(buf[2]) || IS_WORM_MEDIUM(buf[2])) {
			/* Short or WORM cartridge cannot be scaled */
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETCAP));
			return ret;
		}

		buf[0]   = 0x00;
		buf[1]   = 0x00;
		buf[27] |= 0x01;
		buf[28]  = 0x00;

		ret = scsipi_ibmtape_modeselect(device, buf, sizeof(buf));
	} else {
		/* Zero out the CDB and the result buffer */
		ret = init_scsireq(&req);
		if (ret < 0)
			return ret;

		memset(cdb, 0, sizeof(cdb));

		/* Build CDB */
		cdb[0] = SET_CAPACITY;
		ltfs_u16tobe(cdb + 3, proportion);

		timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
		if (timeout < 0)
			return -EDEV_UNSUPPORETD_COMMAND;

		/* Build request */
		req.flags           = 0;
		req.cmdlen          = sizeof(cdb);
		memcpy(req.cmd, cdb, sizeof(cdb));
		req.timeout         = SGConversion(timeout);

		ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
		if (ret < 0) {
			ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
			if (ret_ep < 0)
				ret = ret_ep;
		}
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETCAP));

	return ret;
}

int scsipi_ibmtape_format(void *device, TC_FORMAT_TYPE format)
{
	int ret = -EDEV_UNKNOWN, aux_ret;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;
	unsigned char buf[TC_MP_SUPPORTEDPAGE_SIZE];

	scsireq_t req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "FORMAT";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_FORMAT));
	ltfsmsg(LTFS_DEBUG, 30392D, "format", priv->drive_serial);

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0] = FORMAT_MEDIUM;
	cdb[2] = format;

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = 0;
	req.cmdlen          = sizeof(cdb);
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0) {
		ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
		if (ret_ep < 0)
			ret = ret_ep;
	}

	/* Check Cartridge type */
	aux_ret = scsipi_ibmtape_modesense(device, TC_MP_SUPPORTEDPAGE, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
	if (!aux_ret) {
		priv->cart_type = buf[2];
		priv->density_code = buf[8];
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_FORMAT));

	return ret;
}

int scsipi_ibmtape_remaining_capacity(void *device, struct tc_remaining_cap *cap)
{
	int ret = -EDEV_UNKNOWN;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	unsigned char buffer[LOGSENSEPAGE];       /* Buffer for logsense */
	unsigned char buf[LOG_TAPECAPACITY_SIZE]; /* Buffer for parsing logsense data */
	uint32_t param_size;
	int32_t i;
	uint32_t logcap;
	int offset, length;
	unsigned cap_offset = global_data.capacity_offset;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_REMAINCAP));

	memset(buffer, 0, LOGSENSEPAGE);

	if (IS_LTO(priv->drive_type) && (DRIVE_GEN(priv->drive_type) == 0x05)) {
		/* Use LogPage 0x31 */
		ret = scsipi_ibmtape_logsense(device, (uint8_t)LOG_TAPECAPACITY, (void *)buffer, LOGSENSEPAGE);
		if(ret < 0)
		{
			ltfsmsg(LTFS_INFO, 30229I, LOG_VOLUMESTATS, ret);
			goto out;
		}

		for( i = TAPECAP_REMAIN_0; i < TAPECAP_SIZE; i++)
		{
			ret = _parse_logPage(buffer, (uint16_t)i, &param_size, buf, sizeof(buf));
			if(ret < 0 || param_size != sizeof(uint32_t))
			{
				ltfsmsg(LTFS_INFO, 30230I, i, param_size);
				ret = -EDEV_INTERNAL_ERROR;
				goto out;
			}

			logcap = ltfs_betou32(buf);

			switch(i)
			{
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
					ltfsmsg(LTFS_INFO, 30231I, i);
					ret = -EDEV_INTERNAL_ERROR;
					goto out;
					break;
			}
		}

		if (global_data.capacity_offset) {
			if (cap->remaining_p1 < global_data.capacity_offset)
				cap_offset = cap->remaining_p1;

			ltfsmsg(LTFS_INFO, 30276I, 1,
					(unsigned long long)cap->remaining_p1,
					(unsigned long long)global_data.capacity_offset,
					priv->drive_serial);
			cap->remaining_p1 -= cap_offset;
		}

		ret = DEVICE_GOOD;
	} else {
		/* Use LogPage 0x17 */
		ret = scsipi_ibmtape_logsense(device, LOG_VOLUMESTATS, (void *)buffer, LOGSENSEPAGE);
		if(ret < 0)
		{
			ltfsmsg(LTFS_INFO, 30229I, LOG_VOLUMESTATS, ret);
			goto out;
		}

		/* Capture Total Cap */
		ret = _parse_logPage(buffer, (uint16_t)VOLSTATS_PARTITION_CAP, &param_size, buf, sizeof(buf));
		if (ret < 0) {
			ltfsmsg(LTFS_INFO, 30232I);
			goto out;
		}

		memset(cap, 0, sizeof(struct tc_remaining_cap));

		cap->max_p0 = ltfs_betou32(&buf[PARTITIOIN_REC_HEADER_LEN]);
		offset = (int)buf[0] + 1;
		length = (int)buf[offset] + 1;

		if (offset + length <= (int)param_size) {
			cap->max_p1 = ltfs_betou32(&buf[offset + PARTITIOIN_REC_HEADER_LEN]);
		}

		/* Capture Remaining Cap  */
		ret = _parse_logPage(buffer, (uint16_t)VOLSTATS_PART_REMAIN_CAP, &param_size, buf, sizeof(buf));
		if (ret < 0) {
			ltfsmsg(LTFS_INFO, 30232I);
			goto out;
		}

		cap->remaining_p0 = ltfs_betou32(&buf[PARTITIOIN_REC_HEADER_LEN]);
		offset = (int)buf[0] + 1;
		length = (int)buf[offset] + 1;

		if (offset + length <= (int)param_size) {
			cap->remaining_p1 = ltfs_betou32(&buf[offset + PARTITIOIN_REC_HEADER_LEN]);
		}

		if (global_data.capacity_offset) {
			if (cap->remaining_p1 < global_data.capacity_offset)
				cap_offset = cap->remaining_p1;

			ltfsmsg(LTFS_INFO, 30276I, 1,
					(unsigned long long)cap->remaining_p1,
					(unsigned long long)global_data.capacity_offset,
					priv->drive_serial);
			cap->remaining_p1 -= cap_offset;
		}

		/* Convert MB to MiB -- Need to consider about overflow when max cap reaches to 18EB */
		cap->max_p0 = (cap->max_p0 * 1000 * 1000) >> 20;
		cap->max_p1 = (cap->max_p1 * 1000 * 1000) >> 20;
		cap->remaining_p0 = (cap->remaining_p0 * 1000 * 1000) >> 20;
		cap->remaining_p1 = (cap->remaining_p1 * 1000 * 1000) >> 20;

		ret = DEVICE_GOOD;
	}

	ltfsmsg(LTFS_DEBUG3, 30397D, "capacity part0", (unsigned long long)cap->remaining_p0,
			(unsigned long long)cap->max_p0, priv->drive_serial);
	ltfsmsg(LTFS_DEBUG3, 30397D, "capacity part1", (unsigned long long)cap->remaining_p1,
			(unsigned long long)cap->max_p1, priv->drive_serial);

out:
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_REMAINCAP));
	return ret;
}

static int _cdb_logsense(void *device, const unsigned char page, const unsigned char subpage,
						 unsigned char *buf, const size_t size)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB10_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "LOGSENSE";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_LOGSENSE));

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0] = LOG_SENSE;
	cdb[2] = 0x40 | (page & 0x3F); /* Current value */
	cdb[3] = subpage;
	ltfs_u16tobe(cdb + 7, size);

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = SCCMD_READ;
	req.cmdlen          = sizeof(cdb);
	req.datalen         = size;
	req.databuf         = buf;
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0){
		ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
		if (ret_ep < 0)
			ret = ret_ep;
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_LOGSENSE));

	return ret;
}

int scsipi_ibmtape_logsense(void *device, const unsigned char page, unsigned char *buf, const size_t size)
{
	int ret = -EDEV_UNKNOWN;

	ltfsmsg(LTFS_DEBUG3, 30393D, "logsense", page, "");
	ret = _cdb_logsense(device, page, 0x00, buf, size);

	return ret;
}

int scsipi_ibmtape_modesense(void *device, const unsigned char page, const TC_MP_PC_TYPE pc,
						 const unsigned char subpage, unsigned char *buf, const size_t size)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB10_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "MODESENSE";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_MODESENSE));
	ltfsmsg(LTFS_DEBUG3, 30393D, "modesense", page, priv->drive_serial);

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0] = MODE_SENSE10;
	cdb[2] = pc | (page & 0x3F); /* Current value */
	cdb[3] = subpage;
	ltfs_u16tobe(cdb + 7, size);

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = SCCMD_READ;
	req.cmdlen          = sizeof(cdb);
	req.datalen         = size;
	req.databuf         = buf;
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0){
		ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
		if (ret_ep < 0)
			ret = ret_ep;
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_MODESENSE));

	return ret;
}

int scsipi_ibmtape_modeselect(void *device, unsigned char *buf, const size_t size)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB10_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "MODESELECT";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_MODESELECT));
	ltfsmsg(LTFS_DEBUG3, 30392D, "modeselect", priv->drive_serial);

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0] = MODE_SELECT10;
	ltfs_u16tobe(cdb + 7, size);

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = SCCMD_WRITE;
	req.cmdlen          = sizeof(cdb);
	req.datalen         = size;
	req.databuf         = buf;
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0){
		ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
		if (ret_ep < 0)
			ret = ret_ep;
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_MODESELECT));

	return ret;
}

int scsipi_ibmtape_reserve(void *device)
{
	int ret = -EDEV_UNKNOWN, count = 0;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_RESERVEUNIT));
	ltfsmsg(LTFS_DEBUG, 30392D, "reserve (PRO)", priv->drive_serial);

start:
	ret = _cdb_pro(device, PRO_ACT_RESERVE, PRO_TYPE_EXCLUSIVE,
				   priv->key, NULL);

	/* Retry if reservation is preempted */
	if ( !count &&
		 ( ret == -EDEV_RESERVATION_PREEMPTED ||
		   ret == -EDEV_REGISTRATION_PREEMPTED ||
		   ret == -EDEV_RESERVATION_CONFLICT)
		) {
		ltfsmsg(LTFS_INFO, 30268I, priv->drive_serial);
		_register_key(device, priv->key);
		count++;
		goto start;
	}

	if (!ret)
		priv->is_reserved = true;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_RESERVEUNIT));

	return ret;
}

int scsipi_ibmtape_release(void *device)
{
	int ret = -EDEV_UNKNOWN;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_RELEASEUNIT));

	ltfsmsg(LTFS_DEBUG, 30392D, "release (PRO)", priv->drive_serial);

	/* Issue release command even if no reservation is made */
	ret = _cdb_pro(device, PRO_ACT_RELEASE, PRO_TYPE_EXCLUSIVE,
				   priv->key, NULL);

	if (!ret)
		priv->is_reserved = false;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_RELEASEUNIT));

	return ret;
}

static int _cdb_prevent_allow_medium_removal(void *device, bool prevent)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "PREVENT/ALLOW_MEDIUM_REMOVAL";
	char *msg = NULL;

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0] = PREVENT_ALLOW_MEDIUM_REMOVAL;
	if (prevent)
		cdb[4] = 0x01;

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = 0;
	req.cmdlen          = sizeof(cdb);
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0){
		ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
		if (ret_ep < 0)
			ret = ret_ep;
	}

	if (!ret) {
		if (prevent)
			priv->is_tape_locked = true;
		else
			priv->is_tape_locked = false;
	}

	return ret;
}

int scsipi_ibmtape_prevent_medium_removal(void *device)
{
	int ret = -EDEV_UNKNOWN;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_PREVENTM));
	ltfsmsg(LTFS_DEBUG, 30392D, "prevent medium removal", priv->drive_serial);
	ret = _cdb_prevent_allow_medium_removal(device, true);
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_PREVENTM));

	return ret;
}

int scsipi_ibmtape_allow_medium_removal(void *device)
{
	int ret = -EDEV_UNKNOWN;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_ALLOWMREM));
	ltfsmsg(LTFS_DEBUG, 30392D, "allow medium removal", priv->drive_serial);
	ret = _cdb_prevent_allow_medium_removal(device, false);
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_ALLOWMREM));

	return ret;
}

int scsipi_ibmtape_write_attribute(void *device, const tape_partition_t part,
							   const unsigned char *buf, const size_t size)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB16_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "WRITE_ATTR";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_WRITEATTR));
	ltfsmsg(LTFS_DEBUG3, 30396D, "writeattr", (unsigned long long)part,
			priv->drive_serial);

	/* Prepare the buffer to transfer */
	uint32_t len = size + 4;
	unsigned char *buffer = calloc(1, len);
	if (!buffer) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return -EDEV_NO_MEMORY;
	}

	ltfs_u32tobe(buffer, len);
	memcpy(buffer + 4, buf, size);

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0] = WRITE_ATTRIBUTE;
	cdb[1] = 0x01; /* Write through bit on */
	cdb[7] = part;
	ltfs_u32tobe(cdb + 10, len);

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0) {
		free(buffer);
		return -EDEV_UNSUPPORETD_COMMAND;
	}

	/* Build request */
	req.flags           = SCCMD_WRITE;
	req.cmdlen          = sizeof(cdb);
	req.datalen         = len;
	req.databuf         = buffer;
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0){
		ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
		if (ret_ep < 0)
			ret = ret_ep;
	}

	free(buffer);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_WRITEATTR));

	return ret;
}

int scsipi_ibmtape_read_attribute(void *device, const tape_partition_t part,
							  const uint16_t id, unsigned char *buf, const size_t size)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB16_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "READ_ATTR";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_READATTR));
	ltfsmsg(LTFS_DEBUG3, 30397D, "readattr", (unsigned long long)part, (unsigned long long)id, priv->drive_serial);

	/* Prepare the buffer to transfer */
	uint32_t len = size + 4;
	unsigned char *buffer = calloc(1, len);
	if (!buffer) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return -EDEV_NO_MEMORY;
	}

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0] = READ_ATTRIBUTE;
	cdb[1] = 0x00; /* Service Action: 0x00 (Value) */
	cdb[7] = part;
	ltfs_u16tobe(cdb + 8, id);
	ltfs_u32tobe(cdb + 10, len);

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = SCCMD_READ;
	req.cmdlen          = sizeof(cdb);
	req.datalen         = len;
	req.databuf         = buffer;
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0){
		bool tape_dump = true;

		if (ret == -EDEV_INVALID_FIELD_CDB)
			tape_dump = false;

		ret_ep = _process_errors(device, ret, msg, cmd_desc, true, tape_dump);
		if (ret_ep < 0)
			ret = ret_ep;

		if (id != TC_MAM_PAGE_COHERENCY &&
			id != TC_MAM_APP_VENDER &&
			id != TC_MAM_APP_NAME &&
			id != TC_MAM_APP_VERSION &&
			id != TC_MAM_USER_MEDIUM_LABEL &&
			id != TC_MAM_TEXT_LOCALIZATION_IDENTIFIER &&
			id != TC_MAM_BARCODE &&
			id != TC_MAM_APP_FORMAT_VERSION)
			ltfsmsg(LTFS_INFO, 30233I, ret);
	} else {
		memcpy(buf, buffer + 4, size);
	}

	free(buffer);
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READATTR));

	return ret;
}

int scsipi_ibmtape_allow_overwrite(void *device, const struct tc_position pos)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB16_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "ALLOWOVERW";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_ALLOWOVERW));
	ltfsmsg(LTFS_DEBUG, 30397D, "allow overwrite", (unsigned long long)pos.partition, (unsigned long long)pos.block, priv->drive_serial);

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0] = ALLOW_OVERWRITE;
	cdb[2] = 0x01; /* ALLOW_OVERWRITE Current Position */
	cdb[3] = (unsigned char)(pos.partition & 0xff);
	ltfs_u64tobe(cdb + 4, pos.block);

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = 0;
	req.cmdlen          = sizeof(cdb);
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0){
		if (pos.block == TAPE_BLOCK_MAX && ret == -EDEV_EOD_DETECTED) {
			ltfsmsg(LTFS_DEBUG, 30224D, "Allow Overwrite");
			ret = DEVICE_GOOD;
		} else {
			ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
			if (ret_ep < 0)
				ret = ret_ep;
		}
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_ALLOWOVERW));

	return ret;
}

int scsipi_ibmtape_set_compression(void *device, const bool enable_compression, struct tc_position *pos)
{
	int ret = -EDEV_UNKNOWN;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	unsigned char buf[TC_MP_COMPRESSION_SIZE];

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_SETCOMPRS));

	/* Capture compression setting */
	ret = scsipi_ibmtape_modesense(device, TC_MP_COMPRESSION, TC_MP_PC_CURRENT, 0x00, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	buf[0]  = 0x00;
	buf[1]  = 0x00;

	if(enable_compression)
		buf[18] = buf[18] | 0x80;
	else
		buf[18] = buf[18] & 0x7E;

	ret = scsipi_ibmtape_modeselect(device, buf, sizeof(buf));

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETCOMPRS));

	return ret;
}

int scsipi_ibmtape_set_default(void *device)
{
	int ret = -EDEV_UNKNOWN;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	priv->use_sili = true;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_SETDEFAULT));

	/* Disable Read across EOD on the enterprise drive */
	if (IS_ENTERPRISE(priv->drive_type)) {
		unsigned char buf[TC_MP_READ_WRITE_CTRL_SIZE];
		ltfsmsg(LTFS_DEBUG, 30392D, __FUNCTION__, "Disabling read across EOD");
		ret = scsipi_ibmtape_modesense(device, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
		if (ret < 0) {
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
			return ret;
		}

		buf[0]  = 0x00;
		buf[1]  = 0x00;
		buf[24] = 0x0C;

		ret = scsipi_ibmtape_modeselect(device, buf, sizeof(buf));
		if (ret < 0) {
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
			return ret;
		}
	}

	/* set logical block protection */
	if (global_data.crc_checking) {
		ltfsmsg(LTFS_DEBUG, 30392D, __FUNCTION__, "Setting LBP");
		ret = _set_lbp(device, true);
	} else {
		ltfsmsg(LTFS_DEBUG, 30392D, __FUNCTION__, "Resetting LBP");
		ret = _set_lbp(device, false);
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
	return ret;
}

/**
 * Get cartridge health information
 * @param device a pointer to the ibmtape backend
 * @return 0 on success or a negative value on error
 */

#define LOG_TAPE_ALERT          (0x2E)
#define LOG_PERFORMANCE         (0x37)

#define LOG_PERFORMANCE_CAPACITY_SUB (0x64)
// Scope(7-6): Mount Values
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
	PERF_ACTIVE_CQ_LOSS_W     = 0x7113,	/* < Active CQ loss Write */
};

static uint16_t perfstats[] = {
	PERF_CART_CONDITION,
};

int scsipi_ibmtape_get_cartridge_health(void *device, struct tc_cartridge_health *cart_health)
{
	int ret = -EDEV_UNKNOWN;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	unsigned char logdata[LOGSENSEPAGE];
	unsigned char buf[16];
	int i;
	uint32_t param_size;
	uint64_t loghlt;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETCARTHLTH));

	/* Issue LogPage 0x37 */
	cart_health->tape_efficiency  = UNSUPPORTED_CARTRIDGE_HEALTH;
	ret = scsipi_ibmtape_logsense(device, LOG_PERFORMANCE, logdata, LOGSENSEPAGE);
	if (ret)
		ltfsmsg(LTFS_INFO, 30234I, LOG_PERFORMANCE, ret, "get cart health");
	else {
		for(i = 0; i < (int)((sizeof(perfstats)/sizeof(perfstats[0]))); i++) {
			if (_parse_logPage(logdata, perfstats[i], &param_size, buf, 16)) {
				ltfsmsg(LTFS_INFO, 30235I, LOG_PERFORMANCE, "get cart health");
			} else {
				switch(param_size) {
					case sizeof(uint8_t):
						loghlt = (uint64_t)(buf[0]);
						break;
					case sizeof(uint16_t):
						loghlt = ((uint64_t)buf[0] << 8) + (uint64_t)buf[1];
						break;
					case sizeof(uint32_t):
						loghlt = ((uint64_t)buf[0] << 24) + ((uint64_t)buf[1] << 16)
							+ ((uint64_t)buf[2] << 8) + (uint64_t)buf[3];
						break;
					case sizeof(uint64_t):
						loghlt = ((uint64_t)buf[0] << 56) + ((uint64_t)buf[1] << 48)
							+ ((uint64_t)buf[2] << 40) + ((uint64_t)buf[3] << 32)
							+ ((uint64_t)buf[4] << 24) + ((uint64_t)buf[5] << 16)
							+ ((uint64_t)buf[6] << 8) + (uint64_t)buf[7];
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
	ret = scsipi_ibmtape_logsense(device, LOG_VOLUMESTATS, logdata, LOGSENSEPAGE);
	if (ret < 0)
		ltfsmsg(LTFS_INFO, 30234I, LOG_VOLUMESTATS, ret, "get cart health");
	else {
		for(i = 0; i < (int)((sizeof(volstats)/sizeof(volstats[0]))); i++) {
			if (_parse_logPage(logdata, volstats[i], &param_size, buf, 16)) {
				ltfsmsg(LTFS_INFO, 30235I, LOG_VOLUMESTATS, "get cart health");
			} else {
				switch(param_size) {
					case sizeof(uint8_t):
						loghlt = (uint64_t)(buf[0]);
						break;
					case sizeof(uint16_t):
						loghlt = ((uint64_t)buf[0] << 8) + (uint64_t)buf[1];
						break;
					case sizeof(uint32_t):
						loghlt = ((uint64_t)buf[0] << 24) + ((uint64_t)buf[1] << 16)
							+ ((uint64_t)buf[2] << 8) + (uint64_t)buf[3];
						break;
					case sizeof(uint64_t):
						loghlt = ((uint64_t)buf[0] << 56) + ((uint64_t)buf[1] << 48)
							+ ((uint64_t)buf[2] << 40) + ((uint64_t)buf[3] << 32)
							+ ((uint64_t)buf[4] << 24) + ((uint64_t)buf[5] << 16)
							+ ((uint64_t)buf[6] << 8) + (uint64_t)buf[7];
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

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETCARTHLTH));
	return 0;
}

int scsipi_ibmtape_get_tape_alert(void *device, uint64_t *tape_alert)
{
	int ret = -EDEV_UNKNOWN;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	unsigned char logdata[LOGSENSEPAGE];
	unsigned char buf[16];
	uint32_t param_size;
	int i;
	uint64_t ta;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETTAPEALT));

	/* Issue LogPage 0x2E */
	ta = 0;
	ret = scsipi_ibmtape_logsense(device, LOG_TAPE_ALERT, logdata, LOGSENSEPAGE);
	if (ret < 0)
		ltfsmsg(LTFS_INFO, 30234I, LOG_TAPE_ALERT, ret, "get tape alert");
	else {
		for(i = 1; i <= 64; i++) {
			if (_parse_logPage(logdata, (uint16_t) i, &param_size, buf, 16)
				|| param_size != sizeof(uint8_t)) {
				ltfsmsg(LTFS_INFO, 30235I, LOG_VOLUMESTATS, "get tape alert");
				ta = 0;
			}

			if(buf[0])
				ta += (uint64_t)(1) << (i - 1);
		}
	}

	priv->tape_alert |= ta;
	*tape_alert = priv->tape_alert;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETTAPEALT));
	return ret;
}

int scsipi_ibmtape_clear_tape_alert(void *device, uint64_t tape_alert)
{
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_CLRTAPEALT));
	priv->tape_alert &= ~tape_alert;
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_CLRTAPEALT));
	return 0;
}

int scsipi_ibmtape_get_xattr(void *device, const char *name, char **buf)
{
	int ret = -LTFS_NO_XATTR;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	unsigned char logdata[LOGSENSEPAGE];
	unsigned char logbuf[16];
	uint32_t param_size;
	uint32_t value32;

	struct ltfs_timespec now;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETXATTR));

	if (!strcmp(name, "ltfs.vendor.IBM.mediaCQsLossRate"))
	{
		ret = DEVICE_GOOD;

		/* If first fetch or cache value is too old and value is dirty, refresh value. */
		get_current_timespec(&now);
		if (priv->fetch_sec_acq_loss_w == 0 ||
			((priv->fetch_sec_acq_loss_w + 60 < now.tv_sec) && priv->dirty_acq_loss_w))
		{
			ret = _cdb_logsense(device, LOG_PERFORMANCE, LOG_PERFORMANCE_CAPACITY_SUB, logdata, LOGSENSEPAGE);

			if (ret < 0) {
				ltfsmsg(LTFS_INFO, 30234I, LOG_PERFORMANCE, ret, "get xattr");
			} else {
				if (_parse_logPage(logdata, PERF_ACTIVE_CQ_LOSS_W, &param_size, logbuf, 16)) {
					ltfsmsg(LTFS_INFO, 30235I, LOG_PERFORMANCE,  "get xattr");
					ret = -LTFS_NO_XATTR;
				}
				else {
					switch (param_size) {
						case sizeof(uint32_t):
							value32 = (uint32_t)ltfs_betou32(logbuf);
							priv->acq_loss_w = (float)value32 / 65536.0;
							priv->fetch_sec_acq_loss_w = now.tv_sec;
							priv->dirty_acq_loss_w = false;
							break;
						default:
							ltfsmsg(LTFS_INFO, 30236I, param_size);
							ret = -LTFS_NO_XATTR;
							break;
					}
				}
			}
		}
	}

	if (ret == DEVICE_GOOD) {
		/* The buf allocated here shall be freed in xattr_get_virtual() */
		ret = asprintf(buf, "%2.2f", priv->acq_loss_w);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
			ret = -EDEV_NO_MEMORY;
		} else {
			ret = DEVICE_GOOD;
		}
	} else {
		priv->fetch_sec_acq_loss_w = 0;
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETXATTR));
	return ret;
}

int scsipi_ibmtape_set_xattr(void *device, const char *name, const char *buf, size_t size)
{
	int ret = -LTFS_NO_XATTR;
	char *null_terminated;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;
	int64_t perm_count = 0;

	if (!size)
		return -LTFS_BAD_ARG;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_SETXATTR));

	null_terminated = malloc(size + 1);
	if (! null_terminated) {
		ltfsmsg(LTFS_ERR, 10001E, "scsipi_ibmtape_set_xattr: null_term");
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETXATTR));
		return -LTFS_NO_MEMORY;
	}
	memcpy(null_terminated, buf, size);
	null_terminated[size] = '\0';

	if (! strcmp(name, "ltfs.vendor.IBM.forceErrorWrite")) {
		perm_count = strtoll(null_terminated, NULL, 0);
		if (perm_count < 0) {
			priv->force_writeperm = -perm_count;
			priv->clear_by_pc     = true;
		} else {
			priv->force_writeperm = perm_count;
			priv->clear_by_pc     = false;
		}
		if (priv->force_writeperm && priv->force_writeperm < THRESHOLD_FORCE_WRITE_NO_WRITE)
			priv->force_writeperm = THRESHOLD_FORCE_WRITE_NO_WRITE;
		priv->write_counter = 0;
		ret = DEVICE_GOOD;
	} else if (! strcmp(name, "ltfs.vendor.IBM.forceErrorType")) {
		priv->force_errortype = strtol(null_terminated, NULL, 0);
		ret = DEVICE_GOOD;
	} else if (! strcmp(name, "ltfs.vendor.IBM.forceErrorRead")) {
		perm_count = strtoll(null_terminated, NULL, 0);
		if (perm_count < 0) {
			priv->force_readperm = -perm_count;
			priv->clear_by_pc    = true;
		} else {
			priv->force_readperm = perm_count;
			priv->clear_by_pc    = false;
		}
		priv->read_counter = 0;
		ret = DEVICE_GOOD;
	} else if (! strcmp(name, "ltfs.vendor.IBM.capOffset")) {
		global_data.capacity_offset = strtoul(null_terminated, NULL, 0);
		ret = DEVICE_GOOD;
	}
	free(null_terminated);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETXATTR));
	return ret;
}

#define BLOCKLEN_DATA_SIZE 6

static int _cdb_read_block_limits(void *device) {
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "READ_BLOCK_LIMITS";
	char *msg = NULL;

	unsigned char buf[BLOCKLEN_DATA_SIZE];

	ltfsmsg(LTFS_DEBUG, 30392D, "read block limits", priv->drive_serial);

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0] = READ_BLOCK_LIMITS;

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = SCCMD_READ;
	req.cmdlen          = sizeof(cdb);
	req.datalen         = sizeof(buf);
	req.databuf         = buf;
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0){
		ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
		if (ret_ep < 0)
			ret = ret_ep;
	} else {
		ret =  ((unsigned int) buf[1] & 0xFF) << 16;
		ret += ((unsigned int) buf[2] & 0xFF) << 8;
		ret += ((unsigned int) buf[3] & 0xFF);
	}

	return ret;
}

int scsipi_ibmtape_get_parameters(void *device, struct tc_current_param *params)
{
	int ret = -EDEV_UNKNOWN;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETPARAM));

	if (priv->loaded) {
		params->cart_type = priv->cart_type;
		params->density   = priv->density_code;
		params->write_protected = 0;

		if (IS_ENTERPRISE(priv->drive_type)) {
			unsigned char buf[TC_MP_MEDIUM_SENSE_SIZE];

			ret = scsipi_ibmtape_modesense(device, TC_MP_MEDIUM_SENSE, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
			if (ret < 0)
				goto out;

			char wp_flag = buf[26];

			if (wp_flag & 0x80) {
				params->write_protected |= VOL_PHYSICAL_WP;
			} else if (wp_flag & 0x01) {
				params->write_protected |= VOL_PERM_WP;
			} else if (wp_flag & 0x10) {
				params->write_protected |= VOL_PERS_WP;
			}

			/* TODO: Following field shall be implemented in the future */
			/*
			if ( (priv->cart_type & 0xF0) == 0xC0 || (priv->cart_type & 0xF0) == 0xA0 )
				params->is_worm = true;

			if (priv->density_code & TEST_CRYPTO)
				params->is_encrypted = true;
			*/
		} else {
			unsigned char buf[MODE_DEVICE_CONFIG_SIZE];

			ret = scsipi_ibmtape_modesense(device, MODE_DEVICE_CONFIG, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
			if (ret < 0)
				goto out;

			if (buf[3] & 0x80) {
				params->write_protected |= VOL_PHYSICAL_WP;
			}

			/* TODO: Following field shall be implemented in the future */
			/*
			if ( (priv->cart_type & 0x0F) == 0x0C)
				params->is_worm = true;

			//TODO: Store is_crypto based on LP17:200h
			*/
		}
	} else {
		params->cart_type = priv->cart_type;
		params->density   = priv->density_code;
	}

	if (global_data.crc_checking)
		params->max_blksize = MIN(_cdb_read_block_limits(device), SG_MAX_BLOCK_SIZE - 4);
	else
		params->max_blksize = MIN(_cdb_read_block_limits(device), SG_MAX_BLOCK_SIZE);

	ret = 0;

out:
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETPARAM));
	return ret;
}

#define LOG_VOL_STATISTICS         (0x17)
#define LOG_VOL_USED_CAPACITY      (0x203)
#define LOG_VOL_PART_HEADER_SIZE   (4)

int scsipi_ibmtape_get_eod_status(void *device, int part)
{
	int ret = -EDEV_UNKNOWN;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	/*
	 * This feature requires new tape drive firmware
	 * to support logpage 17h correctly
	 */
	unsigned char logdata[LOGSENSEPAGE];
	unsigned char buf[16];
	unsigned int i;
	uint32_t param_size;
	uint32_t part_cap[2] = {EOD_UNKNOWN, EOD_UNKNOWN};

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETEODSTAT));

	/* Issue LogPage 0x17 */
	ret = scsipi_ibmtape_logsense(device, LOG_VOLUMESTATS, logdata, LOGSENSEPAGE);
	if (ret) {
		ltfsmsg(LTFS_WARN, 30237W, LOG_VOLUMESTATS, ret);
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETEODSTAT));
		return EOD_UNKNOWN;
	}

	/* Parse Approximate used native capacity of partitions (0x203)*/
	if (_parse_logPage(logdata, (uint16_t)VOLSTATS_PART_USED_CAP, &param_size, buf, sizeof(buf))
		|| (param_size != sizeof(buf) ) ) {
		ltfsmsg(LTFS_WARN, 30238W);
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETEODSTAT));
		return EOD_UNKNOWN;
	}

	i = 0;
	while (i < sizeof(buf)) {
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
			ltfsmsg(LTFS_WARN, 30239W, i, part_buf, len);

		i += (len + 1);
	}

	/* Create return value */
	if(part_cap[part] == 0xFFFFFFFF)
		ret = EOD_MISSING;
	else
		ret = EOD_GOOD;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETEODSTAT));
	return ret;
}

static const char *_generate_product_name(const char *product_id)
{
	const char *product_name = "";
	int i = 0;

	for (i = 0; ibm_supported_drives[i]; ++i) {
		if (strncmp(ibm_supported_drives[i]->product_id, product_id,
					strlen(ibm_supported_drives[i]->product_id)) == 0) {
			product_name = ibm_supported_drives[i]->product_name;
			break;
		}
	}

	return product_name;
}

int scsipi_ibmtape_get_device_list(struct tc_drive_info *buf, int count)
{
	int ret = -1;
	DIR *dp = NULL;
	struct dirent *entry = NULL;
	int found = 0;
	int flags;
	struct scsipi_tape dev;
	char devname[PATH_MAX];
	scsi_device_identifier identifier;

	dp = opendir("/dev");
	if (!dp) {
		ltfsmsg(LTFS_INFO, 30240I);
		return -EDEV_DEVICE_UNOPENABLE;
	}

	dev.fd = -1;
	dev.is_data_key_set = false;

	while ((entry = readdir(dp)) != NULL) {
		if (strncmp(entry->d_name, "nst", strlen("nst")))
			continue;

		sprintf(devname, "/dev/%s", entry->d_name);

		dev.fd = open(devname, O_RDONLY | O_NONBLOCK);
		if (dev.fd < 0)
			continue;

		/* Get the device back to blocking mode */
		flags = fcntl(dev.fd, F_GETFL, 0);
		if (flags < 0) {
			ltfsmsg(LTFS_INFO, 30273I, "get", flags);
			close(dev.fd);
			continue;
		}
		flags = (flags & (~O_NONBLOCK));
		flags = fcntl(dev.fd, F_SETFL, 0);
		if (flags < 0) {
			ltfsmsg(LTFS_INFO, 30273I, "set", flags);
			close(dev.fd);
			continue;
		}

		ret = scsipi_get_drive_identifier(&dev, &identifier);
		if (ret < 0) {
			close(dev.fd);
			dev.fd = -1;
			continue;
		}

		if (found < count && buf) {
			snprintf(buf[found].name, TAPE_DEVNAME_LEN_MAX + 1, "%s", devname);
			snprintf(buf[found].vendor, TAPE_VENDOR_NAME_LEN_MAX + 1, "%s", identifier.vendor_id);
			snprintf(buf[found].model, TAPE_MODEL_NAME_LEN_MAX + 1, "%s", identifier.product_id);
			snprintf(buf[found].serial_number, TAPE_SERIAL_LEN_MAX + 1, "%s", identifier.unit_serial);
			snprintf(buf[found].product_name, PRODUCT_NAME_LENGTH + 1, "%s", _generate_product_name(identifier.product_id));
		}
		found++;

		close(dev.fd);
		dev.fd = -1;
	}

	closedir(dp);

	return found;
}

void scsipi_ibmtape_help_message(void)
{
	ltfsresult(30399I, default_device);
}

int scsipi_ibmtape_parse_opts(void *device, void *opt_args)
{
	struct fuse_args *args = (struct fuse_args *) opt_args;
	int ret;

	/* fuse_opt_parse can handle a NULL device parameter just fine */
	ret = fuse_opt_parse(args, &global_data, scsipi_ibmtape_global_opts, null_parser);
	if (ret < 0) {
		return ret;
	}

	/* Validate scsi logical block protection */
	if (global_data.str_crc_checking) {
		if (strcasecmp(global_data.str_crc_checking, "on") == 0)
			global_data.crc_checking = 1;
		else if (strcasecmp(global_data.str_crc_checking, "off") == 0)
			global_data.crc_checking = 0;
		else {
			ltfsmsg(LTFS_ERR, 30241E, global_data.str_crc_checking);
			return -EDEV_INTERNAL_ERROR;
		}
	} else
		global_data.crc_checking = 0;

	return 0;
}

const char *scsipi_ibmtape_default_device_name(void)
{
	const char *devname;
	devname = default_device;
	return devname;
}

static int _cdb_spin(void *device, const uint16_t sps, unsigned char **buffer, size_t * const size)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB12_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "SPIN";
	char *msg = NULL;
	size_t len = *size + 4;

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	*buffer = calloc(len, sizeof(unsigned char));
	if (! *buffer) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return -EDEV_NO_MEMORY;
	}

	/* Build CDB */
	cdb[0] = SPIN;
	cdb[1] = 0x20;
	ltfs_u16tobe(cdb + 2, sps); /* SECURITY PROTOCOL SPECIFIC */
	ltfs_u32tobe(cdb + 6, len);

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = SCCMD_READ;
	req.cmdlen          = sizeof(cdb);
	req.datalen         = len;
	req.databuf         = *buffer;
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0){
		ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
		if (ret_ep < 0)
			ret = ret_ep;
	}

	*size = ltfs_betou16((*buffer) + 2);

	return ret;
}

int _cdb_spout(void *device, const uint16_t sps,
			   unsigned char* const buffer, const size_t size)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB12_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "SPOUT";
	char *msg = NULL;

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0] = SPOUT;
	cdb[1] = 0x20;
	ltfs_u16tobe(cdb + 2, sps); /* SECURITY PROTOCOL SPECIFIC */
	ltfs_u32tobe(cdb + 6, size);

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = SCCMD_WRITE;
	req.cmdlen          = sizeof(cdb);
	req.datalen         = size;
	req.databuf         = buffer;
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret < 0){
		ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
		if (ret_ep < 0)
			ret = ret_ep;
	}

	return ret;
}

static void ltfsmscsipi_keyalias(const char * const title, const unsigned char * const keyalias)
{
	char s[128] = {'\0'};

	if (keyalias)
		sprintf(s, "keyalias = %c%c%c%02X%02X%02X%02X%02X%02X%02X%02X%02X", keyalias[0],
				keyalias[1], keyalias[2], keyalias[3], keyalias[4], keyalias[5], keyalias[6],
				keyalias[7], keyalias[8], keyalias[9], keyalias[10], keyalias[11]);
	else
		sprintf(s, "keyalias: NULL");

	ltfsmsg(LTFS_DEBUG, 30392D, title, s);
}

static bool is_ame(void *device)
{
	unsigned char buf[TC_MP_READ_WRITE_CTRL_SIZE] = {0};
	const int ret = scsipi_ibmtape_modesense(device, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));

	if (ret != 0) {
		char message[100] = {0};
		sprintf(message, "failed to get MP %02Xh (%d)", TC_MP_READ_WRITE_CTRL, ret);
		ltfsmsg(LTFS_DEBUG, 30392D, __FUNCTION__, message);

		return false; /* Consider that the encryption method is not AME */
	} else {
		const unsigned char encryption_method = buf[16 + 27];
		char message[100] = {0};
		char *method = NULL;
		switch (encryption_method) {
			case 0x00:
				method = "None";
				break;
			case 0x10:
				method = "System";
				break;
			case 0x1F:
				method = "Controller";
				break;
			case 0x50:
				method = "Application";
				break;
			case 0x60:
				method = "Library";
				break;
			case 0x70:
				method = "Internal";
				break;
			case 0xFF:
				method = "Custom";
				break;
			default:
				method = "Unknown";
				break;
		}
		sprintf(message, "Encryption Method is %s (0x%02X)", method, encryption_method);
		ltfsmsg(LTFS_DEBUG, 30392D, __FUNCTION__, message);

		if (encryption_method != 0x50) {
			ltfsmsg(LTFS_ERR, 30242E, method, encryption_method);
		}
		return encryption_method == 0x50;
	}
}

static int is_encryption_capable(void *device)
{
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	if (IS_LTO(priv->drive_type)) {
		ltfsmsg(LTFS_ERR, 30243E, priv->drive_type);
		return -EDEV_INTERNAL_ERROR;
	}

	if (! is_ame(device))
		return -EDEV_INTERNAL_ERROR;

	return DEVICE_GOOD;
}

int scsipi_ibmtape_set_key(void *device, const unsigned char *keyalias, const unsigned char *key)
{
	int ret = -EDEV_UNKNOWN;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	/*
	 * Encryption  Decryption     Key         DKi      keyalias
	 *    Mode        Mode
	 * 0h Disable  0h Disable  Prohibited  Prohibited    NULL
	 * 2h Encrypt  3h Mixed    Mandatory    Optional    !NULL
	 */
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_SETKEY));
	ret = is_encryption_capable(device);
	if (ret < 0) {
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETKEY));
		return ret;
	}

	const uint16_t sps = 0x10;
	const size_t size = keyalias ? 20 + DK_LENGTH + 4 + DKI_LENGTH : 20;
	uint8_t *buffer = calloc(size, sizeof(uint8_t));
	if (! buffer) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		ret = -EDEV_NO_MEMORY;
		goto out;
	}

	unsigned char buf[TC_MP_READ_WRITE_CTRL_SIZE] = {0};
	ret = scsipi_ibmtape_modesense(device, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
	if (ret != DEVICE_GOOD)
		goto out;

	ltfs_u16tobe(buffer + 0, sps);
	ltfs_u16tobe(buffer + 2, size - 4);
	buffer[4] = 0x40; /* SCOPE: 010b All I_T Nexus, LOCK: 0 */
	/*
	 * CEEM: 00b Vendor specific
	 * RDMC: 00b The device entity shall mark each encrypted logical block per the default setting
	 *           for the algorithm.
	 * SDK:   0b The logical block encryption key sent in this page shall be the logical block
	 *           encryption key used for both encryption and decryption.
	 * CKOD:  0b The demounting of a volume shall not affect the logical block encryption parameters.
	 * CKORP: 0b Clear key on reservation preempt (CKORP) bit
	 * CKORL: 0b Clear key on reservation loss (CKORL) bit
	 */
	buffer[5] = 0x00;
	enum { DISABLE = 0, EXTERNAL = 1, ENCRYPT = 2 };
	buffer[6] = keyalias ? ENCRYPT : DISABLE; /* ENCRYPTION MODE */
	enum { /* DISABLE = 0, */ RAW = 1, DECRYPT = 2, MIXED = 3 };
	buffer[7] = keyalias ? MIXED : DISABLE; /* DECRYPTION MODE */
	buffer[8] = 1; /* ALGORITHM INDEX */
	buffer[9] = 0; /* LOGICAL BLOCK ENCRYPTION KEY FORMAT: plain-text key */
	buffer[10] = 0; /* KAD FORMAT: Unspecified */
	ltfs_u16tobe(buffer + 18, keyalias ? DK_LENGTH : 0x00); /* LOGICAL BLOCK ENCRYPTION KEY LENGTH */
	if (keyalias) {
		if (! key) {
			ret = -EINVAL;
			goto free;
		}
		memcpy(buffer + 20, key, DK_LENGTH); /* LOGICAL BLOCK ENCRYPTION KEY */
		buffer[20 + DK_LENGTH] = 0x01; /* KEY DESCRIPTOR TYPE: 01h DKi (Data Key Identifier) */
		ltfs_u16tobe(buffer + 20 + DK_LENGTH + 2, DKI_LENGTH);
		memcpy(buffer + 20 + 0x20 + 4, keyalias, DKI_LENGTH);
	}

	const char * const title = "set key:";
	ltfsmscsipi_keyalias(title, keyalias);

	ret = _cdb_spout(device, sps, buffer, size);
	if (ret != DEVICE_GOOD)
		goto free;

	priv->dev.is_data_key_set = keyalias != NULL;

	memset(buf, 0, sizeof(buf));
	ret = scsipi_ibmtape_modesense(device, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
	if (ret != DEVICE_GOOD)
		goto out;

free:
	free(buffer);

out:
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETKEY));
	return ret;
}

static void show_hex_dump(const char * const title, const uint8_t * const buf, const size_t size)
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

	ltfsmsg(LTFS_DEBUG, 30392D, title, s);
}

int scsipi_ibmtape_get_keyalias(void *device, unsigned char **keyalias)
{
	int ret = -EDEV_UNKNOWN;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETKEYALIAS));
	ret = is_encryption_capable(device);
	if (ret < 0) {
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETKEYALIAS));
		return ret;
	}

	const uint16_t sps = 0x21;
	uint8_t *buffer = NULL;
	size_t size = 0;
	int i = 0;

	memset(priv->dki, 0, sizeof(priv->dki));
	*keyalias = NULL;

	/*
	 * 1st loop: Get the page length.
	 * 2nd loop: Get full data in the page.
	 */
	for (i = 0; i < 2; ++i) {
		free(buffer);
		ret = _cdb_spin(device, sps, &buffer, &size);
		if (ret != DEVICE_GOOD)
			goto free;
	}

	show_hex_dump("SPIN:", buffer, size + 4);

	const unsigned char encryption_status = buffer[12] & 0xF;
	enum {
		ENC_STAT_INCAPABLE                          = 0,
		ENC_STAT_NOT_YET_BEEN_READ                  = 1,
		ENC_STAT_NOT_A_LOGICAL_BLOCK                = 2,
		ENC_STAT_NOT_ENCRYPTED                      = 3,
		ENC_STAT_ENCRYPTED_BY_UNSUPPORTED_ALGORITHM = 4,
		ENC_STAT_ENCRYPTED_BY_SUPPORTED_ALGORITHM   = 5,
		ENC_STAT_ENCRYPTED_BY_OTHER_KEY             = 6,
		ENC_STAT_RESERVED, /* 7h-Fh */
	};
	if (encryption_status == ENC_STAT_ENCRYPTED_BY_UNSUPPORTED_ALGORITHM ||
		encryption_status == ENC_STAT_ENCRYPTED_BY_SUPPORTED_ALGORITHM ||
		encryption_status == ENC_STAT_ENCRYPTED_BY_OTHER_KEY) {
		uint offset = 16; /* offset of key descriptor */
		while (offset <= size && buffer[offset] != 1) {
			offset += ltfs_betou16(buffer + offset + 2) + 4;
		}
		if (offset <= size && buffer[offset] == 1) {
			const uint dki_length = ((int) buffer[offset + 2]) << 8 | buffer[offset + 3];
			if (offset + dki_length <= size) {
				int n = dki_length < sizeof(priv->dki) ? dki_length : sizeof(priv->dki);
				memcpy(priv->dki, &buffer[offset + 4], n);
				*keyalias = priv->dki;
			}
		}
	}

	const char * const title = "get key-alias:";
	ltfsmscsipi_keyalias(title, priv->dki);

free:
	free(buffer);
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETKEYALIAS));
	return ret;
}

int scsipi_ibmtape_takedump_drive(void *device, bool capture_unforced)
{
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_TAKEDUMPDRV));
	_take_dump(priv, capture_unforced);
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_TAKEDUMPDRV));

	return 0;
}

int scsipi_ibmtape_is_mountable(void *device, const char *barcode, const unsigned char cart_type,
							const unsigned char density)
{
	int ret;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_ISMOUNTABLE));

	ret = ibm_tape_is_mountable( priv->drive_type,
								barcode,
								cart_type,
								density,
								global_data.strict_drive);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_ISMOUNTABLE));

	return ret;
}

bool scsipi_ibmtape_is_readonly(void *device)
{
	int ret;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	ret = ibm_tape_is_mountable( priv->drive_type,
								NULL,
								priv->cart_type,
								priv->density_code,
								global_data.strict_drive);

	if (ret == MEDIUM_READONLY)
		return true;
	else
		return false;
}

int scsipi_ibmtape_get_worm_status(void *device, bool *is_worm)
{
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETWORMSTAT));
	*is_worm = false;
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETWORMSTAT));
	return 0;
}

int scsipi_ibmtape_get_serialnumber(void *device, char **result)
{
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	CHECK_ARG_NULL(device, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(result, -LTFS_NULL_ARG);

	ltfs_profiler_add_entry(priv->profiler, NULL, CHANGER_REQ_ENTER(REQ_TC_GETSER));

	*result = strdup((const char *) priv->drive_serial);
	if (! *result) {
		ltfsmsg(LTFS_ERR, 10001E, "scsipi_ibmtape_get_serialnumber: result");
		ltfs_profiler_add_entry(priv->profiler, NULL, CHANGER_REQ_EXIT(REQ_TC_GETSER));
		return -EDEV_NO_MEMORY;
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, CHANGER_REQ_EXIT(REQ_TC_GETSER));

	return 0;
}

int scsipi_ibmtape_set_profiler(void *device, char *work_dir, bool enable)
{
	int rc = 0;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	char *path;
	FILE *p;
	struct timer_info timerinfo;

	if (enable) {
		if (priv->profiler)
			return 0;

		if(!work_dir)
			return -LTFS_BAD_ARG;

		rc = asprintf(&path, "%s/%s%s%s", work_dir, DRIVER_PROFILER_BASE,
					  "DUMMY", PROFILER_EXTENSION);
		if (rc < 0) {
			ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
			return -EDEV_NO_MEMORY;
		}

		p = fopen(path, PROFILER_FILE_MODE);

		free(path);

		if (! p)
			rc = -LTFS_FILE_ERR;
		else {
			get_timer_info(&timerinfo);
			fwrite((void*)&timerinfo, sizeof(timerinfo), 1, p);
			priv->profiler = p;
			rc = 0;
		}
	} else {
		if (priv->profiler) {
			fclose(priv->profiler);
			priv->profiler = NULL;
		}
	}

	return rc;
}

int scsipi_ibmtape_get_block_in_buffer(void *device, uint32_t *block)
{
	int ret = -EDEV_UNKNOWN;
	int ret_ep = DEVICE_GOOD;
	struct scsipi_ibmtape_data *priv = (struct scsipi_ibmtape_data*)device;

	scsireq_t req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "READPOS";
	char *msg = NULL;
	unsigned char buf[REDPOS_EXT_LEN];

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_READPOS));

	/* Zero out the CDB and the result buffer */
	ret = init_scsireq(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));

	/* Build CDB */
	cdb[0] = READ_POSITION;
	cdb[1] = 0x08; /* Extended Format */

	timeout = ibm_tape_get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.flags           = SCCMD_READ;
	req.cmdlen          = sizeof(cdb);
	req.datalen         = sizeof(buf);
	req.databuf         = buf;
	memcpy(req.cmd, cdb, sizeof(cdb));
	req.timeout         = SGConversion(timeout);

	ret = scsipi_issue_cdb_command(&priv->dev, &req, cmd_desc, &msg);
	if (ret == DEVICE_GOOD) {
		*block = (buf[5] << 16) + (buf[6] << 8) + (int)buf[7];

		ltfsmsg(LTFS_DEBUG, 30398D, "blocks-in-buffer",
				(unsigned long long)*block, (unsigned long long)0, (unsigned long long)0, priv->drive_serial);
	} else {
		ret_ep = _process_errors(device, ret, msg, cmd_desc, true, true);
		if (ret_ep < 0)
			ret = ret_ep;
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READPOS));

	return ret;
}

struct tape_ops scsipi_ibmtape_handler = {
	.open                   = scsipi_ibmtape_open,
	.reopen                 = scsipi_ibmtape_reopen,
	.close                  = scsipi_ibmtape_close,
	.close_raw              = scsipi_ibmtape_close_raw,
	.is_connected           = scsipi_ibmtape_is_connected,
	.inquiry                = scsipi_ibmtape_inquiry,
	.inquiry_page           = scsipi_ibmtape_inquiry_page,
	.test_unit_ready        = scsipi_ibmtape_test_unit_ready,
	.read                   = scsipi_ibmtape_read,
	.write                  = scsipi_ibmtape_write,
	.writefm                = scsipi_ibmtape_writefm,
	.rewind                 = scsipi_ibmtape_rewind,
	.locate                 = scsipi_ibmtape_locate,
	.space                  = scsipi_ibmtape_space,
	.erase                  = scsipi_ibmtape_erase,
	.load                   = scsipi_ibmtape_load,
	.unload                 = scsipi_ibmtape_unload,
	.readpos                = scsipi_ibmtape_readpos,
	.setcap                 = scsipi_ibmtape_setcap,
	.format                 = scsipi_ibmtape_format,
	.remaining_capacity     = scsipi_ibmtape_remaining_capacity,
	.logsense               = scsipi_ibmtape_logsense,
	.modesense              = scsipi_ibmtape_modesense,
	.modeselect             = scsipi_ibmtape_modeselect,
	.reserve_unit           = scsipi_ibmtape_reserve,
	.release_unit           = scsipi_ibmtape_release,
	.prevent_medium_removal = scsipi_ibmtape_prevent_medium_removal,
	.allow_medium_removal   = scsipi_ibmtape_allow_medium_removal,
	.write_attribute        = scsipi_ibmtape_write_attribute,
	.read_attribute         = scsipi_ibmtape_read_attribute,
	.allow_overwrite        = scsipi_ibmtape_allow_overwrite,
	// May be command combination
	.set_compression        = scsipi_ibmtape_set_compression,
	.set_default            = scsipi_ibmtape_set_default,
	.get_cartridge_health   = scsipi_ibmtape_get_cartridge_health,
	.get_tape_alert         = scsipi_ibmtape_get_tape_alert,
	.clear_tape_alert       = scsipi_ibmtape_clear_tape_alert,
	.get_xattr              = scsipi_ibmtape_get_xattr,
	.set_xattr              = scsipi_ibmtape_set_xattr,
	.get_parameters         = scsipi_ibmtape_get_parameters,
	.get_eod_status         = scsipi_ibmtape_get_eod_status,
	.get_device_list        = scsipi_ibmtape_get_device_list,
	.help_message           = scsipi_ibmtape_help_message,
	.parse_opts             = scsipi_ibmtape_parse_opts,
	.default_device_name    = scsipi_ibmtape_default_device_name,
	.set_key                = scsipi_ibmtape_set_key,
	.get_keyalias           = scsipi_ibmtape_get_keyalias,
	.takedump_drive         = scsipi_ibmtape_takedump_drive,
	.is_mountable           = scsipi_ibmtape_is_mountable,
	.get_worm_status        = scsipi_ibmtape_get_worm_status,
	.get_serialnumber       = scsipi_ibmtape_get_serialnumber,
	.set_profiler           = scsipi_ibmtape_set_profiler,
	.get_block_in_buffer    = scsipi_ibmtape_get_block_in_buffer,
	.is_readonly            = scsipi_ibmtape_is_readonly,
};

struct tape_ops *tape_dev_get_ops(void)
{
	standard_table = standard_tape_errors;
	vendor_table = ibm_tape_errors;
	return &scsipi_ibmtape_handler;
}

extern char tape_linux_sg_ibmtape_dat[];

const char *tape_dev_get_message_bundle_name(void **message_data)
{
	*message_data = tape_linux_sg_ibmtape_dat;
	return "tape_linux_sg_ibmtape";
}
