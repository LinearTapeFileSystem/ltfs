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
** FILE NAME:       tape_drivers/osx/iokit/iokit_tape.c
**
** DESCRIPTION:     LTFS tape drive backend implementation for OS X
**
** AUTHORS:         Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
**                  Michael A. Richmond
**                  IBM Almaden Research Center
**                  mar@almaden.ibm.com
**
*************************************************************************************
*/

#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include "ltfs_copyright.h"
#include "libltfs/ltfslogging.h"
#include "libltfs/fs.h"
#include "libltfs/ltfs_endian.h"
#include "libltfs/arch/time_internal.h"
#include "kmi/key_format_ltfs.h"

/* Common header of backend */
#include "reed_solomon_crc.h"
#include "crc32c_crc.h"
#include "vendor_compat.h"

/* iokit functions */
#ifdef VERSION
#undef VERSION // Undef VERSION because it is conflicted into the header in IOKit
#endif
#include "iokit_service.h"
#include "iokit_scsi.h"

/* Definitions of this backend*/
#include "iokit_tape.h"

#include "libltfs/ltfs_fuse_version.h"
#include <fuse.h>

volatile char *copyright = LTFS_COPYRIGHT_0"\n"LTFS_COPYRIGHT_1"\n"LTFS_COPYRIGHT_2"\n" \
	LTFS_COPYRIGHT_3"\n"LTFS_COPYRIGHT_4"\n"LTFS_COPYRIGHT_5"\n";

// Default device name
const char *default_device = "0";

// Global values
struct iokit_global_data global_data;

// Definitions
#define LOG_PAGE_HEADER_SIZE      (4)
#define LOG_PAGE_PARAMSIZE_OFFSET (3)
#define LOG_PAGE_PARAM_OFFSET     (4)

#define IOKIT_MAX_BLOCK_SIZE (1 * MB)

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

/* Forward references (For keep function order to struct tape_ops) */
int iokit_readpos(void *device, struct tc_position *pos);
int iokit_locate(void *device, struct tc_position dest, struct tc_position *pos);
int iokit_logsense(void *device, const uint8_t page, const uint8_t subpage,
				   unsigned char *buf, const size_t size);
int iokit_modesense(void *device, const unsigned char page, const TC_MP_PC_TYPE pc,
							const unsigned char subpage, unsigned char *buf, const size_t size);
int iokit_modeselect(void *device, unsigned char *buf, const size_t size);
static const char *_generate_product_name(const char *product_id);

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
 * Parse option for iokit driver
 * Option value cannot be stored into device instance because option parser calls
 * before opening a device.
 * @param devname device name of the iokit
 * @return a pointer to the iokit backend on success or NULL on error
 */

#define iokit_opt(templ,offset,value)							\
	{ templ, offsetof(struct iokit_global_data, offset), value }

static struct fuse_opt iokit_global_opts[] = {
	iokit_opt("scsi_lbprotect=%s", str_crc_checking, 0),
	iokit_opt("strict_drive",      strict_drive, 1),
	iokit_opt("nostrict_drive",    strict_drive, 0),
	iokit_opt("autodump",          disable_auto_dump, 0),
	iokit_opt("noautodump",        disable_auto_dump, 1),
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
	struct iokit_data *priv = (struct iokit_data*)device;

	unsigned char buf[TC_MP_SUB_DP_CTRL_SIZE];
	unsigned char buf_ext[TC_MP_INIT_EXT_SIZE];
	unsigned char lbp_method = LBP_DISABLE;

	/* Check logical block protection capability */
	if (IS_ENTERPRISE(priv->drive_type)) {
		ret = iokit_modesense(device, TC_MP_INIT_EXT, TC_MP_PC_CURRENT, 0x00, buf_ext, sizeof(buf_ext));
		if (ret < 0)
			return ret;

		if (buf_ext[0x12] & TC_MP_INIT_EXT_LBP_CRC32C)
			lbp_method = CRC32C_CRC;
		else
			lbp_method = REED_SOLOMON_CRC;
	} else {
		/*
		 * LTO drive doesn't have a modepage to support CRC32C or not
		 * So use CRC32C based on it's generation
		 */
		if (DRIVE_GEN(priv->drive_type) >= 0x07)
			lbp_method = CRC32C_CRC;
		else
			lbp_method = REED_SOLOMON_CRC;
	}

	/* set logical block protection */
	ltfsmsg(LTFS_DEBUG, 30993D, "LBP Enable", enable, "");
	ltfsmsg(LTFS_DEBUG, 30993D, "LBP Method", lbp_method, "");
	ret = iokit_modesense(device, TC_MP_CTRL, TC_MP_PC_CURRENT,
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

	ret = iokit_modeselect(device, buf, sizeof(buf));

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
			ltfsmsg(LTFS_INFO, 30853I);
		} else {
			priv->f_crc_enc   = NULL;
			priv->f_crc_check = NULL;
			ltfsmsg(LTFS_INFO, 30854I);
		}
	}

	return ret;
}

static bool is_dump_required(struct iokit_data *priv, int ret, bool *capture_unforced)
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
static int _cdb_force_dump(struct iokit_data *priv);

static int _get_dump(struct iokit_data *priv, char *fname)
{
	int ret = 0;

	long long               data_length, buf_offset;
	int                     dumpfd = -1;
	int                     transfer_size, num_transfers, excess_transfer;
	int                     i, bytes;
	unsigned char           cap_buf[DUMP_HEADER_SIZE];
	unsigned char           *dump_buf;
	int                     buf_id;

	ltfsmsg(LTFS_INFO, 30855I, fname);

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
		ltfsmsg(LTFS_WARN, 30856W, errno);
		free(dump_buf);
		return -2;
	}

	/* get the total number of transfers */
	num_transfers   = data_length / transfer_size;
	excess_transfer = data_length % transfer_size;
	if(excess_transfer)
		num_transfers += 1;

	/* Total dump data length is %lld. Total number of transfers is %d. */
	ltfsmsg(LTFS_DEBUG, 30857D, data_length);
	ltfsmsg(LTFS_DEBUG, 30858D, num_transfers);

	/* start to transfer data */
	buf_offset = 0;
	i = 0;
	ltfsmsg(LTFS_DEBUG, 30859D);
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
			ltfsmsg(LTFS_WARN, 30860W, ret);
			free(dump_buf);
			close(dumpfd);
			return ret;
		}

		/* write buffer data into dump file */
		bytes = write(dumpfd, dump_buf, length);
		if(bytes == -1)
		{
			ltfsmsg(LTFS_WARN, 30861W, ret);
			free(dump_buf);
			close(dumpfd);
			return -1;
		}

		if(bytes != length)
		{
			ltfsmsg(LTFS_WARN, 30862W, bytes, length);
			free(dump_buf);
			close(dumpfd);
			return -2;
		}

		/* update offset and num_transfers, free buffer */
		buf_offset += transfer_size;
		num_transfers -= 1;

	} /* end of while(num_transfers) */

	free(dump_buf);
	close(dumpfd);

	return ret;
}

static int _take_dump(struct iokit_data *priv, bool capture_unforced)
{
	char      fname_base[1024];
	char      fname[1024];
	time_t    now;
	struct tm *tm_now;

	if (priv->vendor != VENDOR_IBM)
		return 0;

	/* Following logic is for IBM tape drives */

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
		ltfsmsg(LTFS_INFO, 30863I);
		strcpy(fname, fname_base);
		strcat(fname, ".dmp");
		_get_dump(priv, fname);
	}

	ltfsmsg(LTFS_INFO, 30864I);
	_cdb_force_dump(priv);
	strcpy(fname, fname_base);
	strcat(fname, "_f.dmp");
	_get_dump(priv, fname);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_TAKEDUMPDRV));

	return 0;
}

static void _process_errors(struct iokit_data *priv, int ret, char *msg, char *cmd, bool take_dump)
{
	bool unforced_dump;

	if (msg != NULL) {
		ltfsmsg(LTFS_INFO, 30865I, cmd, msg, ret, priv->devname);
	} else {
		ltfsmsg(LTFS_ERR, 30866E, cmd, ret, priv->devname);
	}

	if (priv) {
		if (take_dump && !global_data.disable_auto_dump
			&& is_dump_required(priv, ret, &unforced_dump)) {
			(void)_take_dump(priv, unforced_dump);
		}
	}
}

static int _cdb_read_buffer(void *device, int id, unsigned char *buf, size_t offset, size_t len, int type)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB10_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "READ_BUFFER";
	char *msg = NULL;

	ltfsmsg(LTFS_DEBUG, 30993D, "read buffer", id, priv->drive_serial);

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

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

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_FROM_TARGET_TO_INITIATOR;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.dxfer_len       = len;
	req.dxferp          = buf;
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0){
		_process_errors(device, ret, msg, cmd_desc, true);
	}

	return ret;
}

static int _cdb_force_dump(struct iokit_data *priv)
{
	int ret = -EDEV_UNKNOWN;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "FORCE_DUMP";
	char *msg = NULL;

	unsigned char buf[SENDDIAG_BUF_LEN];

	ltfsmsg(LTFS_DEBUG, 30993D, "force dump", 0, priv->drive_serial);

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));
	memset(&buf, 0, sizeof(buf));

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

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_FROM_INITIATOR_TO_TARGET;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.dxfer_len       = SENDDIAG_BUF_LEN;
	req.dxferp          = buf;
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0){
		_process_errors(priv, ret, msg, cmd_desc, true);
	}

	return ret;
}

static int _cdb_pri(void *device, unsigned char *buf, int size)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB10_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "PRI";
	char *msg = NULL;

	memset(cdb, 0, sizeof(cdb));
	memset(buf, 0, size);

	/* Build CDB */
	cdb[0] = PERSISTENT_RESERVE_IN;
	cdb[1] = 0x03; /* Full Info */
	cdb[6] = (unsigned char)(size >> 16) & 0xFF;
	cdb[7] = (unsigned char)(size >> 8)  & 0xFF;
	cdb[8] = (unsigned char) size        & 0xFF;

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_FROM_TARGET_TO_INITIATOR;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.dxfer_len       = size;
	req.dxferp          = buf;
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0){
		_process_errors(device, ret, msg, cmd_desc, true);
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
	if (holder) {
		memcpy(r->key, cur, KEYLEN);
		ibm_tape_parsekey(cur, r);
	} else
		ret = -EDEV_INTERNAL_ERROR;

	free(buf);

	return ret;
}

