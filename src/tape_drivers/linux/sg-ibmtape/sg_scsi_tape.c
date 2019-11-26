/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2019 IBM Corp. All rights reserved.
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
** FILE NAME:       tape_drivers/linux/sg-ibmtape/sg_scsi_tape.c
**
** DESCRIPTION:     Implements SCSI command handling in sg tape driver
**
** AUTHORS:         Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
**
*************************************************************************************
*/

#include <stdint.h>
#include <sys/ioctl.h>

#include "libltfs/ltfs_error.h"
#include "libltfs/ltfs_endian.h"
#include "libltfs/ltfslogging.h"

#include "tape_drivers/ibm_tape.h"

#include "sg_scsi_tape.h"

/* Extern those 2 variables for sense conversion for specific tape */
struct error_table *standard_table = NULL;
struct error_table *vendor_table = NULL;

/* Local functions */
static int sg_sense2errno(sg_io_hdr_t *req, uint32_t *s, char **msg)
{
	int rc = -EDEV_UNKNOWN;
	unsigned char *sense = req->sbp;
	uint32_t sense_value = 0;

	unsigned char sk   = (*(sense + 2)) & 0x0F;
	unsigned char asc  = *(sense + 12);
	unsigned char ascq = *(sense + 13);

	sense_value += (uint32_t) sk << 16;
	sense_value += (uint32_t) asc << 8;
	sense_value += (uint32_t) ascq;

	*s = sense_value;

	rc = _sense2errorcode(sense_value, standard_table, msg, MASK_WITH_SENSE_KEY);
	/* NOTE: error table must be changed in library edition */
	if (rc == -EDEV_VENDOR_UNIQUE)
		rc = _sense2errorcode(sense_value, vendor_table, msg, MASK_WITH_SENSE_KEY);

	if (rc == -EDEV_UNKNOWN) {
		ltfsmsg(LTFS_INFO, 30287I, sense_value);
	}

	return rc;
}

static bool is_expected_error(struct sg_tape *device, uint8_t *cdb, int32_t rc )
{
	int cmd = (cdb[0]&0xFF);
	uint64_t destination;
	uint64_t cdb_dest[8];
	int i;

	switch (cmd) {
		case TEST_UNIT_READY:
			if (rc == -EDEV_NEED_INITIALIZE || rc == -EDEV_CONFIGURE_CHANGED)
				return true;
			break;
		case READ:
			if (rc == -EDEV_FILEMARK_DETECTED || rc == -EDEV_NO_SENSE || rc == -EDEV_CLEANING_REQUIRED)
				return true;
			if ((rc == -EDEV_CRYPTO_ERROR || rc == -EDEV_KEY_REQUIRED) && !device->is_data_key_set)
				return true;
			break;
		case WRITE:
			if (rc == -EDEV_EARLY_WARNING || rc == -EDEV_PROG_EARLY_WARNING || rc == -EDEV_CLEANING_REQUIRED)
				return true;
			break;
		case WRITE_FILEMARKS6:
			if (rc == -EDEV_EARLY_WARNING || rc == -EDEV_PROG_EARLY_WARNING || rc == -EDEV_CLEANING_REQUIRED)
				return true;
			break;
		case LOAD_UNLOAD:
			if ((cdb[4] & 0x01) == 0) // Unload
				if (rc == -EDEV_CLEANING_REQUIRED)
					return true;
			break;
		case MODE_SELECT10:
			if (rc == -EDEV_MODE_PARAMETER_ROUNDED)
				return true;
			break;
		case LOCATE16:
			for (i=0; i<8; i++)
				cdb_dest[i] = (uint64_t)cdb[i+4] & 0xff;

			destination = (cdb_dest[0] << 56) + (cdb_dest[1] << 48)
			+ (cdb_dest[2] << 40) + (cdb_dest[3] << 32)
			+ (cdb_dest[4] << 24) + (cdb_dest[5] << 16)
			+ (cdb_dest[6] << 8) + cdb_dest[7];

			if (destination == TAPE_BLOCK_MAX && rc == -EDEV_EOD_DETECTED)
				return true;
			break;
	}

	return false;
}

