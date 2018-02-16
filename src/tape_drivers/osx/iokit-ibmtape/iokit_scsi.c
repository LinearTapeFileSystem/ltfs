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
** FILE NAME:       tape_drivers/osx/iokit/iokit_scsi_base.c
**
** DESCRIPTION:     Implements raw SCSI operations in user-space
**                  to control SCSI-based tape and tape changer
**                  devices.
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

#include <stdint.h>

#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOTypes.h>
#include <IOKit/scsi/SCSITaskLib.h>
#include <IOKit/storage/IOStorageProtocolCharacteristics.h>
#include <IOKit/storage/IOStorageDeviceCharacteristics.h>
#include <mach/mach.h>

#include "iokit_service.h"
#include "iokit_scsi.h"

#include "libltfs/ltfs_error.h"
#include "libltfs/ltfs_endian.h"
#include "libltfs/ltfslogging.h"

#include "tape_drivers/ibm_tape.h"

/* Extern those 2 variables for sense conversion for specific tape */
struct error_table *standard_table = NULL;
struct error_table *vendor_table = NULL;

/* Local functions */
static int iokit_sense2errno(struct iokit_scsi_request *req, uint32_t *s, char **msg)
{
	SCSI_Sense_Data *sense = &req->sense_buffer;
	uint32_t        sense_value = 0;
	int             rc = -EDEV_UNKNOWN;

	sense_value += (uint32_t) ((sense->SENSE_KEY & kSENSE_KEY_Mask) & 0x0F) << 16;
	sense_value += (uint32_t) (sense->ADDITIONAL_SENSE_CODE) << 8;
	sense_value += (uint32_t) sense->ADDITIONAL_SENSE_CODE_QUALIFIER;

	*s = sense_value;

	rc = _sense2errorcode(sense_value, standard_table, msg, MASK_WITH_SENSE_KEY);
	/* NOTE: error table must be changed in library edition */
	if (rc == -EDEV_VENDOR_UNIQUE)
		rc = _sense2errorcode(sense_value, vendor_table, msg, MASK_WITH_SENSE_KEY);

	return rc;
}

static bool is_expected_error(struct iokit_device *device, uint8_t *cdb, int32_t rc )
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
int iokit_issue_cdb_command(struct iokit_device *device,
							struct iokit_scsi_request *req,
							char **msg)
{
	int ret = -EDEV_INTERNAL_ERROR;;
	//char sense_string[SIZE_OF_SENSE_STRING] = "";

	IOReturn kernelReturn = kIOReturnSuccess;
	IOVirtualRange *range = NULL;
	uint64_t transfer_count = 0;
	uint32_t sense = 0;

	CHECK_ARG_NULL(device->scsiTaskInterface, -LTFS_NULL_ARG);

	ret = iokit_allocate_scsitask(device);
	if(ret != 0) {
		return ret;
	}

	if( (req->dxferp) && (req->dxfer_len > 0) ) {
		// Allocate a virtual range for the buffer. If we had more than 1 scatter-gather entry,
		// we would allocate more than 1 IOVirtualRange.
		range = (IOVirtualRange *) malloc(sizeof(IOVirtualRange));
		if (range == NULL) {
			ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
			ret = -EDEV_NO_MEMORY;
			goto free;
		}

		// Set up the range. The address is just the buffer's address. The length is our request size.
		range->address = (IOVirtualAddress) req->dxferp;
		range->length = req->dxfer_len;

		// Set the scatter-gather entry in the task.
		kernelReturn = (*device->task)->SetScatterGatherEntries(device->task, range, 1,
																req->dxfer_len, req->dxfer_direction);
		if (kernelReturn != kIOReturnSuccess) {
			ltfsmsg(LTFS_INFO, 30800I, *req->cmdp, kernelReturn);
			ret = -EDEV_INTERNAL_ERROR;
			goto free;
		}
	}

	// Set the actual cdb in the task.
	kernelReturn = (*device->task)->SetCommandDescriptorBlock(device->task, req->cmdp, req->cmd_len);
	if (kernelReturn != kIOReturnSuccess) {
		ltfsmsg(LTFS_INFO, 30801I, *req->cmdp, kernelReturn);
		ret = -EDEV_INTERNAL_ERROR;
		goto free;
	}

	// Set the timeout in the task
	kernelReturn = (*device->task)->SetTimeoutDuration(device->task, req->timeout);
	if (kernelReturn != kIOReturnSuccess) {
		ltfsmsg(LTFS_INFO, 30802I, *req->cmdp, kernelReturn);
		ret = -EDEV_INTERNAL_ERROR;
		goto free;
	}

	kernelReturn = (*device->task)->ExecuteTaskSync(device->task, &req->sense_buffer,
													&req->status, &transfer_count);
	if (kernelReturn != kIOReturnSuccess) {
		ltfsmsg(LTFS_INFO, 30803I, *req->cmdp, kernelReturn);
		ret = -EDEV_INTERNAL_ERROR;
		goto free;
	}

	req->actual_xfered = transfer_count;
	req->resid         = req->dxfer_len - transfer_count;

	switch (req->status) {
		case kSCSITaskStatus_GOOD:
			ret = DEVICE_GOOD;
			break;
		case kSCSITaskStatus_CHECK_CONDITION:
			ret = iokit_sense2errno(req, &sense, msg);
			ltfsmsg(LTFS_DEBUG, 30804D, sense, *msg);
			break;
		case kSCSITaskStatus_BUSY:
			ltfsmsg(LTFS_DEBUG, 30805D, "busy");
			ret = -EDEV_DEVICE_BUSY;
			if (msg) {
				*msg = "Drive busy";
			}
			break;
		case kSCSITaskStatus_RESERVATION_CONFLICT:
			ltfsmsg(LTFS_DEBUG, 30805D, "reservation conflict");
			ret = -EDEV_RESERVATION_CONFLICT;
			if (msg) {
				*msg = "Drive reservation conflict";
			}
			break;
		default:
			ret = -EDEV_DRIVER_ERROR;
			ltfsmsg(LTFS_INFO, 30806I, req->status);
			if (msg) {
				*msg = "CDB command returned with unexpected status";
			}
			break;
	}

free:
	if (ret != DEVICE_GOOD) {
		if (is_expected_error(device, req->cmdp, ret)) {
			ltfsmsg(LTFS_DEBUG, 30807D, req->desc, req->cmdp[0], ret);
		} else {
			ltfsmsg(LTFS_INFO, 30808I, req->desc, req->cmdp[0], ret);
		}
	}

	// Be a good citizen and clean up.
	free(range);

	// Clean up task for future use
	(*device->task)->SetTimeoutDuration(device->task, 0);
	(*device->task)->SetCommandDescriptorBlock(device->task, (UInt8 *)NULL, 0);
	(*device->task)->SetScatterGatherEntries(device->task, NULL, 0, 0, kSCSIDataTransfer_NoDataTransfer);

	return ret;
}