static int _cdb_pro(void *device,
					enum pro_action action, enum pro_type type,
					unsigned char *key, unsigned char *sakey)
{
	int ret = -EDEV_UNKNOWN, f_ret;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB10_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "PRO";
	unsigned char buf[PRO_BUF_LEN];
	char *msg = NULL;

	struct reservation_info r_info;

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

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_FROM_INITIATOR_TO_TARGET;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.dxfer_len       = PRO_BUF_LEN;
	req.dxferp          = buf;
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0){
		if (ret == -EDEV_RESERVATION_CONFLICT && action == PRO_ACT_RESERVE) {
			/* Read reservation information and print */
			memset(&r_info, 0x00, sizeof(r_info));
			f_ret = _fetch_reservation_key(device, &r_info);
			if (!f_ret) {
				ltfsmsg(LTFS_WARN, 30869W, r_info.hint, priv->drive_serial);
				ltfsmsg(LTFS_WARN, 30867W,
						r_info.wwid[0], r_info.wwid[1], r_info.wwid[2], r_info.wwid[3],
						r_info.wwid[6], r_info.wwid[5], r_info.wwid[6], r_info.wwid[7],
						priv->drive_serial);
			} else {
				ltfsmsg(LTFS_WARN, 30869W, "unknown host (reserve command)", priv->drive_serial);
			}
		} else {
			_process_errors(device, ret, msg, cmd_desc, true);
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

/** SCSI command handling of REPORT SUPPORTED OPERATION CODES
 */
static int _cdb_rsoc(void *device, unsigned char *buf, uint32_t len)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB12_LEN];
	unsigned char sense[MAXSENSE];
	int timeout = 60;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "RSOC";
	char *msg = NULL;

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));
	memset(buf, 0, len);
	memset(sense, 0, sizeof(sense));

	/* Build CDB */
	cdb[0] = MAINTENANCE_IN;
	cdb[1] = 0x0C; /* REPORT SUPPORTED OPERATION CODES */
	cdb[2] = 0x80; /* Fetch all commands with RCTD */
	ltfs_u32tobe(cdb + 6, len); /* allocation length */

	/* Build request */
	req.dxfer_direction = SCSI_FROM_TARGET_TO_INITIATOR;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.dxfer_len       = len;
	req.dxferp          = buf;
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0){
		_process_errors(device, ret, msg, cmd_desc, true);
	}

	return ret;
}

/* Global functions */
int iokit_open(const char *devname, void **handle)
{
	char    *end;
	int     drive_type = DRIVE_UNSUPPORTED;
	int     ret = -1, count = 0, i;
	int32_t drive_number;

	struct iokit_data *priv;
	scsi_device_identifier id_data;

	unsigned char *rsoc_buf = NULL;
	uint32_t rsoc_len = RSOC_BUF_SIZE;

	ltfsmsg(LTFS_INFO, 30810I, devname);

	priv = calloc(1, sizeof(struct iokit_data));
	if(!priv) {
		ltfsmsg(LTFS_ERR, 10001E, "iokit_open: private data");
		return -EDEV_NO_MEMORY;
	}

	priv->devname = strdup(devname);
	if (!priv->devname) {
		ltfsmsg(LTFS_ERR, 10001E, "iokit_open: devname");
		free(priv);
		return -EDEV_NO_MEMORY;
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_OPEN));

	errno = 0;
	drive_number = strtoul(devname, &end, 10);
	if(errno || (*end != '\0') || drive_number > 256) {
		/* Find the drive by serial number */
		bool found = false;
		count = iokit_get_ssc_device_count();
		for (i = 0; i < count; i++) {
			ret = iokit_find_ssc_device(&priv->dev, i);
			if(!ret) {
				ret = iokit_get_drive_identifier(&priv->dev, &id_data);
				if (!ret) {
					if (!strncmp(devname, id_data.unit_serial, strlen(devname))) {
						found = true;
						priv->drive_number = i;
						break;
					}
				}
				iokit_free_device(&priv->dev);
			}
		}

		if (!found) {
			ltfsmsg(LTFS_INFO, 30811I, devname);
			ret = -EDEV_DEVICE_UNOPENABLE;
			goto free;
		}
	} else {
		ret = iokit_find_ssc_device(&priv->dev, drive_number);
		if(ret < 0) {
			ret = -EDEV_DEVICE_UNOPENABLE;
			goto free;
		}
		priv->drive_number = drive_number;
	}

	ret = iokit_obtain_exclusive_access(&priv->dev);
	if(ret < 0)
		goto free;

	ret = iokit_get_drive_identifier(&priv->dev, &id_data);
	if(ret < 0) {
		ltfsmsg(LTFS_INFO, 30812I, devname);
		goto free;
	}
	strncpy(priv->drive_serial, id_data.unit_serial, UNIT_SERIAL_LENGTH - 1);

	/* Convert vendor_id in inq buffer to integer based id */
	priv->vendor = get_vendor_id(id_data.vendor_id);

	/* Check the drive is supportable */
	struct supported_device **cur = get_supported_devs(priv->vendor);
	while(cur && *cur) {
		if((! strncmp(id_data.vendor_id, (*cur)->vendor_id, strlen((*cur)->vendor_id)) ) &&
		   (! strncmp(id_data.product_id, (*cur)->product_id, strlen((*cur)->product_id)) ) ) {
			drive_type = (*cur)->drive_type;
			break;
		}
		cur++;
	}

	if(drive_type > 0) {
		if (!drive_has_supported_fw(priv->vendor, drive_type, (unsigned char*)id_data.product_rev)) {
			iokit_release_exclusive_access(&priv->dev);
			ret = -EDEV_UNSUPPORTED_FIRMWARE;
			goto free;
		} else {
			priv->drive_type = drive_type;
		}
	} else {
		ltfsmsg(LTFS_INFO, 30813I, id_data.vendor_id, id_data.product_id);
		iokit_release_exclusive_access(&priv->dev);
		ret = -EDEV_DEVICE_UNSUPPORTABLE; /* Unsupported device */
		goto free;
	}

	ltfsmsg(LTFS_INFO, 30814I, id_data.vendor_id);
	ltfsmsg(LTFS_INFO, 30815I, id_data.product_id);
	ltfsmsg(LTFS_INFO, 30816I, id_data.product_rev);
	ltfsmsg(LTFS_INFO, 30817I, priv->drive_serial);

	snprintf(priv->info.name, TAPE_DEVNAME_LEN_MAX + 1, "%d", drive_number);
	snprintf(priv->info.vendor, TAPE_VENDOR_NAME_LEN_MAX + 1, "%s", id_data.vendor_id);
	snprintf(priv->info.model, TAPE_MODEL_NAME_LEN_MAX + 1, "%s", id_data.product_id);
	snprintf(priv->info.serial_number, TAPE_SERIAL_LEN_MAX + 1, "%s", priv->drive_serial);
	snprintf(priv->info.product_rev, PRODUCT_REV_LENGTH + 1, "%s", id_data.product_rev);
	snprintf(priv->info.product_name, PRODUCT_NAME_LENGTH + 1, "%s", _generate_product_name(id_data.product_id));
	priv->info.host    = 0;
	priv->info.channel = 0;
	priv->info.target  = 0;
	priv->info.lun     = -1;

	/* Setup error table sense to ltfs error code */
	init_error_table(priv->vendor, &standard_table, &vendor_table);

	/* Setup device specific timeout value */
	rsoc_buf = calloc(1, RSOC_BUF_SIZE);
	if (rsoc_buf) {
		ret = _cdb_rsoc(&priv->dev, rsoc_buf, RSOC_BUF_SIZE);
		rsoc_len = ltfs_betou32(rsoc_buf);
		if (!ret && rsoc_len < RSOC_BUF_SIZE) {
			ltfsmsg(LTFS_INFO, 30872I, "RSOC");
			ret = init_timeout_rsoc(&priv->timeouts, rsoc_buf, rsoc_len);
		}

		if (ret < 0) {
			/*
			 * The drive doesn't support RSOC, buffer overrun or parse error
			 * try to initialize the timeout table from drive vendor and drive type
			 */
			ltfsmsg(LTFS_INFO, 30872I, "vendor and device");
			ret = init_timeout(priv->vendor, &priv->timeouts, priv->drive_type);
			if (!priv->timeouts) {
				ltfsmsg(LTFS_INFO, 30872I, "device");
				ibm_tape_init_timeout(&priv->timeouts, priv->drive_type);
			}
		}
		free(rsoc_buf);
	} else {
		/*
		 * Memory allocation failure, try to initialize the timeout table
		 * from drive vendor and drive type
		 */
		ltfsmsg(LTFS_INFO, 30872I, "vendor and device");
		init_timeout(priv->vendor, &priv->timeouts, priv->drive_type);
		if (!priv->timeouts) {
			ltfsmsg(LTFS_INFO, 30872I, "device");
			ibm_tape_init_timeout(&priv->timeouts, priv->drive_type);
		}
	}

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

int iokit_reopen(const char *devname, void *device)
{
	int     drive_type = DRIVE_UNSUPPORTED;
	int     ret = -EDEV_UNKNOWN;

	CHECK_ARG_NULL(device, -LTFS_NULL_ARG);

	struct iokit_data *priv = (struct iokit_data*)device;
	scsi_device_identifier id_data;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_REOPEN));

	ltfsmsg(LTFS_INFO, 30818I, devname);

	ret = iokit_find_ssc_device(&priv->dev, priv->drive_number);
	if(ret < 0){
		ret = -EDEV_DEVICE_UNOPENABLE;
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_REOPEN));
		return ret;
	}

	ret = iokit_obtain_exclusive_access(&priv->dev);
	if(ret < 0) {
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_REOPEN));
		return ret;
	}

	ret = iokit_get_drive_identifier(&priv->dev, &id_data);
	if(ret < 0) {
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_REOPEN));
		return ret;
	}
	strncpy(priv->drive_serial, id_data.unit_serial, UNIT_SERIAL_LENGTH - 1);

	/* Check the drive is supportable */
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
			iokit_release_exclusive_access(&priv->dev);
			ret = -EDEV_UNSUPPORTED_FIRMWARE;
		} else
			priv->drive_type = drive_type;
	} else {
		ltfsmsg(LTFS_INFO, 30813I, id_data.vendor_id, id_data.product_id);
		iokit_release_exclusive_access(&priv->dev);
		ret = -EDEV_DEVICE_UNSUPPORTABLE; /* Unsupported device */
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_REOPEN));
	return ret;
}

int iokit_close(void *device)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_CLOSE));

	_set_lbp(device, false);
	_register_key(device, NULL);

	if(priv->dev.exclusive_lock)
		ret = iokit_release_exclusive_access(&priv->dev);

	ret = iokit_free_device(&priv->dev);

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

int iokit_close_raw(void *device)
{
	int ret = 0;
	struct iokit_data *priv = (struct iokit_data*)device;

	/* This operation is called only after resource is forked. */
	/* On OSX environment, this operation is not required      */
	/* because file discripter is not inherited.               */
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_CLOSERAW));
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_CLOSERAW));
	return ret;
}