/* Global functions */
#define HOST_OK          (0x00)
#define HOST_NO_CONNECT  (0x01)
#define HOST_BUS_BUSY    (0x02)
#define HOST_TIME_OUT    (0x03)
#define HOST_BAD_TARGET  (0x04)
#define HOST_ABORT       (0x05)
#define HOST_PARITY      (0x06)
#define HOST_ERROR       (0x07)
#define HOST_RESET       (0x08)
#define HOST_BAD_INTR    (0x09)
#define HOST_PASSTHROUGH (0x0a)
#define HOST_SOFT_ERROR  (0x0b)
#define HOST_IMM_RETRY   (0x0c)
#define HOST_REQUEUE     (0x0d)
#define HOST_TRANS_DISR  (0x0e)
#define HOST_TRANS_FAIL  (0x0f)
#define HOST_TARGET_FAIL (0x10)
#define HOST_NEXUS_FAIL  (0x11)

#define DRIVER_OK        (0x00)
#define DRIVER_BUSY      (0x01)
#define DRIVER_SOFT      (0x02)
#define DRIVER_MEDIA     (0x03)
#define DRIVER_ERROR     (0x04)
#define DRIVER_INVALID   (0x05)
#define DRIVER_TIMEOUT   (0x06)
#define DRIVER_HARD      (0x07)
#define DRIVER_SENSE     (0x08)

#define NO_SUGGESTION    (0x00)
#define SUGGEST_RETRY    (0x10)
#define SUGGEST_ABORT    (0x20)
#define SUGGEST_REMAP    (0x30)
#define SUGGEST_DIE      (0x40)
#define SUGGEST_SENSE    (0x80)