static int _inquiry_low(struct iokit_device *device, uint8_t page, unsigned char *buf, size_t size)
{
	int ret = -EDEV_UNKNOWN;

	struct iokit_scsi_request req;
	unsigned char cdb[CDB6_LEN];
	char cmd_desc[COMMAND_DESCRIPTION_LENGTH] = "INQUIRY LOW";
	char *msg;

	// Zero out the CDB and the result buffer
	memset(&cdb, 0, sizeof(cdb));
	memset(&req, 0, sizeof(struct iokit_scsi_request));
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
	req.mx_sb_len       = sizeof(SCSI_Sense_Data);
	req.dxfer_len       = size;
	req.dxferp          = buf;
	req.cmdp            = cdb;
	memset(&req.sense_buffer, 0, req.mx_sb_len);
	req.timeout         = IOKitConversion(10);
	req.desc            = cmd_desc;

	ret = iokit_issue_cdb_command(device, &req, &msg);

	return ret;
}

int iokit_get_drive_identifier(struct iokit_device *device,
							   scsi_device_identifier *id_data)
{
	int ret;
	unsigned char inquiry_buf[MAX_INQ_LEN];

	CHECK_ARG_NULL(device->scsiTaskInterface, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(id_data, -LTFS_NULL_ARG);

	ret = _inquiry_low(device, 0, inquiry_buf, MAX_INQ_LEN);
	if( ret < 0 ) {
		ltfsmsg(LTFS_INFO, 30809I, ret);
		return ret;
	}

	memset(id_data, 0, sizeof(scsi_device_identifier));

	strncpy(id_data->vendor_id,   (char*)(&(inquiry_buf[8])),  VENDOR_ID_LENGTH);
	strncpy(id_data->product_id,  (char*)(&(inquiry_buf[16])), PRODUCT_ID_LENGTH);
	strncpy(id_data->product_rev, (char*)(&(inquiry_buf[32])), PRODUCT_REV_LENGTH);

	ret = _inquiry_low(device, 0x80, inquiry_buf, MAX_INQ_LEN);
	if( ret < 0 ) {
		ltfsmsg(LTFS_INFO, 30809I, ret);
		return ret;
	}

	strncpy(id_data->unit_serial, (char*)(&(inquiry_buf[4])), inquiry_buf[3]);

	return 0;
}