int iokit_is_connected(const char *devname)
{
	/*
	 * Temporary return false here.
	 * Current iokit driver uses index number as a devname and this
	 * index may be changed by drive hotplug.
	 * However LTFS's library code is assuming fixed devname
	 * during running LTFS even though drive plug/unplug.
	 */
	return false;
}

int iokit_inquiry_page(void *device, unsigned char page, struct tc_inq_page *inq)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "INQUIRY";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_INQUIRYPAGE));
	ltfsmsg(LTFS_DEBUG, 30993D, "inquiry", page, priv->drive_serial);

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	/* Build CDB */
	cdb[0] = INQUIRY;
	if(page)
		cdb[1] = 0x01;
	cdb[2] = page;
	ltfs_u16tobe(cdb + 3, sizeof(inq->data));

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_FROM_TARGET_TO_INITIATOR;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.dxfer_len       = sizeof(inq->data);
	req.dxferp          = inq->data;
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0){
		_process_errors(device, ret, msg, cmd_desc, true);
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_INQUIRYPAGE));

	return ret;
}

int iokit_inquiry(void *device, struct tc_inq *inq)
{
	int ret = -EDEV_UNKNOWN;
	int vendor_length = 0;
	struct iokit_data *priv = (struct iokit_data*)device;
	struct tc_inq_page inq_page;

	ret = iokit_inquiry_page(device, 0x00, &inq_page);
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

int iokit_test_unit_ready(void *device)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "TEST_UNIT_READY";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_TUR));
	ltfsmsg(LTFS_DEBUG3, 30992D, "test unit ready", priv->drive_serial);

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	/* Build CDB */
	cdb[0] = TEST_UNIT_READY;

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_NO_DATA_TRANSFER;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
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

		if (print_msg) {
			_process_errors(device, ret, msg, cmd_desc, take_dump);
		}
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_TUR));

	return ret;
}

static int _cdb_read(void *device, char *buf, size_t size, boolean_t sili)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "READ";
	char *msg = NULL;
	size_t length = -EDEV_UNKNOWN;

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	cdb[0] = READ;
	if(sili && priv->use_sili)
		cdb[1] = 0x02;
	cdb[2] = (size >> 16) & 0xFF;
	cdb[3] = (size >> 8)  & 0xFF;
	cdb[4] =  size        & 0xFF;

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_FROM_TARGET_TO_INITIATOR;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.dxfer_len       = size;
	req.dxferp          = (unsigned char*)buf;
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0){
		int32_t diff_len = 0;
		SCSI_Sense_Data *sense_data = (SCSI_Sense_Data *)(&req.sense_buffer);

		switch (ret) {
			case DEVICE_GOOD:
			case -EDEV_NO_SENSE:
				if (sense_data->SENSE_KEY & kSENSE_ILI_Set) {
					diff_len = ((int32_t)sense_data->INFORMATION_1 << 24) +
						((int32_t)sense_data->INFORMATION_2 << 16) +
						((int32_t)sense_data->INFORMATION_3 << 8) +
						((int32_t)sense_data->INFORMATION_4);

					if (!req.actual_xfered || diff_len != req.resid) {
#if SUPPORT_BUGGY_IFS
						/*
						 * A few I/Fs, like thunderbolt/SAS converter or USB/SAS converter,
						 * cannot handle actual transfer length and residual length correctly
						 * In this case, LTFS will trust SCSI sense.
						 */
						if (diff_len < 0) {
							ltfsmsg(LTFS_INFO, 30820I, diff_len, (int)(size - diff_len)); // "Detect overrun condition"
							ret = -EDEV_OVERRUN;
						} else {
							ltfsmsg(LTFS_DEBUG, 30821D, diff_len, (int)(size - diff_len)); // "Detect underrun condition"
							length = size - diff_len;
							ret = DEVICE_GOOD;
						}
#else
						ltfsmsg(LTFS_WARN, 30819W, req.actual_xfered, req.resid, diff_len);
						return -EDEV_LENGTH_MISMATCH;
#endif
					} else {
						if (diff_len < 0) {
							ltfsmsg(LTFS_INFO, 30820I, diff_len, (int)(size - diff_len)); // "Detect overrun condition"
							ret = -EDEV_OVERRUN;
						} else {
							ltfsmsg(LTFS_DEBUG, 30821D, diff_len, (int)(size - diff_len)); // "Detect underrun condition"
							length = size - diff_len;
							ret = DEVICE_GOOD;
						}
					}
				} else if (sense_data->SENSE_KEY & kSENSE_FILEMARK_Set) {
					ltfsmsg(LTFS_DEBUG, 30822D);
					ret = -EDEV_FILEMARK_DETECTED;
					length = -EDEV_FILEMARK_DETECTED;
				}
				break;
			case -EDEV_FILEMARK_DETECTED:
				ltfsmsg(LTFS_DEBUG, 30822D);
				ret = -EDEV_FILEMARK_DETECTED;
				length = -EDEV_FILEMARK_DETECTED;
				break;
			case -EDEV_CLEANING_REQUIRED:
				ltfsmsg(LTFS_INFO, 30823I);
				length = 0;
				ret = DEVICE_GOOD;
				break;
		}

		if (ret != DEVICE_GOOD && ret != -EDEV_FILEMARK_DETECTED) {
			if ((ret != -EDEV_CRYPTO_ERROR && ret != -EDEV_KEY_REQUIRED) || priv->dev.is_data_key_set)
				_process_errors(device, ret, msg, cmd_desc, true);
			length = ret;
		}
	} else {
		/* check condition is not set so we have a good read and can trust the length value */
		length = req.actual_xfered;
	}

	return length;
}

int iokit_read(void *device, char *buf, size_t size,
					   struct tc_position *pos, const bool unusual_size)
{
	int32_t ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;
	size_t datacount = size;
	struct tc_position pos_retry = {0, 0};

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_READ));
	ltfsmsg(LTFS_DEBUG3, 30995D, "read", size, priv->drive_serial);

	if (priv->force_readperm) {
		priv->read_counter++;
		if (priv->read_counter > priv->force_readperm) {
			ltfsmsg(LTFS_INFO, 30846I, "read");
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READ));
			if (priv->force_errortype)
				return -EDEV_NO_SENSE;
			else
				return -EDEV_READ_PERM;
		}
	}

	if (global_data.crc_checking) {
		datacount = size + 4;
		/* Never fall into this block, fail safe to adjust record length*/
		if (datacount > IOKIT_MAX_BLOCK_SIZE)
			datacount = IOKIT_MAX_BLOCK_SIZE;
	}

read_retry:
	ret = _cdb_read(device, buf, datacount, unusual_size);
	if (ret == -EDEV_LENGTH_MISMATCH) {
		if (pos_retry.partition || pos_retry.block) {
			/* Return error when retry is already executed */
			iokit_readpos(device, pos);
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READ));
			return ret;
		}
		pos_retry.partition = pos->partition;
		pos_retry.block     = pos->block;
		ret = iokit_locate(device, pos_retry, pos);
		if (ret) {
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READ));
			return ret;
		}
		goto read_retry;
	} else if ( !(pos->block) && unusual_size &&
				(ret == (int32_t)size || ret == -EDEV_FILEMARK_DETECTED) ) {
		/*
		 *  Try to read again without sili bit, because some I/F doesn't support SILION read correctly
		 *  like
		 *    - USB connected LTO drive (ret == size)
		 *    - ATTO ThunderLink        (ret == -EDEV_FILEMARK_DETECTED)
		 *
		 *  This recovery procedure is executed only when reading VOL1 on both partiton. Once this memod
		 *  is completed successfully, the iokit backend uses SILI off read always.
		 */
		pos_retry.partition = pos->partition;
		ret = iokit_locate(device, pos_retry, pos);
		if (ret) {
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READ));
			return ret;
		}
		priv->use_sili = false;
		ret = _cdb_read(device, buf, datacount, unusual_size);
	}

	if (ret == -EDEV_FILEMARK_DETECTED) {
		pos->filemarks++;
		ret = DEVICE_GOOD;
	}

	if (ret >= 0) {
		pos->block++;
		if (global_data.crc_checking && ret > 4) {
			if (priv->f_crc_check)
				ret = priv->f_crc_check(buf, ret - 4);
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, 30824E);
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
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "WRITE";
	char *msg = NULL;

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	cdb[0] = WRITE;
	cdb[1] = 0x00; /* Always variable in LTFS */
	cdb[2] = (size >> 16) & 0xFF;
	cdb[3] = (size >> 8)  & 0xFF;
	cdb[4] =  size        & 0xFF;

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_FROM_INITIATOR_TO_TARGET;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.dxfer_len       = size;
	req.dxferp          = (unsigned char*)buf;
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	*ew = false;
	*pew = false;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0){
		switch (ret) {
			case -EDEV_EARLY_WARNING:
				ltfsmsg(LTFS_WARN, 30825W, "write");
				*ew = true;
				*pew = true;
				ret = DEVICE_GOOD;
				break;
			case -EDEV_PROG_EARLY_WARNING:
				ltfsmsg(LTFS_WARN, 30826W, "write");
				*pew = true;
				ret = DEVICE_GOOD;
				break;
			case -EDEV_CLEANING_REQUIRED:
				ltfsmsg(LTFS_INFO, 30823I);
				ret = DEVICE_GOOD;
				break;
			default:
				break;
		}

		if (ret < 0) {
			_process_errors(device, ret, msg, cmd_desc, true);
		}
	}

	return ret;
}

int iokit_write(void *device, const char *buf, size_t count, struct tc_position *pos)
{
	int ret;
	bool ew = false, pew = false;
	struct iokit_data *priv = (struct iokit_data*)device;
	size_t datacount = count;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_WRITE));

	ltfsmsg(LTFS_DEBUG3, 30995D, "write", count, priv->drive_serial);

	if ( priv->force_writeperm ) {
		priv->write_counter++;
		if ( priv->write_counter > priv->force_writeperm ) {
			ltfsmsg(LTFS_INFO, 30846I, "write");
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_WRITE));
			if (priv->force_errortype)
				return -EDEV_NO_SENSE;
			else
				return -EDEV_WRITE_PERM;
		} else if ( priv->write_counter > (priv->force_writeperm - THRESHOLD_FORCE_WRITE_NO_WRITE) ) {
			ltfsmsg(LTFS_INFO, 30847I);
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

	ret = _cdb_write(device, (uint8_t *)buf, datacount, &ew, &pew);
	if(ret == DEVICE_GOOD) {
		pos->block++;
		pos->early_warning = ew;
		pos->programmable_early_warning = pew;
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_WRITE));

	return ret;
}