int sg_issue_cdb_command(struct sg_tape *device, sg_io_hdr_t *req, char **msg)
{
	int ret = -1;
	uint32_t sense = 0;
	unsigned short d_suggest = 0, d_status;
	unsigned char masked_status = 0;
	unsigned int retry_count = 0;

	CHECK_ARG_NULL(req, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(msg, -LTFS_NULL_ARG);

	if (device->fd < 0)
		return -EDEV_NO_CONNECTION;

start:
	ret = ioctl(device->fd, SG_IO, req);
	if (ret < 0) {
		ltfsmsg(LTFS_INFO, 30200I, *req->cmdp, errno);
		if (errno == ENODEV) {
			if (msg) *msg = "No device found";
			return -EDEV_CONNECTION_LOST;
		} else if (errno == ENOMEM) {
			if (msg) *msg = "ioctl ENOMEM error";
			return -EDEV_BUFFER_ALLOCATE_ERROR;
		} else {
			if (msg) *msg = "ioctl error";
			return -EDEV_INTERNAL_ERROR;
		}
	}

	if (req->host_status) {
		switch (req->host_status) {
			case HOST_NO_CONNECT:
				if (msg) *msg = "Couldn't connect before timeout period";
				ret = -EDEV_CONNECTION_LOST;
				break;
			case HOST_BUS_BUSY:
				if (msg) *msg = "Bus stayed busy through timeout period";
				ret = -EDEV_DEVICE_BUSY;
				break;
			case HOST_TIME_OUT:
				if (msg) *msg = "Command TIMEOUT";
				ret = -EDEV_TIMEOUT;
				break;
			case HOST_BAD_TARGET:
				if (msg) *msg = "Bad target, device not responding?";
				ret = -EDEV_CONNECTION_LOST;
				break;
			case HOST_ABORT:
				if (msg) *msg = "Abort";
				ret = -EDEV_ABORTED_COMMAND;
				break;
			case HOST_PARITY:
				if (msg) *msg = "Parity error";
				ret = -EDEV_HOST_ERROR;
				break;
			case HOST_ERROR:
				if (msg) *msg = "Internal error detected in the host adapter";
				ret = -EDEV_HOST_ERROR;
				break;
			case HOST_RESET:
				if (msg) *msg = "The SCSI bus (or this device) has been reset";
				ret = -EDEV_CONNECTION_LOST;
				break;
			case HOST_BAD_INTR:
				if (msg) *msg = "Unexpected interrupt";
				ret = -EDEV_HOST_ERROR;
				break;
			case HOST_PASSTHROUGH:
				if (msg) *msg = "Force command past mid-layer";
				ret = -EDEV_HOST_ERROR;
				break;
			case HOST_SOFT_ERROR:
				if (msg) *msg = "The low level driver wants a retry";
				if (!retry_count) {
					if (msg) *msg = "";
					retry_count++;
					goto start;
				}
				ret = -EDEV_HOST_ERROR;
				break;
			case HOST_IMM_RETRY:
			case HOST_REQUEUE:
				/* immediate retry without decrementing counter */
				goto start;
				break;
			case HOST_TRANS_DISR:
				if (msg) *msg = "Disrupted transport failure";
				ret = -EDEV_CONNECTION_LOST;
				break;
			case HOST_TRANS_FAIL:
				if (msg) *msg = "Transport failure";
				ret = -EDEV_CONNECTION_LOST;
				break;
			case HOST_TARGET_FAIL:
				if (msg) *msg = "Target failure";
				ret = -EDEV_CONNECTION_LOST;
				break;
			case HOST_NEXUS_FAIL:
				/* See https://fossies.org/linux/sdparm/lib/sg_pt_linux.c */
				if (msg) *msg = "SCSI nexus failure (reservation conflict)";
				ret = -EDEV_RESERVATION_CONFLICT;
				break;
			default:
				ltfsmsg(LTFS_INFO, 30244I, req->host_status, req->driver_status);
				if (msg) *msg = "Unexpected host status";
				ret = -EDEV_HOST_ERROR;
				break;
		}

		if (ret)
			return ret;
	}

	if (req->driver_status) {
		d_suggest = req->driver_status & 0xF0;
		d_status  = req->driver_status & 0x0F;

		switch (d_status) {
			case DRIVER_OK:
				/* Do nothing */
				break;
			case DRIVER_BUSY:
				if (msg) *msg = "Busy on the driver";
				ret = -EDEV_DEVICE_BUSY;
				break;
			case DRIVER_TIMEOUT:
				if (msg) *msg = "Timeout on the driver";
				ret = -EDEV_TIMEOUT;
				break;
			case DRIVER_SENSE:
				masked_status = SCSI_CHECK_CONDITION;
				break;
			case DRIVER_SOFT:
			case DRIVER_MEDIA:
			case DRIVER_ERROR:
			case DRIVER_INVALID:
			case DRIVER_HARD:
			default:
				ltfsmsg(LTFS_INFO, 30244I, req->host_status, req->driver_status);
				if (msg) *msg = "Busy on the driver";
				ret = -EDEV_DRIVER_ERROR;
				break;
		}

		switch (d_suggest) {
			case NO_SUGGESTION:
				/* Do nothing */
				break;
			case SUGGEST_RETRY:
				if (!retry_count) {
					if (msg) *msg = "";
					retry_count++;
					goto start;
				}
				ret = -EDEV_DRIVER_ERROR;
				break;
			case SUGGEST_SENSE:
				if (masked_status != SCSI_CHECK_CONDITION)
					masked_status = SCSI_CHECK_CONDITION;
				break;
			case SUGGEST_ABORT:
			case SUGGEST_REMAP:
			case SUGGEST_DIE:
			default:
				if (!ret)
					ret = -EDEV_DRIVER_ERROR;
				break;
		}

		if (ret)
			return ret;
	}

	if (masked_status != SCSI_CHECK_CONDITION)
		masked_status = req->masked_status;

	switch (masked_status) {
		case SCSI_GOOD:
			ret = DEVICE_GOOD;
			break;
		case SCSI_CHECK_CONDITION:
			if (req->sb_len_wr) {
				ret = sg_sense2errno(req, &sense, msg);
				ltfsmsg(LTFS_DEBUG, 30201D, sense, *msg);
			} else {
				ret = -EDEV_NO_SENSE;
				ltfsmsg(LTFS_DEBUG, 30202D, "nosense");
			}
			break;
		case SCSI_BUSY:
			ltfsmsg(LTFS_DEBUG, 30202D, "busy");
			ret = -EDEV_DEVICE_BUSY;
			if (msg) *msg = "Drive busy";
			break;
		case SCSI_RESERVATION_CONFLICT:
			ltfsmsg(LTFS_DEBUG, 30202D, "reservation conflict");
			ret = -EDEV_RESERVATION_CONFLICT;
			if (msg) *msg = "Drive reservation conflict";
			break;
		default:
			ltfsmsg(LTFS_INFO, 30203I, req->status, req->masked_status);
			ret = -EDEV_TARGET_ERROR;
			if (msg) *msg = "CDB command returned unexpected status";
			break;
	}

	if (ret != DEVICE_GOOD) {
		if (is_expected_error(device, req->cmdp, ret)) {
			ltfsmsg(LTFS_DEBUG, 30204D, (char *)req->usr_ptr, req->cmdp[0], ret);
		} else {
			ltfsmsg(LTFS_INFO, 30205I, (char *)req->usr_ptr, req->cmdp[0], ret);
		}
	}

	return ret;
}

static int _inquiry_low(struct sg_tape *device, uint8_t page, unsigned char *buf, size_t size)
{
	int ret = -EDEV_UNKNOWN;

	sg_io_hdr_t req;
	unsigned char cdb[CDB6_LEN];
	unsigned char sense[MAXSENSE];
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "INQUIRY LOW";
	char *msg;

	// Zero out the CDB and the result buffer
	ret = init_sg_io_header(&req);
	if (ret < 0)
		return ret;

	memset(cdb, 0, sizeof(cdb));
	memset(sense, 0, sizeof(sense));
	memset(buf, 0, size);

	/* Build CDB */
	cdb[0] = INQUIRY;
	if(page)
		cdb[1] = 0x01;
	cdb[2] = page;
	ltfs_u16tobe(cdb + 3, size);

	/* Build request */
	req.dxfer_direction = SCSI_FROM_TARGET_TO_INITIATOR;
	req.cmd_len         = sizeof(cdb);
	req.mx_sb_len       = sizeof(sense);
	req.dxfer_len       = size;
	req.dxferp          = buf;
	req.cmdp            = cdb;
	req.sbp             = sense;
	req.timeout         = SGConversion(10);
	req.usr_ptr         = (void *)cmd_desc;

	ret = sg_issue_cdb_command(device, &req, &msg);

	return ret;
}

int sg_get_drive_identifier(struct sg_tape *device, scsi_device_identifier *id_data)
{
	int ret;
	unsigned char inquiry_buf[MAX_INQ_LEN];

	CHECK_ARG_NULL(id_data, -LTFS_NULL_ARG);

	ret = _inquiry_low(device, 0, inquiry_buf, MAX_INQ_LEN);
	if( ret < 0 ) {
		ltfsmsg(LTFS_INFO, 30206I, ret);
		return ret;
	}

	memset(id_data, 0, sizeof(scsi_device_identifier));

	if ((inquiry_buf[0] & PERIPHERAL_MASK) != SEQUENTIAL_DEVICE) {
		/* TODO: Need a debug message */
		return -EDEV_DEVICE_UNSUPPORTABLE;
	}

	strncpy(id_data->vendor_id,   (char*)(&(inquiry_buf[8])),  VENDOR_ID_LENGTH);
	strncpy(id_data->product_id,  (char*)(&(inquiry_buf[16])), PRODUCT_ID_LENGTH);
	strncpy(id_data->product_rev, (char*)(&(inquiry_buf[32])), PRODUCT_REV_LENGTH);

	ret = _inquiry_low(device, 0x80, inquiry_buf, MAX_INQ_LEN);
	if( ret < 0 ) {
		ltfsmsg(LTFS_INFO, 30206I, ret);
		return ret;
	}

	strncpy(id_data->unit_serial, (char*)(&(inquiry_buf[4])), inquiry_buf[3]);

	return 0;
}