int iokit_writefm(void *device, size_t count, struct tc_position *pos, bool immed)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "WRITEFM";
	char *msg = NULL;

	bool ew = false, pew = false;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_WRITEFM));
	ltfsmsg(LTFS_DEBUG, 30994D, "write file marks", count, priv->drive_serial);

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	cdb[0] = WRITE_FILEMARKS6;
	if (immed)
		cdb[1] = 0x01;
	cdb[2] = (count >> 16) & 0xFF;
	cdb[3] = (count >> 8)  & 0xFF;
	cdb[4] =  count        & 0xFF;

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_NO_DATA_TRANSFER;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0){
		switch (ret) {
			case -EDEV_EARLY_WARNING:
				ltfsmsg(LTFS_WARN, 30825W, "write filemarks");
				ew = true;
				pew = true;
				ret = DEVICE_GOOD;
				break;
			case -EDEV_PROG_EARLY_WARNING:
				ltfsmsg(LTFS_WARN, 30826W, "write filemarks");
				pew = true;
				ret = DEVICE_GOOD;
				break;
			case -EDEV_CLEANING_REQUIRED:
				ltfsmsg(LTFS_INFO, 30823I);
				ret = DEVICE_GOOD;
				break;
			default:
				break;
		}

		if (ret < 0) {
			_process_errors(device, ret, msg, cmd_desc, true);
		}
	}

	if(ret == DEVICE_GOOD) {
		ret = iokit_readpos(device, pos);
		if(ret == DEVICE_GOOD) {
			if (ew && !pos->early_warning)
				pos->early_warning = ew;
			if (pew && !pos->programmable_early_warning)
				pos->programmable_early_warning = pew;
		}
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_WRITEFM));

	return ret;
}

int iokit_rewind(void *device, struct tc_position *pos)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "REWIND";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_REWIND));
	ltfsmsg(LTFS_DEBUG, 30997D, "rewind", (unsigned long long)0, (unsigned long long)0, priv->drive_serial);

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	cdb[0] = REWIND;

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_NO_DATA_TRANSFER;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0){
		_process_errors(device, ret, msg, cmd_desc, true);
	}

	if(ret == DEVICE_GOOD) {
		/* Clear force perm setting */
		priv->clear_by_pc     = false;
		priv->force_writeperm = DEFAULT_WRITEPERM;
		priv->force_readperm  = DEFAULT_READPERM;
		priv->write_counter   = 0;
		priv->read_counter    = 0;

		ret = iokit_readpos(device, pos);

		if(ret == DEVICE_GOOD) {
			if(pos->early_warning)
				ltfsmsg(LTFS_WARN, 30825W, "rewind");
			else if(pos->programmable_early_warning)
				ltfsmsg(LTFS_WARN, 30826W, "rewind");
		}
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_REWIND));

	return ret;
}

int iokit_locate(void *device, struct tc_position dest, struct tc_position *pos)
{
	int ret = -EDEV_UNKNOWN;
	int ret_rp = DEVICE_GOOD;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB16_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "LOCATE";
	char *msg = NULL;
	bool pc = false;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_LOCATE));
	ltfsmsg(LTFS_DEBUG, 30997D, "locate", (unsigned long long)dest.partition, dest.block, priv->drive_serial);

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
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	cdb[0]  = LOCATE16;
	if (pc)
		cdb[1]  = 0x02; /* Set Change partition(CP) flag */
	cdb[3]  = (unsigned char)(dest.partition & 0xff);
	ltfs_u64tobe(cdb + 4, dest.block);

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_NO_DATA_TRANSFER;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0){
		if (dest.block == TAPE_BLOCK_MAX && ret == -EDEV_EOD_DETECTED) {
			ltfsmsg(LTFS_DEBUG, 30827D, "Locate");
			ret = DEVICE_GOOD;
		} else {
			_process_errors(device, ret, msg, cmd_desc, true);
		}
	}

	ret_rp = iokit_readpos(device, pos);
	if(ret_rp == DEVICE_GOOD) {
		if(pos->early_warning)
			ltfsmsg(LTFS_WARN, 30825W, "locate");
		else if(pos->programmable_early_warning)
			ltfsmsg(LTFS_WARN, 30826W, "locate");
	} else {
		if (!ret)
			ret = ret_rp;
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_LOCATE));

	return ret;
}

int iokit_space(void *device, size_t count, TC_SPACE_TYPE type, struct tc_position *pos)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB16_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "SPACE";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_SPACE));


	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	cdb[0] = SPACE16;
	switch(type) {
		case TC_SPACE_EOD:
			ltfsmsg(LTFS_DEBUG, 30992D, "space to EOD", priv->drive_serial);
			cdb[1] = 0x03;
			break;
		case TC_SPACE_FM_F:
			ltfsmsg(LTFS_DEBUG, 30996D, "space forward file marks", (unsigned long long)count,
					priv->drive_serial);
			cdb[1] = 0x01;
			ltfs_u64tobe(cdb + 4, count);
			break;
		case TC_SPACE_FM_B:
			ltfsmsg(LTFS_DEBUG, 30996D, "space back file marks", (unsigned long long)count,
					priv->drive_serial);
			cdb[1] = 0x01;
			ltfs_u64tobe(cdb + 4, -count);
			break;
		case TC_SPACE_F:
			ltfsmsg(LTFS_DEBUG, 30996D, "space forward records", (unsigned long long)count,
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
			ltfsmsg(LTFS_INFO, 30828I);
			ret = -EDEV_INVALID_ARG;
			break;
	}

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_NO_DATA_TRANSFER;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0){
		_process_errors(device, ret, msg, cmd_desc, true);
	}

	if(ret == DEVICE_GOOD)
		ret = iokit_readpos(device, pos);

	if(ret == DEVICE_GOOD) {
		if(pos->early_warning)
			ltfsmsg(LTFS_WARN, 30825W, "space");
		else if(pos->programmable_early_warning)
			ltfsmsg(LTFS_WARN, 30826W, "space");
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SPACE));

	return ret;
}

static int _cdb_request_sense(void *device, unsigned char *buf, unsigned char size)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "REQUEST_SENSE";
	char *msg = NULL;

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	cdb[0] = REQUEST_SENSE;
	cdb[4] = (unsigned char)size;

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_FROM_TARGET_TO_INITIATOR;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.dxfer_len       = size;
	req.dxferp          = buf;
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0) {
		_process_errors(device, ret, msg, cmd_desc, true);
	}

	return ret;
}

int iokit_erase(void *device, struct tc_position *pos, bool long_erase)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "ERASE";
	char *msg = NULL;
	struct ltfs_timespec ts_start, ts_now;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_ERASE));
	if (long_erase)
		ltfsmsg(LTFS_DEBUG, 30992D, "long erase", priv->drive_serial);
	else
		ltfsmsg(LTFS_DEBUG, 30992D, "short erase", priv->drive_serial);

	get_current_timespec(&ts_start);

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	cdb[0] = ERASE;
	if (long_erase)
		cdb[1] = 0x03;

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_NO_DATA_TRANSFER;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);

	if (long_erase) {
		unsigned char sense_buf[sizeof(SCSI_Sense_Data)];
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
				ltfsmsg(LTFS_INFO, 30829I, (int)((ts_now.tv_sec - ts_start.tv_sec)/60));
			} else {
				progress = ((uint32_t) sense_buf[16] & 0xFF) << 8;
				progress += ((uint32_t) sense_buf[17] & 0xFF);
				ltfsmsg(LTFS_INFO, 30830I, (progress*100/0xFFFF));
			}

			sleep(60);
		}
	}

	if (ret < 0){
		_process_errors(device, ret, msg, cmd_desc, true);
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_ERASE));

	return ret;
}

static int _cdb_load_unload(void *device, bool load)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "LOAD_UNLOAD";
	char *msg = NULL;

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	cdb[0] = LOAD_UNLOAD;
	if (load)
		cdb[4] = 0x01;

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_NO_DATA_TRANSFER;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);

	/* Clear force perm setting */
	priv->clear_by_pc     = false;
	priv->force_writeperm = DEFAULT_WRITEPERM;
	priv->force_readperm  = DEFAULT_READPERM;
	priv->write_counter   = 0;
	priv->read_counter    = 0;

	if (ret < 0){
		_process_errors(device, ret, msg, cmd_desc, true);
	}

	return ret;
}

int iokit_load(void *device, struct tc_position *pos)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;
	unsigned char buf[TC_MP_SUPPORTEDPAGE_SIZE];

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_LOAD));
	ltfsmsg(LTFS_DEBUG, 30992D, "load", priv->drive_serial);

	ret = _cdb_load_unload(device, true);

	/* Clear force perm setting */
	priv->clear_by_pc     = false;
	priv->force_writeperm = DEFAULT_WRITEPERM;
	priv->force_readperm  = DEFAULT_READPERM;
	priv->write_counter   = 0;
	priv->read_counter    = 0;

	iokit_readpos(device, pos);
	if (ret < 0) {
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_LOAD));
		return ret;
	} else {
		if(ret == DEVICE_GOOD) {
			if(pos->early_warning)
				ltfsmsg(LTFS_WARN, 30825W, "load");
			else if(pos->programmable_early_warning)
				ltfsmsg(LTFS_WARN, 30826W, "load");
		}

		priv->loaded = true;
	}

	priv->tape_alert = 0;

	/* Check Cartridge type */
	ret = iokit_modesense(device, TC_MP_SUPPORTEDPAGE, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
	if (ret < 0) {
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_LOAD));
		return ret;
	}

	priv->density_code = buf[8];

	if (priv->vendor == VENDOR_HP) {
		priv->cart_type = assume_cart_type(priv->density_code);
		if (buf[2] == 0x01)
			priv->is_worm = true;
	} else {
		priv->cart_type = buf[2];
	}

	ret = is_supported_tape(priv->cart_type, priv->density_code, &(priv->is_worm));
	if(ret == -LTFS_UNSUPPORTED_MEDIUM)
		ltfsmsg(LTFS_INFO, 30831I, priv->cart_type, priv->density_code);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_LOAD));

	return ret;
}

int iokit_unload(void *device, struct tc_position *pos)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_UNLOAD));
	ltfsmsg(LTFS_DEBUG, 30992D, "unload", priv->drive_serial);

	ret = _cdb_load_unload(device, false);
	if (ret < 0) {
		/* TODO: Read position here */
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

int iokit_readpos(void *device, struct tc_position *pos)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB10_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "READPOS";
	char *msg = NULL;
	unsigned char buf[REDPOS_LONG_LEN];

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_READPOS));

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	cdb[0] = READ_POSITION;
	cdb[1] = 0x06; /* Long Format */

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_FROM_TARGET_TO_INITIATOR;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.dxfer_len       = sizeof(buf);
	req.dxferp          = buf;
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret == DEVICE_GOOD) {
		pos->partition = ltfs_betou32(buf + 4);
		pos->block     = ltfs_betou64(buf + 8);
		pos->filemarks = ltfs_betou64(buf + 16);
		pos->early_warning = buf[0] & 0x40;
		pos->programmable_early_warning = buf[0] & 0x01;

		ltfsmsg(LTFS_DEBUG, 30998D, "readpos", (unsigned long long)pos->partition,
				(unsigned long long)pos->block, (unsigned long long)pos->filemarks,
				priv->drive_serial);
	} else {
		_process_errors(device, ret, msg, cmd_desc, true);
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READPOS));

	return ret;
}

int iokit_setcap(void *device, uint16_t proportion)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "SETCAP";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_SETCAP));
	ltfsmsg(LTFS_DEBUG, 30993D, "setcap", proportion, priv->drive_serial);

	if (IS_ENTERPRISE(priv->drive_type)) {
		unsigned char buf[TC_MP_MEDIUM_SENSE_SIZE];

		/* scale media instead of setcap */
		ret = iokit_modesense(device, TC_MP_MEDIUM_SENSE, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
		if (ret < 0) {
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETCAP));
			return ret;
		}

		if (IS_SHORT_MEDIUM(buf[2]) || IS_WORM_MEDIUM(buf[2])) {
			/* JK media cannot be scaled */
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETCAP));
			return ret;
		}

		buf[0]   = 0x00;
		buf[1]   = 0x00;
		buf[27] |= 0x01;
		buf[28]  = 0x00;

		ret = iokit_modeselect(device, buf, sizeof(buf));
	} else {
		/* Zero out the CDB and the result buffer */
		memset(cdb, 0, sizeof(cdb));
		memset(&req, 0, sizeof(struct iokit_scsi_request));

		cdb[0] = SET_CAPACITY;
		ltfs_u16tobe(cdb + 3, proportion);

		timeout = get_timeout(priv->timeouts, cdb[0]);
		if (timeout < 0)
			return -EDEV_UNSUPPORETD_COMMAND;

		/* Build request */
		req.dxfer_direction = SCSI_NO_DATA_TRANSFER;
		req.cmd_len         = sizeof(cdb);
		req.mx_sb_len       = sizeof(SCSI_Sense_Data);
		req.cmdp            = cdb;
		memset(&req.sense_buffer, 0, req.mx_sb_len);
		req.timeout         = IOKitConversion(timeout);
		req.desc            = cmd_desc;

		ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
		if (ret < 0) {
			_process_errors(device, ret, msg, cmd_desc, true);
		}
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETCAP));

	return ret;
}

int iokit_format(void *device, TC_FORMAT_TYPE format, const char *vol_name, const char *barcode_name, const char *vol_mam_uuid)
{
	int ret = -EDEV_UNKNOWN, aux_ret;
	struct iokit_data *priv = (struct iokit_data*)device;
	unsigned char buf[TC_MP_SUPPORTEDPAGE_SIZE];

	struct iokit_scsi_request req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "FORMAT";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_FORMAT));
	ltfsmsg(LTFS_DEBUG, 30992D, "format", priv->drive_serial);

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	cdb[0] = FORMAT_MEDIUM;
	cdb[2] = format;

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_NO_DATA_TRANSFER;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0) {
		_process_errors(device, ret, msg, cmd_desc, true);
	}

	/* Check Cartridge type */
	aux_ret = iokit_modesense(device, TC_MP_SUPPORTEDPAGE, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
	if (aux_ret >= 0) {
		priv->cart_type = buf[2];
		priv->density_code = buf[8];
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_FORMAT));

	return ret;
}

int iokit_remaining_capacity(void *device, struct tc_remaining_cap *cap)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	unsigned char buffer[LOGSENSEPAGE];       /* Buffer for logsense */
	unsigned char buf[LOG_TAPECAPACITY_SIZE]; /* Buffer for parsing logsense data */
	uint32_t param_size;
	int32_t i;
	uint32_t logcap;
	int offset, length;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_REMAINCAP));

	memset(&buffer, 0, LOGSENSEPAGE);

	if (IS_LTO(priv->drive_type) && (DRIVE_GEN(priv->drive_type) == 0x05)) {
		/* Use LogPage 0x31 */
		ret = iokit_logsense(device, (uint8_t)LOG_TAPECAPACITY, (uint8_t)0, (void *)buffer, LOGSENSEPAGE);
		if(ret < 0)
		{
			ltfsmsg(LTFS_INFO, 30832I, LOG_VOLUMESTATS, ret);
			goto out;
		}

		for( i = TAPECAP_REMAIN_0; i < TAPECAP_SIZE; i++)
		{
			ret = _parse_logPage(buffer, (uint16_t)i, &param_size, buf, sizeof(buf));
			if(ret < 0 || param_size != sizeof(uint32_t))
			{
				ltfsmsg(LTFS_INFO, 30833I, i, param_size);
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
					ltfsmsg(LTFS_INFO, 30834I, i);
					ret = -EDEV_INTERNAL_ERROR;
					goto out;
					break;
			}
		}
		ret = DEVICE_GOOD;
	} else {
		/* Use LogPage 0x17 */
		ret = iokit_logsense(device, LOG_VOLUMESTATS, (uint8_t)0, (void *)buffer, LOGSENSEPAGE);
		if(ret < 0) {
			ltfsmsg(LTFS_INFO, 30832I, LOG_VOLUMESTATS, ret);
			goto out;
		}

		/* Capture Total Cap */
		ret = _parse_logPage(buffer, (uint16_t)VOLSTATS_PARTITION_CAP, &param_size, buf, sizeof(buf));
		if (ret < 0) {
			ltfsmsg(LTFS_INFO, 30835I);
			goto out;
		}

		memset(cap, 0, sizeof(struct tc_remaining_cap));

		cap->max_p0 = ltfs_betou32(&buf[PARTITIOIN_REC_HEADER_LEN]);
		offset = (int)buf[0] + 1;
		length = (int)buf[offset] + 1;

		if ((uint32_t)(offset + length) <= param_size) {
			cap->max_p1 = ltfs_betou32(&buf[offset + PARTITIOIN_REC_HEADER_LEN]);
		}

		/* Capture Remaining Cap  */
		ret = _parse_logPage(buffer, (uint16_t)VOLSTATS_PART_REMAIN_CAP, &param_size, buf, sizeof(buf));
		if (ret < 0) {
			ltfsmsg(LTFS_INFO, 30835I);
			goto out;
		}

		cap->remaining_p0 = ltfs_betou32(&buf[PARTITIOIN_REC_HEADER_LEN]);
		offset = (int)buf[0] + 1;
		length = (int)buf[offset] + 1;

		if ((uint32_t)(offset + length) <= param_size) {
			cap->remaining_p1 = ltfs_betou32(&buf[offset + PARTITIOIN_REC_HEADER_LEN]);
		}

		/* Convert MB to MiB -- Need to consider about overflow when max cap reaches to 18EB */
		cap->max_p0 = (cap->max_p0 * 1000 * 1000) >> 20;
		cap->max_p1 = (cap->max_p1 * 1000 * 1000) >> 20;
		cap->remaining_p0 = (cap->remaining_p0 * 1000 * 1000) >> 20;
		cap->remaining_p1 = (cap->remaining_p1 * 1000 * 1000) >> 20;

		ret = DEVICE_GOOD;
	}

	ltfsmsg(LTFS_DEBUG3, 30997D, "capacity part0", (unsigned long long)cap->remaining_p0,
			(unsigned long long)cap->max_p0, priv->drive_serial);
	ltfsmsg(LTFS_DEBUG3, 30997D, "capacity part1", (unsigned long long)cap->remaining_p1,
			(unsigned long long)cap->max_p1, priv->drive_serial);

out:
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_REMAINCAP));
	return ret;
}

int iokit_logsense(void *device, const uint8_t page, const uint8_t subpage,
				   unsigned char *buf, const size_t size)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB10_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "LOGSENSE";
	char *msg = NULL;

	unsigned int len = 0;
	unsigned char *inner_buf = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_LOGSENSE));
	ltfsmsg(LTFS_DEBUG3, 30997D, "logsense",
			(unsigned long long)page, (unsigned long long)subpage, priv->drive_serial);

	inner_buf = calloc(1, MAXLP_SIZE); /* Assume max length of LP is 0xFFFF */
	if (!inner_buf)
		return -LTFS_NO_MEMORY;

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	/* Build CDB */
	cdb[0] = LOG_SENSE;
	cdb[2] = 0x40 | (page & 0x3F); /* Current value */
	cdb[3] = subpage;
	ltfs_u16tobe(cdb + 7, MAXLP_SIZE);

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0) {
		free(inner_buf);
		return -EDEV_UNSUPPORETD_COMMAND;
	}

	/* Build request */
	req.dxfer_direction = SCSI_FROM_TARGET_TO_INITIATOR;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.dxfer_len       = MAXLP_SIZE;
	req.dxferp          = inner_buf;
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0){
		_process_errors(device, ret, msg, cmd_desc, true);
	} else {
		len = ((int)inner_buf[2] << 8) + (int)inner_buf[3] + 4;

		if (size > len)
			memcpy(buf, inner_buf, len);
		else
			memcpy(buf, inner_buf, size);

		ret = len;
	}

	free (inner_buf);
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_LOGSENSE));

	return ret;
}

int iokit_modesense(void *device, const unsigned char page, const TC_MP_PC_TYPE pc,
							const unsigned char subpage, unsigned char *buf, const size_t size)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB10_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "MODESENSE";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_MODESENSE));
	ltfsmsg(LTFS_DEBUG3, 30993D, "modesense", page, priv->drive_serial);

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	/* Build CDB */
	cdb[0] = MODE_SENSE10;
	cdb[2] = pc | (page & 0x3F); /* Current value */
	cdb[3] = subpage;
	ltfs_u16tobe(cdb + 7, size);

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_FROM_TARGET_TO_INITIATOR;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.dxfer_len       = size;
	req.dxferp          = buf;
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0){
		_process_errors(device, ret, msg, cmd_desc, true);
	} else {
		ret = req.actual_xfered;
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_MODESENSE));

	return ret;
}

int iokit_modeselect(void *device, unsigned char *buf, const size_t size)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB10_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "MODESELECT";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_MODESELECT));
	ltfsmsg(LTFS_DEBUG3, 30992D, "modeselect", priv->drive_serial);

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	/* Build CDB */
	cdb[0] = MODE_SELECT10;
	cdb[1] = 0x10; /* Set PF bit */
	ltfs_u16tobe(cdb + 7, size);

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_FROM_INITIATOR_TO_TARGET;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.dxfer_len       = size;
	req.dxferp          = buf;
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0){
		_process_errors(device, ret, msg, cmd_desc, true);
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_MODESELECT));

	return ret;
}

int iokit_reserve(void *device)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

#ifdef USE_RESERVE6
	struct iokit_scsi_request req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "RESERVE6";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_RESERVEUNIT));
	ltfsmsg(LTFS_DEBUG, 30992D, "reserve unit (6)", priv->drive_serial);

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	/* TODO: Need to use Persistent Reserve */
	cdb[0] = RESERVE_UNIT6;

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_NO_DATA_TRANSFER;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0){
		_process_errors(device, ret, msg, cmd_desc, true);
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_RESERVEUNIT));

#else /* Use persistent reserve */

	int count = 0;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_RESERVEUNIT));
	ltfsmsg(LTFS_DEBUG, 30992D, "reserve (PRO)", priv->drive_serial);

start:
	ret = _cdb_pro(device, PRO_ACT_RESERVE, PRO_TYPE_EXCLUSIVE,
				   priv->key, NULL);

	/* Retry if reservation is preempted */
	if ( !count &&
		 ( ret == -EDEV_RESERVATION_PREEMPTED ||
		   ret == -EDEV_REGISTRATION_PREEMPTED ||
		   ret == -EDEV_RESERVATION_CONFLICT)
		) {
		ltfsmsg(LTFS_INFO, 30868I, priv->drive_serial);
		_register_key(device, priv->key);
		count++;
		goto start;
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_RESERVEUNIT));

#endif

	return ret;
}

int iokit_release(void *device)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

#ifdef USE_RESERVE6
	struct iokit_scsi_request req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "RELEASE6";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_RELEASEUNIT));
	ltfsmsg(LTFS_DEBUG, 30992D, "release unit (6)", priv->drive_serial);

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	/* TODO: Need to use Persistent Reserve */
	cdb[0] = RELEASE_UNIT6;

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_NO_DATA_TRANSFER;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0){
		_process_errors(device, ret, msg, cmd_desc, true);
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_RELEASEUNIT));

#else /* Use persistent reserve */

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_RELEASEUNIT));
	ltfsmsg(LTFS_DEBUG, 30992D, "release (PRO)", priv->drive_serial);

	ret = _cdb_pro(device, PRO_ACT_RELEASE, PRO_TYPE_EXCLUSIVE,
				   priv->key, NULL);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_RELEASEUNIT));

#endif

	return ret;
}

static int _cdb_prevent_allow_medium_removal(void *device, bool prevent)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "PREVENT/ALLOW_MEDIUM_REMOVAL";
	char *msg = NULL;

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	cdb[0] = PREVENT_ALLOW_MEDIUM_REMOVAL;
	if (prevent)
		cdb[4] = 0x01;

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_NO_DATA_TRANSFER;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0){
		_process_errors(device, ret, msg, cmd_desc, true);
	}

	return ret;
}

int iokit_prevent_medium_removal(void *device)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_PREVENTM));
	ltfsmsg(LTFS_DEBUG, 30992D, "prevent medium removal", priv->drive_serial);
	ret = _cdb_prevent_allow_medium_removal(device, true);
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_PREVENTM));

	return ret;
}

int iokit_allow_medium_removal(void *device)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_ALLOWMREM));
	ltfsmsg(LTFS_DEBUG, 30992D, "allow medium removal", priv->drive_serial);
	ret = _cdb_prevent_allow_medium_removal(device, false);
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_ALLOWMREM));

	return ret;
}

int iokit_write_attribute(void *device, const tape_partition_t part,
								  const unsigned char *buf, const size_t size)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB16_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "WRITE_ATTR";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_WRITEATTR));
	ltfsmsg(LTFS_DEBUG3, 30996D, "writeattr", (unsigned long long)part,
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
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	/* Build CDB */
	cdb[0] = WRITE_ATTRIBUTE;
	cdb[1] = 0x01; /* Write through bit on */
	cdb[7] = part;
	ltfs_u32tobe(cdb + 10, len);

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0) {
		free(buffer);
		return -EDEV_UNSUPPORETD_COMMAND;
	}

	/* Build request */
	req.dxfer_direction = SCSI_FROM_INITIATOR_TO_TARGET;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.dxfer_len       = len;
	req.dxferp          = buffer;
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0){
		_process_errors(device, ret, msg, cmd_desc, true);
	}

	free(buffer);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_WRITEATTR));

	return ret;
}

int iokit_read_attribute(void *device, const tape_partition_t part,
								 const uint16_t id, unsigned char *buf, const size_t size)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB16_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "READ_ATTR";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_READATTR));
	ltfsmsg(LTFS_DEBUG3, 30997D, "readattr", (unsigned long long)part, (unsigned long long)id, priv->drive_serial);

	/* Prepare the buffer to transfer */
	uint32_t len = 0;
	if (size == MAXMAM_SIZE)
		len = MAXMAM_SIZE;
	else
		len = size + 4;

	unsigned char *buffer = calloc(1, len);
	if (!buffer) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return -EDEV_NO_MEMORY;
	}

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	/* Build CDB */
	cdb[0] = READ_ATTRIBUTE;
	cdb[1] = 0x00; /* Service Action: 0x00 (Value) */
	cdb[7] = part;
	ltfs_u16tobe(cdb + 8, id);
	ltfs_u32tobe(cdb + 10, len);

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_FROM_TARGET_TO_INITIATOR;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.dxfer_len       = len;
	req.dxferp          = buffer;
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0){
		bool tape_dump = true;

		if (ret == -EDEV_INVALID_FIELD_CDB)
			tape_dump = false;

		_process_errors(device, ret, msg, cmd_desc, tape_dump);

		if (id != TC_MAM_PAGE_COHERENCY &&
			id != TC_MAM_APP_VENDER &&
			id != TC_MAM_APP_NAME &&
			id != TC_MAM_APP_VERSION &&
			id != TC_MAM_USER_MEDIUM_LABEL &&
			id != TC_MAM_TEXT_LOCALIZATION_IDENTIFIER &&
			id != TC_MAM_BARCODE &&
			id != TC_MAM_APP_FORMAT_VERSION)
			ltfsmsg(LTFS_INFO, 30836I, ret);
	} else {
		if (size == MAXMAM_SIZE) {
			/* Include header if request size is MAXMAM_SIZE */
			memcpy(buf, buffer, size);
		} else {
			memcpy(buf, buffer + 4, size);
		}
	}

	free(buffer);
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READATTR));

	return ret;
}

int iokit_allow_overwrite(void *device, const struct tc_position pos)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB16_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "ALLOWOVERW";
	char *msg = NULL;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_ALLOWOVERW));
	ltfsmsg(LTFS_DEBUG, 30997D, "allow overwrite", (unsigned long long)pos.partition, pos.block, priv->drive_serial);

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	cdb[0] = ALLOW_OVERWRITE;
	cdb[2] = 0x01; /* ALLOW_OVERWRITE Current Position */
	cdb[3] = (unsigned char)(pos.partition & 0xff);
	ltfs_u64tobe(cdb + 4, pos.block);

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_NO_DATA_TRANSFER;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0){
		if (pos.block == TAPE_BLOCK_MAX && ret == -EDEV_EOD_DETECTED) {
			ltfsmsg(LTFS_DEBUG, 30827D, "Allow Overwrite");
			ret = DEVICE_GOOD;
		} else {
			_process_errors(device, ret, msg, cmd_desc, true);
		}
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_ALLOWOVERW));

	return ret;
}

/**
 * GRAO command is currently unsupported on this device
 */
int iokit_grao(void *device, unsigned char *buf, const uint32_t len)
{
	int ret = -EDEV_UNSUPPORETD_COMMAND;
	return ret;
}

/**
 * RRAO command is currently unsupported on this device
 */
int iokit_rrao(void *device, unsigned char *buf, const uint32_t len, size_t *out_size)
{
	int ret = -EDEV_UNSUPPORETD_COMMAND;
	return ret;
}

int iokit_set_compression(void *device, const bool enable_compression, struct tc_position *pos)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	unsigned char buf[TC_MP_COMPRESSION_SIZE];

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_SETCOMPRS));

	/* Capture compression setting */
	ret = iokit_modesense(device, TC_MP_COMPRESSION, TC_MP_PC_CURRENT, 0x00, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	buf[0]  = 0x00;
	buf[1]  = 0x00;

	if(enable_compression)
		buf[18] = buf[18] | 0x80;
	else
		buf[18] = buf[18] & 0x7E;

	ret = iokit_modeselect(device, buf, sizeof(buf));

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETCOMPRS));

	return ret;
}

int iokit_set_default(void *device)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	priv->use_sili = true;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_SETDEFAULT));

	/* Disable Read across EOD on the enterprise drive */
	if (IS_ENTERPRISE(priv->drive_type)) {
		unsigned char buf[TC_MP_READ_WRITE_CTRL_SIZE];
		ltfsmsg(LTFS_DEBUG, 30992D, __FUNCTION__, "Disabling read across EOD");
		ret = iokit_modesense(device, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
		if (ret < 0) {
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
			return ret;
		}

		buf[0]  = 0x00;
		buf[1]  = 0x00;
		buf[24] = 0x0C;

		ret = iokit_modeselect(device, buf, sizeof(buf));
		if (ret < 0) {
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
			return ret;
		}
	}

	/* set logical block protection */
	if (priv->vendor == VENDOR_IBM) {
		if (global_data.crc_checking) {
			ltfsmsg(LTFS_DEBUG, 30992D, __FUNCTION__, "Setting LBP");
			ret = _set_lbp(device, true);
		} else {
			ltfsmsg(LTFS_DEBUG, 30992D, __FUNCTION__, "Resetting LBP");
			ret = _set_lbp(device, false);
		}
	} else {
		ret = DEVICE_GOOD;
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
	return ret;
}

/**
 * Get cartridge health information
 * @param device a pointer to the iokit backend
 * @return 0 on success or a negative value on error
 */

#define LOG_TAPE_ALERT          (0x2E)
#define LOG_PERFORMANCE         (0x37)

#define LOG_PERFORMANCE_CAPACITY_SUB (0x64)	// Scope(7-6): Mount Values
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

int iokit_get_cartridge_health(void *device, struct tc_cartridge_health *cart_health)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	unsigned char logdata[LOGSENSEPAGE];
	unsigned char buf[16];
	int i;
	uint32_t param_size;
	uint64_t loghlt;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETCARTHLTH));

	/* Issue LogPage 0x37 */
	cart_health->tape_efficiency  = UNSUPPORTED_CARTRIDGE_HEALTH;
	ret = iokit_logsense(device, LOG_PERFORMANCE, (uint8_t)0, logdata, LOGSENSEPAGE);
	if (ret < 0)
		ltfsmsg(LTFS_INFO, 30837I, LOG_PERFORMANCE, ret, "get cart health");
	else {
		for(i = 0; i < (int)((sizeof(perfstats)/sizeof(perfstats[0]))); i++) {
			if (_parse_logPage(logdata, perfstats[i], &param_size, buf, 16)) {
				ltfsmsg(LTFS_INFO, 30838I, LOG_PERFORMANCE, "get cart health");
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
	ret = iokit_logsense(device, LOG_VOLUMESTATS, (uint8_t)0, logdata, LOGSENSEPAGE);
	if (ret < 0)
		ltfsmsg(LTFS_INFO, 30837I, LOG_VOLUMESTATS, ret, "get cart health");
	else {
		for(i = 0; i < (int)((sizeof(volstats)/sizeof(volstats[0]))); i++) {
			if (_parse_logPage(logdata, volstats[i], &param_size, buf, 16)) {
				ltfsmsg(LTFS_INFO, 30838I, LOG_VOLUMESTATS, "get cart health");
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

int iokit_get_tape_alert(void *device, uint64_t *tape_alert)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	unsigned char logdata[LOGSENSEPAGE];
	unsigned char buf[16];
	uint32_t param_size;
	int i;
	uint64_t ta;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETTAPEALT));

	/* Issue LogPage 0x2E */
	ta = 0;
	ret = iokit_logsense(device, LOG_TAPE_ALERT, (uint8_t)0, logdata, LOGSENSEPAGE);
	if (ret < 0)
		ltfsmsg(LTFS_INFO, 30837I, LOG_TAPE_ALERT, ret, "get tape alert");
	else {
		ret = 0;
		for(i = 1; i <= 64; i++) {
			if (_parse_logPage(logdata, (uint16_t) i, &param_size, buf, 16)
				|| param_size != sizeof(uint8_t)) {
				ltfsmsg(LTFS_INFO, 30838I, LOG_VOLUMESTATS, "get tape alert");
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

int iokit_clear_tape_alert(void *device, uint64_t tape_alert)
{
	struct iokit_data *priv = (struct iokit_data*)device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_CLRTAPEALT));
	priv->tape_alert &= ~tape_alert;
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_CLRTAPEALT));
	return 0;
}

int iokit_get_xattr(void *device, const char *name, char **buf)
{
	int ret = -LTFS_NO_XATTR;
	struct iokit_data *priv = (struct iokit_data*)device;

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
			ret = iokit_logsense(device, LOG_PERFORMANCE, LOG_PERFORMANCE_CAPACITY_SUB, logdata, LOGSENSEPAGE);
			if (ret < 0) {
				ltfsmsg(LTFS_INFO, 30837I, LOG_PERFORMANCE, ret, "get xattr");
			} else {
				ret = 0;
				if (_parse_logPage(logdata, PERF_ACTIVE_CQ_LOSS_W, &param_size, logbuf, 16)) {
					ltfsmsg(LTFS_INFO, 30838I, LOG_PERFORMANCE,  "get xattr");
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
							ltfsmsg(LTFS_INFO, 30839I, param_size);
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
			ltfsmsg(LTFS_ERR, 10001E, "getting active CQ loss write");
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

int iokit_set_xattr(void *device, const char *name, const char *buf, size_t size)
{
	int ret = -LTFS_NO_XATTR;
	char *null_terminated;
	struct iokit_data *priv = (struct iokit_data*)device;
	int64_t wp_count = 0;

	if (!size)
		return -LTFS_BAD_ARG;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_SETXATTR));

	null_terminated = malloc(size + 1);
	if (! null_terminated) {
		ltfsmsg(LTFS_ERR, 10001E, "iokit_set_xattr: null_term");
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETXATTR));
		return -LTFS_NO_MEMORY;
	}
	memcpy(null_terminated, buf, size);
	null_terminated[size] = '\0';

	if (! strcmp(name, "ltfs.vendor.IBM.forceErrorWrite")) {
		wp_count = strtoll(null_terminated, NULL, 0);
		if (wp_count < 0) {
			priv->force_writeperm = -wp_count;
			priv->clear_by_pc     = true;
		}
		if (priv->force_writeperm && priv->force_writeperm < THRESHOLD_FORCE_WRITE_NO_WRITE)
			priv->force_writeperm = THRESHOLD_FORCE_WRITE_NO_WRITE;
		ret = DEVICE_GOOD;
	} else if (! strcmp(name, "ltfs.vendor.IBM.forceErrorType")) {
		priv->force_errortype = strtol(null_terminated, NULL, 0);
		ret = DEVICE_GOOD;
	} else if (! strcmp(name, "ltfs.vendor.IBM.forceErrorRead")) {
		priv->force_readperm = strtoull(null_terminated, NULL, 0);
		priv->read_counter = 0;
		ret = DEVICE_GOOD;
	}
	free(null_terminated);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETXATTR));
	return -LTFS_NO_XATTR;
}

#define BLOCKLEN_DATA_SIZE 6

static int _cdb_read_block_limits(void *device) {
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB6_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "READ_BLOCK_LIMITS";
	char *msg = NULL;

	unsigned char buf[BLOCKLEN_DATA_SIZE];

	ltfsmsg(LTFS_DEBUG, 30992D, "read block limits", priv->drive_serial);

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	/* Build CDB */
	cdb[0] = READ_BLOCK_LIMITS;

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_FROM_TARGET_TO_INITIATOR;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.dxfer_len       = sizeof(buf);
	req.dxferp          = buf;
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0){
		_process_errors(device, ret, msg, cmd_desc, true);
	} else {
		ret =  ((unsigned int) buf[1] & 0xFF) << 16;
		ret += ((unsigned int) buf[2] & 0xFF) << 8;
		ret += ((unsigned int) buf[3] & 0xFF);
	}

	return ret;
}

int iokit_get_parameters(void *device, struct tc_drive_param *params)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETPARAM));

	if (priv->loaded) {
		params->cart_type = priv->cart_type;
		params->density   = priv->density_code;

		if (IS_ENTERPRISE(priv->drive_type)) {
			unsigned char buf[TC_MP_MEDIUM_SENSE_SIZE];

			ret = iokit_modesense(device, TC_MP_MEDIUM_SENSE, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
			if (ret < 0)
				goto out;

			char wp_flag = buf[26];

			if (wp_flag & 0x80) {
				params->write_protect |= VOL_PHYSICAL_WP;
			} else if (wp_flag & 0x01) {
				params->write_protect |= VOL_PERM_WP;
			} else if (wp_flag & 0x10) {
				params->write_protect |= VOL_PERS_WP;
			}

			/* TODO: Following field shall be implemented in the future */
			/*
			if ( (priv->cart_type & 0xF0) == 0xC0 || (priv->cart_type & 0xF0) == 0xA0)
				params->is_worm = true;

			if (priv->density_code & TEST_CRYPTO)
				params->is_encrypted = true;
			*/
		} else {
			unsigned char buf[MODE_DEVICE_CONFIG_SIZE];

			ret = iokit_modesense(device, MODE_DEVICE_CONFIG, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
			if (ret < 0)
				goto out;

			if (buf[3] & 0x80) {
				params->write_protect |= VOL_PHYSICAL_WP;
			}

			/* TODO: Following field shall be implemented in the future */
			/*
			if ( (priv->cart_type & 0x0F) == 0x0C)
				params->is_worm = true;

			// TODO: Store is_crypto based on LP17:200h
			*/
		}
	} else {
		params->cart_type = priv->cart_type;
		params->density   = priv->density_code;
	}

	if (global_data.crc_checking)
		params->max_blksize = MIN(_cdb_read_block_limits(device), IOKIT_MAX_BLOCK_SIZE - 4);
	else
		params->max_blksize = MIN(_cdb_read_block_limits(device), IOKIT_MAX_BLOCK_SIZE);

	ret = 0;

out:
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETPARAM));
	return ret;
}

#define LOG_VOL_STATISTICS         (0x17)
#define LOG_VOL_USED_CAPACITY      (0x203)
#define LOG_VOL_PART_HEADER_SIZE   (4)

int iokit_get_eod_status(void *device, int part)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

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
	ret = iokit_logsense(device, LOG_VOLUMESTATS, (uint8_t)0, logdata, LOGSENSEPAGE);
	if (ret < 0) {
		ltfsmsg(LTFS_WARN, 30840W, LOG_VOLUMESTATS, ret);
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETEODSTAT));
		return EOD_UNKNOWN;
	}

	/* Parse Approximate used native capacity of partitions (0x203)*/
	if (_parse_logPage(logdata, (uint16_t)VOLSTATS_PART_USED_CAP, &param_size, buf, sizeof(buf))
		|| (param_size != sizeof(buf) ) ) {
		ltfsmsg(LTFS_WARN, 30841W);
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
			ltfsmsg(LTFS_WARN, 30842W, i, part_buf, len);

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

	for (i = 0; hp_supported_drives[i]; ++i) {
		if (strncmp(hp_supported_drives[i]->product_id, product_id,
					strlen(hp_supported_drives[i]->product_id)) == 0) {
			product_name = hp_supported_drives[i]->product_name;
			break;
		}
	}

	return product_name;
}

int iokit_get_device_list(struct tc_drive_info *buf, int count)
{
	int i, ret;
	int found = 0;
	int32_t devs = iokit_get_ssc_device_count();
	int drive_type;
	scsi_device_identifier identifier;
	struct iokit_device *iokit_device; // Pointer to device status structure

	iokit_device = malloc(sizeof(struct iokit_device));
	if(iokit_device == NULL) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return -EDEV_NO_MEMORY;
	}

	memset(iokit_device, 0, sizeof(struct iokit_device));
	if( devs > 0 ) {
		for (i = 0; i < devs; i++) {
			if(iokit_find_ssc_device(iokit_device, i) != 0)
			{
				ret = -EDEV_DEVICE_UNOPENABLE;
				continue;
			}
			drive_type = iokit_get_drive_identifier(iokit_device, &identifier);
			if (!drive_type) {
				if (found < count && buf) {
					snprintf(buf[i].name, TAPE_DEVNAME_LEN_MAX, "%d", i);
					snprintf(buf[i].vendor, TAPE_VENDOR_NAME_LEN_MAX, "%s", identifier.vendor_id);
					snprintf(buf[i].model, TAPE_MODEL_NAME_LEN_MAX, "%s", identifier.product_id);
					snprintf(buf[i].serial_number, TAPE_SERIAL_LEN_MAX, "%s", identifier.unit_serial);
					snprintf(buf[i].product_name, PRODUCT_NAME_LENGTH, "%s", _generate_product_name(identifier.product_id));
					buf[i].host    = 0;
					buf[i].channel = 0;
					buf[i].target  = 0;
					buf[i].lun     = -1;
				}
				found ++;
			}
			ret = iokit_free_device(iokit_device);
		}
	}

	return found;
}

void iokit_help_message(const char *progname)
{
	ltfsresult(30999I, default_device);
}

int iokit_parse_opts(void *device, void *opt_args)
{
	struct fuse_args *args = (struct fuse_args *) opt_args;
	int ret;

	CHECK_ARG_NULL(device, -LTFS_NULL_ARG);

	ret = fuse_opt_parse(args, &global_data, iokit_global_opts, null_parser);
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
			ltfsmsg(LTFS_ERR, 30843E, global_data.str_crc_checking);
			return -EDEV_INTERNAL_ERROR;
		}
	} else
		global_data.crc_checking = 0;

	return 0;
}

const char *iokit_default_device_name(void)
{
	const char *devname;
	devname = default_device;
	return devname;
}

static int _cdb_spin(void *device, const uint16_t sps, unsigned char **buffer, size_t * const size)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB12_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "SPIN";
	char *msg = NULL;
	size_t len = *size + 4;

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

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

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_FROM_TARGET_TO_INITIATOR;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.dxfer_len       = len;
	req.dxferp          = *buffer;
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0){
		_process_errors(device, ret, msg, cmd_desc, true);
	}

	*size = ltfs_betou16((*buffer) + 2);

	return ret;
}

int _cdb_spout(void *device, const uint16_t sps,
			   unsigned char *const buffer, const size_t size)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB12_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "SPOUT";
	char *msg = NULL;

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	/* Build CDB */
	cdb[0] = SPOUT;
	cdb[1] = 0x20;
	ltfs_u16tobe(cdb + 2, sps); /* SECURITY PROTOCOL SPECIFIC */
	ltfs_u32tobe(cdb + 6, size);

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_FROM_INITIATOR_TO_TARGET;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.dxfer_len       = size;
	req.dxferp          = buffer;
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret < 0){
		_process_errors(device, ret, msg, cmd_desc, true);
	}

	return ret;
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

	ltfsmsg(LTFS_DEBUG, 30992D, title, s);
}

static bool is_ame(void *device)
{
	unsigned char buf[TC_MP_READ_WRITE_CTRL_SIZE] = {0};
	const int ret = iokit_modesense(device, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));

	if (ret != 0) {
		char message[100] = {0};
		sprintf(message, "failed to get MP %02Xh (%d)", TC_MP_READ_WRITE_CTRL, ret);
		ltfsmsg(LTFS_DEBUG, 30992D, __FUNCTION__, message);

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
		ltfsmsg(LTFS_DEBUG, 30992D, __FUNCTION__, message);

		if (encryption_method != 0x50) {
			ltfsmsg(LTFS_ERR, 30844E, method, encryption_method);
		}
		return encryption_method == 0x50;
	}
}

static int is_encryption_capable(void *device)
{
	struct iokit_data *priv = (struct iokit_data*)device;

	if (IS_LTO(priv->drive_type)) {
		ltfsmsg(LTFS_ERR, 30845E, priv->drive_type);
		return -EDEV_INTERNAL_ERROR;
	}

	if (! is_ame(device))
		return -EDEV_INTERNAL_ERROR;

	return DEVICE_GOOD;
}

int iokit_set_key(void *device, const unsigned char *keyalias, const unsigned char *key)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

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
	ret = iokit_modesense(device, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
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
	ltfsmsg_keyalias(title, keyalias);

	ret = _cdb_spout(device, sps, buffer, size);
	if (ret != DEVICE_GOOD)
		goto free;

	priv->dev.is_data_key_set = keyalias != NULL;

	memset(buf, 0, sizeof(buf));
	ret = iokit_modesense(device, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
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

	ltfsmsg(LTFS_DEBUG, 30992D, title, s);
}

int iokit_get_keyalias(void *device, unsigned char **keyalias)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

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
	ltfsmsg_keyalias(title, priv->dki);

free:
	free(buffer);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETKEYALIAS));
	return ret;
}

int iokit_takedump_drive(void *device, bool capture_unforced)
{
	struct iokit_data *priv = (struct iokit_data*)device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_TAKEDUMPDRV));
	_take_dump(priv, capture_unforced);
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_TAKEDUMPDRV));

	return 0;
}

int iokit_is_mountable(void *device, const char *barcode, const unsigned char cart_type,
							const unsigned char density)
{
	int ret;
	struct iokit_data *priv = (struct iokit_data*)device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_ISMOUNTABLE));

	ret = ibm_tape_is_mountable( priv->drive_type,
								barcode,
								cart_type,
								density,
								global_data.strict_drive);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_ISMOUNTABLE));

	return ret;
}

bool iokit_is_readonly(void *device)
{
	int ret;
	struct iokit_data *priv = (struct iokit_data*)device;

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

int iokit_get_worm_status(void *device, bool *is_worm)
{
	int rc = 0;
	struct iokit_data *priv = (struct iokit_data*)device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETWORMSTAT));
	if (priv->loaded) {
		*is_worm = priv->is_worm;
	} else {
		ltfsmsg(LTFS_INFO, 30870I);
		*is_worm = false;
		rc = -1;
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETWORMSTAT));
	return rc;
}

int iokit_get_serialnumber(void *device, char **result)
{
	struct iokit_data *priv = (struct iokit_data*)device;

	CHECK_ARG_NULL(device, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(result, -LTFS_NULL_ARG);

	ltfs_profiler_add_entry(priv->profiler, NULL, CHANGER_REQ_ENTER(REQ_TC_GETSER));

	*result = strdup((const char *) priv->drive_serial);
	if (! *result) {
		ltfsmsg(LTFS_ERR, 10001E, "iokit_get_serialnumber: result");
		ltfs_profiler_add_entry(priv->profiler, NULL, CHANGER_REQ_EXIT(REQ_TC_GETSER));
		return -EDEV_NO_MEMORY;
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, CHANGER_REQ_EXIT(REQ_TC_GETSER));

	return 0;
}

int iokit_get_info(void *device, struct tc_drive_info *info)
{
	struct iokit_data *priv = (struct iokit_data*)device;

	memcpy(info, &priv->info, sizeof(struct tc_drive_info));

	return 0;
}

int iokit_set_profiler(void *device, char *work_dir, bool enable)
{
	int rc = 0;
	struct iokit_data *priv = (struct iokit_data*)device;

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

int iokit_get_next_block_to_xfer(void *device, struct tc_position *pos)
{
	int ret = -EDEV_UNKNOWN;
	struct iokit_data *priv = (struct iokit_data*)device;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB10_LEN];
	int timeout;
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "READPOS";
	char *msg = NULL;
	unsigned char buf[REDPOS_EXT_LEN];

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_READPOS));

	memset(pos, 0, sizeof(struct tc_position));

	/* Zero out the CDB and the result buffer */
	memset(cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));

	/* Build CDB */
	cdb[0] = READ_POSITION;
	cdb[1] = 0x08; /* Extended Format */
	ltfs_u16tobe(cdb + 7, sizeof(buf)); /* allocation length */

	timeout = get_timeout(priv->timeouts, cdb[0]);
	if (timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Build request */
	req.dxfer_direction = SCSI_FROM_TARGET_TO_INITIATOR;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.dxfer_len       = sizeof(buf);
	req.dxferp          = buf;
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(timeout);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(&priv->dev, &req, &msg);
	if (ret == DEVICE_GOOD) {
		pos->partition = (tape_partition_t)buf[1];
		pos->block     = ltfs_betou64(buf + 16);

		ltfsmsg(LTFS_DEBUG, 30998D, "next-block-to-xfer",
				(unsigned long long)pos->partition, (unsigned long long)pos->block, (unsigned long long)0, priv->drive_serial);
	} else {
		_process_errors(device, ret, msg, cmd_desc, true);
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READPOS));

	return ret;
}

struct tape_ops iokit_handler = {
	.open                   = iokit_open,
	.reopen                 = iokit_reopen,
	.close                  = iokit_close,
	.close_raw              = iokit_close_raw,
	.is_connected           = iokit_is_connected,
	.inquiry                = iokit_inquiry,
	.inquiry_page           = iokit_inquiry_page,
	.test_unit_ready        = iokit_test_unit_ready,
	.read                   = iokit_read,
	.write                  = iokit_write,
	.writefm                = iokit_writefm,
	.rewind                 = iokit_rewind,
	.locate                 = iokit_locate,
	.space                  = iokit_space,
	.erase                  = iokit_erase,
	.load                   = iokit_load,
	.unload                 = iokit_unload,
	.readpos                = iokit_readpos,
	.setcap                 = iokit_setcap,
	.format                 = iokit_format,
	.remaining_capacity     = iokit_remaining_capacity,
	.logsense               = iokit_logsense,
	.modesense              = iokit_modesense,
	.modeselect             = iokit_modeselect,
	.reserve_unit           = iokit_reserve,
	.release_unit           = iokit_release,
	.prevent_medium_removal = iokit_prevent_medium_removal,
	.allow_medium_removal   = iokit_allow_medium_removal,
	.write_attribute        = iokit_write_attribute,
	.read_attribute         = iokit_read_attribute,
	.allow_overwrite        = iokit_allow_overwrite,
	.grao                   = iokit_grao,
	.rrao                   = iokit_rrao,
	// May be command combination
	.set_compression        = iokit_set_compression,
	.set_default            = iokit_set_default,
	.get_cartridge_health   = iokit_get_cartridge_health,
	.get_tape_alert         = iokit_get_tape_alert,
	.clear_tape_alert       = iokit_clear_tape_alert,
	.get_xattr              = iokit_get_xattr,
	.set_xattr              = iokit_set_xattr,
	.get_parameters         = iokit_get_parameters,
	.get_eod_status         = iokit_get_eod_status,
	.get_device_list        = iokit_get_device_list,
	.help_message           = iokit_help_message,
	.parse_opts             = iokit_parse_opts,
	.default_device_name    = iokit_default_device_name,
	.set_key                = iokit_set_key,
	.get_keyalias           = iokit_get_keyalias,
	.takedump_drive         = iokit_takedump_drive,
	.is_mountable           = iokit_is_mountable,
	.get_worm_status        = iokit_get_worm_status,
	.get_serialnumber       = iokit_get_serialnumber,
	.get_info               = iokit_get_info,
	.set_profiler           = iokit_set_profiler,
	.get_next_block_to_xfer = iokit_get_next_block_to_xfer,
	.is_readonly            = iokit_is_readonly,
};

struct tape_ops *tape_dev_get_ops(void)
{
	if (!standard_table)
		standard_table = standard_tape_errors;
	if (!vendor_table)
		vendor_table = ibm_tape_errors;

	return &iokit_handler;
}

extern char tape_iokit_dat[];

const char *tape_dev_get_message_bundle_name(void **message_data)
{
	*message_data = tape_iokit_dat;
	return "tape_iokit";
}
