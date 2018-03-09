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
** FILE NAME:       tape_drivers/freebsd/cam/camtape_cmn.c
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


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <cam/scsi/scsi_message.h>
#include "ltfs_copyright.h"
#include "ibm_tape.h"
#include "libltfs/ltfslogging.h"
#include "camtape_cmn.h"

volatile char *copyright = LTFS_COPYRIGHT_0"\n"LTFS_COPYRIGHT_1"\n"LTFS_COPYRIGHT_2"\n";

extern struct error_table *standard_table;
extern struct error_table *vendor_table;

/* Global Functions */

int camtape_sense2rc(void *device, struct scsi_sense_data *sense, int sense_len)
{
	uint32_t sense_concat;
	int error_code, sense_key, asc, ascq;
	int rc;

	scsi_extract_sense_len(sense, sense_len, &error_code, &sense_key, &asc, &ascq,
		/*show_errors*/ 1);
	sense_concat = ((sense_key & 0xff) << 16) |
					((asc & 0xff) << 8) |
					(ascq & 0xff);
	/*
	 * If the asc and/or ascq are 0xff (i.e. -1), it will return an error that the
	 * sense code is vendor unique.  Those are the first fields (before the sense key)
	 * that will get trimmed off and therefore cause an error
	 */
	rc = _sense2errcode(sense_concat, standard_table, NULL, MASK_WITH_SENSE_KEY);

	if (rc == -EDEV_VENDOR_UNIQUE) {
		rc = _sense2errcode(sense_concat, vendor_table, NULL, MASK_WITH_SENSE_KEY);
	}

	return rc;
}

/**
 * Given a completed CCB, return an LTFS error code.
 * @param ccb CAM CCB
 * @return 0 on success or negative value on error
 */
int camtape_ccb2rc(struct camtape_data *softc, union ccb *ccb)
{
	int rc;

	switch (ccb->ccb_h.status & CAM_STATUS_MASK) {
	case CAM_REQ_CMP:
		rc = DEVICE_GOOD;
		break;
	case CAM_SCSI_STATUS_ERROR: {
		switch (ccb->csio.scsi_status) {
		case SCSI_STATUS_OK:
			/* This shouldn't happen, but just in case... */
			rc = DEVICE_GOOD;
			break;
		case SCSI_STATUS_CHECK_COND:
			if (ccb->ccb_h.status & CAM_AUTOSNS_VALID)
				rc = camtape_sense2rc(softc, &ccb->csio.sense_data, ccb->csio.sense_len -
					ccb->csio.sense_resid);
			else
				rc = -EDEV_TARGET_ERROR;
			break;
		case SCSI_STATUS_BUSY:
		case SCSI_STATUS_QUEUE_FULL:
			rc = -EDEV_DEVICE_BUSY;
			break;
		case SCSI_STATUS_RESERV_CONFLICT:
		default:
			rc = -EDEV_TARGET_ERROR;
			break;
		}
		break;
	}
	case CAM_REQ_INVALID:
		rc = -EDEV_INVALID_ARG;
		break;
	case CAM_SEL_TIMEOUT:
	case CAM_DEV_NOT_THERE:
		rc = -EDEV_DEVICE_UNOPENABLE;
		break;
	case CAM_REQ_ABORTED:
		rc = -EDEV_ABORTED_COMMAND;
		break;
	case CAM_CMD_TIMEOUT:
		rc = -EDEV_TIMEOUT;
		break;
	default:
		rc = -EDEV_HOST_ERROR;
		break;
	}

	return rc;
}

int camtape_ioctlrc2err(void *device, int fd, struct scsi_sense_data *sense_data, 
	int control_cmd, char **msg)
{
	union mterrstat errstat;
	int rc, rc_sense;

	memset(&errstat, 0, sizeof(errstat));

	rc_sense = ioctl(fd, MTIOCERRSTAT, &errstat);

	if (rc_sense == 0) {
		size_t sense_data_len;

		if (control_cmd == 0) {
			sense_data_len = sizeof(errstat.scsi_errstat.io_sense);
			memcpy(sense_data, &errstat.scsi_errstat.io_sense,
				MIN(sizeof(*sense_data), sense_data_len));
		} else {
			sense_data_len = sizeof(errstat.scsi_errstat.ctl_sense);
			memcpy(sense_data, &errstat.scsi_errstat.ctl_sense,
				MIN(sizeof(*sense_data), sense_data_len));
		}

		/*
		 * The latched sense data in the sa(4) driver is cleared after it is read,
		 * if the non-control device (e.g. /dev/sa0, not /dev/sa0.ctl) was opened.
		 */
		if (sense_data->error_code == 0) {
			ltfsmsg(LTFS_DEBUG, 31209D);

			if (msg) {
				*msg = strdup("No Sense Information");
			}
			rc = -EDEV_NO_SENSE;
		} else {
			int error_code, sense_key, asc, ascq;

			scsi_extract_sense_len(sense_data, sense_data_len, &error_code, &sense_key, &asc,
				&ascq, /*show_errors*/ 1);
			ltfsmsg(LTFS_DEBUG, 31206D, sense_key, asc, ascq);
			/*
			 * XXX KDM we should figure out a better way to extract these vendor specific bits.
			 * Do the IBM drives not support descriptor sense?
			 * The vendor specific data that this debug statement pulls out is beyond the 32
			 * bytes of sense data that the sa(4) driver provides through the MTIOCERRSTAT ioctl.
			 * This means that we will have to bump the size of the sense data used in the
			 * ioctl.  That also means we have an opportunity to provide more information in
			 * the ioctl, like the valid length of the sense data.
			 */

#if 0
			ltfsmsg(LTFS_DEBUG, 31207D, sense_data->vendor[27], sense_data->vendor[28], sense_data->vendor[29], sense_data->vendor[30],
										((struct camtape_data *) device)->drive_serial);
#endif
			rc = camtape_sense2rc(device, sense_data, sense_data_len);
		}
	} else {
		ltfsmsg(LTFS_INFO, 31212I, rc_sense);
		if (msg)
			*msg = strdup("Cannot get sense information");

		rc = -EDEV_CANNOT_GET_SENSE;
	}

	return rc;
}

/**
 * Get inquiry data from a specific page
 * @param device tape device
 * @param page page
 * @param inq pointer to inquiry data. This function will update this value
 * @return 0 on success or negative value on error
 */
int _camtape_inquiry_page(void *device, unsigned char page, struct tc_inq_page *inq, bool error_handle)
{
	char *msg = NULL;
	int rc = DEVICE_GOOD;
	union ccb *ccb = NULL;
	struct camtape_data *softc = (struct camtape_data *)device;
	uint8_t *data_ptr = NULL;
	int timeout;

	if (!inq)
		return -EDEV_INVALID_ARG;

	ccb = cam_getccb(softc->cd);
	if (ccb == NULL) {
		rc = -EDEV_NO_MEMORY;
		goto bailout;
	}

	memset(&(&ccb->ccb_h)[1], 0,
		sizeof(struct ccb_scsiio) - sizeof(struct ccb_hdr));

	data_ptr = malloc(sizeof(*inq));
	if (data_ptr == NULL) {
		rc = -EDEV_NO_MEMORY;
		goto bailout;
	}
	memset(data_ptr, 0, sizeof(*inq));

	timeout = camtape_get_timeout(softc->timeouts, INQUIRY);
	if (timeout < 0) {
		rc = -EDEV_UNSUPPORETD_COMMAND;
		goto bailout;
	}

	ltfsmsg(LTFS_DEBUG, 31393D, "inquiry", page, softc->drive_serial);

	scsi_inquiry(&ccb->csio,
				 /*retries*/ 1,
				 /*cbfcnp*/ NULL,
				 /*tag_action*/ MSG_SIMPLE_Q_TAG,
				 /*inq_buf*/ data_ptr,
				 /*inq_len*/ sizeof(*inq),
				 /*evpd*/ 1,
				 /*page_code*/ page,
				 /*sense_len*/ SSD_FULL_SIZE,
				 /*timeout*/ timeout);

	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS | CAM_PASS_ERR_RECOVER;

	rc = camtape_send_ccb(softc, ccb, &msg);

	if (rc != DEVICE_GOOD) {
		if(error_handle)
			camtape_process_errors(device, rc, msg, "inquiry", true);
	} else {
		memcpy(inq->data, data_ptr, sizeof(inq->data));
	}

bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);

	if (data_ptr != NULL)
		free(data_ptr);

	return rc;
}

int camtape_inquiry_page(void *device, unsigned char page, struct tc_inq_page *inq)
{
	struct camtape_data *softc = (struct camtape_data *)device;
	int ret = 0;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_INQUIRYPAGE));
	ret = _camtape_inquiry_page(device, page, inq, true);
	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_INQUIRYPAGE));
	return ret;
}

/**
 * Get inquiry data
 * @param inq pointer to inquiry data. This function will update this value
 * @return 0 on success or negative value on error
 */
int camtape_inquiry(void *device, struct tc_inq *inq)
{
	int rc = DEVICE_GOOD;
	struct scsi_inquiry_data *inq_data;
	struct camtape_data *softc= (struct camtape_data *)device;
	int vendor_length;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_INQUIRY));
	if (softc->cd == NULL) {
		rc = -EDEV_INVALID_ARG;
		goto bailout;
	}

	inq_data = &softc->cd->inq_data;
	inq->devicetype = SID_TYPE(inq_data);
	inq->cmdque = (inq_data->flags & SID_CmdQue) ? 1 : 0;
	strncpy((char *)inq->vid, (char *)inq_data->vendor, 8);
	inq->vid[8] = '\0';
	strncpy((char *)inq->pid, (char *)inq_data->product, 16);
	inq->pid[16] = '\0';
	strncpy((char *)inq->revision, (char *)inq_data->revision, 4);
	inq->revision[4] = '\0';

	vendor_length = 20;
	if (IS_ENTERPRISE(softc->drive_type))
		vendor_length = 18;

	strncpy((char *)inq->vendor, (char *)inq_data->vendor_specific0,
		vendor_length);
	inq->vendor[vendor_length] = '\0';

bailout:
	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_INQUIRY));
	return rc;
}

/**
 * Request Sense
 * @param device a pointer to the camtape backend
 * @param sense pointer to sense data.  This function will update this value if successful.
 * @param alloc_sense_len length of sense data allocated
 * @param valid_sense_len length of sense data returned, this may be longer than alloc_sense_len
 * @return 0 for success, negative number for failure
 */
int camtape_request_sense(void *device, struct scsi_sense_data *sense, int alloc_sense_len,
    int *valid_sense_len)
{
	int rc = DEVICE_GOOD;
	char *msg;
	struct camtape_data *softc = (struct camtape_data *)device;
	struct scsi_sense_data sense_data;
	union ccb *ccb = NULL;

	ccb = cam_getccb(softc->cd);
	if (ccb == NULL) {
		rc = -EDEV_NO_MEMORY;
		goto bailout;
	}

	memset(&sense_data, 0, sizeof(sense_data));

	scsi_request_sense(&ccb->csio,
					   /*retries*/ 0,
					   /*cbfcnp*/ NULL,
					   /*data_ptr*/ &sense_data,
					   /*dxfer_len*/ sizeof(sense_data),
					   /*tag_action*/ MSG_SIMPLE_Q_TAG,
					   /*sense_len*/ SSD_FULL_SIZE,
					   /*timeout*/ 90000);

	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	rc = camtape_send_ccb(softc, ccb, &msg);

	if (rc == DEVICE_GOOD) {
		*valid_sense_len = ccb->csio.dxfer_len - ccb->csio.resid;
		memcpy(sense, &sense_data, MIN(alloc_sense_len, *valid_sense_len));
	}
bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);

	return rc;
}


/**
 * Test Unit Ready
 * @param device a pointer to the camtape backend
 * @param inq pointer to inquiry data. This function will update this value
 * @return 0 on success or negative value on error
 */
int camtape_test_unit_ready(void *device)
{
	char *msg;
	int rc = DEVICE_GOOD;
	bool take_dump = true, print_message = true;
	struct camtape_data *softc = (struct camtape_data *)device;
	int timeout;
	union ccb *ccb = NULL;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_TUR));
	ltfsmsg(LTFS_DEBUG3, 31392D, "test unit ready",
			softc->drive_serial);

	timeout = camtape_get_timeout(softc->timeouts, TEST_UNIT_READY);
	if (timeout < 0) {
		rc = -EDEV_UNSUPPORETD_COMMAND;
		goto bailout;
	}

	ccb = cam_getccb(softc->cd);
	if (ccb == NULL) {
		rc = -EDEV_NO_MEMORY;
		goto bailout;
	}

	memset(&(&ccb->ccb_h)[1], 0,
		sizeof(struct ccb_scsiio) - sizeof(struct ccb_hdr));

	scsi_test_unit_ready(&ccb->csio,
						 /*retries*/ 1,
						 /*cbfcnp*/ NULL,
						 /*tag_action*/ MSG_SIMPLE_Q_TAG,
						 /*sense_len*/ SSD_FULL_SIZE,
						 /*timeout*/ timeout * 1000);

	/*
	 * XXX KDM enabling error recovery here, so the retry count will work.
	 * Not sure whether this is the correct thing to do.
	 */
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS | CAM_PASS_ERR_RECOVER;

	rc = camtape_send_ccb(softc, ccb, &msg);

	if (rc != DEVICE_GOOD) {
		switch (rc) {
		case -EDEV_NEED_INITIALIZE:
		case -EDEV_CONFIGURE_CHANGED:
		case -EDEV_OPERATION_IN_PROGRESS:
			print_message = false;
			/* Fall through */
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
		if (print_message)
			camtape_process_errors(device, rc, msg, "test unit ready", take_dump);
	}

bailout:

	if (ccb != NULL)
		cam_freeccb(ccb);

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_TUR));
	return rc;
}

/**
 * Reserve the unit
 * @param device a pointer to the camtape backend
 * @param lun lun to reserve
 * @return 0 on success or a negative value on error
 */
int camtape_reserve_unit(void *device)
{
	struct camtape_data *softc = (struct camtape_data *)device;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_RESERVEUNIT));
	ltfsmsg(LTFS_DEBUG, 31392D, "reserve unit (6)", softc->drive_serial);
	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_RESERVEUNIT));
	/*
	 * The FreeBSD tape driver issues a RESERVE UNIT command during the open process.  So a
	 * separate reserve is not needed.
	 */
	return DEVICE_GOOD;
}

/**
 * Release the unit
 * @param device a pointer to the camtape backend
 * @param lun lun to release
 * @return 0 on success or a negative value on error
 */
int camtape_release_unit(void *device)
{
	struct camtape_data *softc = (struct camtape_data *)device;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_RELEASEUNIT));
	ltfsmsg(LTFS_DEBUG, 31392D, "release unit (6)", softc->drive_serial);
	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_RELEASEUNIT));
	/*
	 * The FreeBSD tape driver issues a RELEASE UNIT during the close process.  So a separate
	 * release is not needed.
	 */
	return DEVICE_GOOD;
}

/**
 * Read buffer
 * @param device a pointer to the camtape backend
 * @param id
 * @param buf
 * @param offset
 * @param len
 * @param type
 * @return 0 on success or negative value on error
 */
int camtape_readbuffer(struct camtape_data *softc, int id, unsigned char *buf, size_t offset,
					   size_t len, int type)
{
	char *msg;
	int rc = DEVICE_GOOD;
	union ccb *ccb;
	int timeout;

	ltfsmsg(LTFS_DEBUG, 31393D, "read buffer", id, softc->drive_serial);

	ccb = cam_getccb(softc->cd);
	if (ccb == NULL) {
		rc = -EDEV_NO_MEMORY;
		goto bailout;
	}

	/* cam_getccb cleans up the header, caller has to zero the payload */
	memset(&(&ccb->ccb_h)[1], 0,
		  sizeof(struct ccb_scsiio) - sizeof(struct ccb_hdr));

	timeout = camtape_get_timeout(softc->timeouts, READ_BUFFER);
	if (timeout < 0) {
		rc = -EDEV_UNSUPPORETD_COMMAND;
		goto bailout;
	}

	scsi_read_buffer(&ccb->csio,
					 /*retries*/ 1,
					 /*cbfcnp*/ NULL,
					 /*tag_action*/ MSG_SIMPLE_Q_TAG,
					 /*mode*/ type,
					 /*buffer_id*/ id,
					 /*offset*/ offset,
					 /*data_ptr*/ buf,
					 /*allocation_length*/ len,
					 /*sense_len*/ SSD_FULL_SIZE,
					 /*timeout*/ timeout);

	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS | CAM_PASS_ERR_RECOVER;

	rc = camtape_send_ccb(softc, ccb, &msg);

	if (rc != DEVICE_GOOD)
		camtape_process_errors(softc, rc, msg, "read buffer", false);

bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);

	return rc;
}

/**
 * Take drive dump
 * @param device a pointer to the camtape backend
 * @param fname a file name of dump
 * @return 0 on success or negative value on error
 */
#define DUMP_HEADER_SIZE   (4)
#define DUMP_TRANSFER_SIZE (512 * KB)

int camtape_getdump_drive(void *device, const char *fname)
{
	struct camtape_data *softc = (struct camtape_data *)device;
	long long data_length, buf_offset;
	int dumpfd = -1;
	int transfer_size, num_transfers, excess_transfer;
	int rc = 0;
	int i, bytes;
	int buf_id;
	unsigned char cap_buf[DUMP_HEADER_SIZE];
	unsigned char *dump_buf;

	ltfsmsg(LTFS_INFO, 31278I, fname);

	/* Set transfer size */
	transfer_size = DUMP_TRANSFER_SIZE;
	dump_buf = calloc(1, DUMP_TRANSFER_SIZE);
	if (dump_buf == NULL) {
		ltfsmsg(LTFS_ERR, 10001E, "camtape_getdump_drive: dump buffer");
		return -EDEV_NO_MEMORY;
	}

	/* Set buffer ID */
	if (IS_ENTERPRISE(softc->drive_type)) {
		buf_id = 0x00;
	} else {
		buf_id = 0x01;
	}

	/* Get buffer capacity */
	camtape_readbuffer(device, buf_id, cap_buf, 0, sizeof(cap_buf), 0x03);
	data_length = (cap_buf[1] << 16) + (cap_buf[2] << 8) + (int) cap_buf[3];

	/* Open dump file for write only */
	dumpfd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (dumpfd < 0) {
		rc = -errno;
		ltfsmsg(LTFS_WARN, 31279W, rc);
		free(dump_buf);
		return rc;
	}

	/* get the total number of transfers */
	num_transfers = data_length / transfer_size;
	excess_transfer = data_length % transfer_size;
	if (excess_transfer)
		num_transfers += 1;

	/* Total dump data length is %lld. Total number of transfers is %d. */
	ltfsmsg(LTFS_DEBUG, 31280D, data_length);
	ltfsmsg(LTFS_DEBUG, 31281D, num_transfers);

	/* start to transfer data */
	buf_offset = 0;
	i = 0;
	ltfsmsg(LTFS_DEBUG, 31282D);
	while (num_transfers) {
		int length;

		i++;

		/* Allocation Length is transfer_size or excess_transfer */
		if (excess_transfer && num_transfers == 1)
			length = excess_transfer;
		else
			length = transfer_size;

		rc = camtape_readbuffer(device, buf_id, dump_buf, buf_offset, length, 0x02);
		if (rc) {
			ltfsmsg(LTFS_WARN, 31283W, rc);
			free(dump_buf);
			close(dumpfd);
			return rc;
		}

		/* write buffer data into dump file */
		bytes = write(dumpfd, dump_buf, length);
		if (bytes == -1) {
			rc = -errno;
			ltfsmsg(LTFS_WARN, 31284W, rc);
			free(dump_buf);
			close(dumpfd);
			return rc;
		}

		ltfsmsg(LTFS_DEBUG, 31285D, i, bytes);
		if (bytes != length) {
			ltfsmsg(LTFS_WARN, 31286W, bytes, length);
			free(dump_buf);
			close(dumpfd);
			return -EDEV_DUMP_EIO;
		}

		/* update offset and num_transfers, free buffer */
		buf_offset += transfer_size;
		num_transfers -= 1;
	}							/* end of while(num_transfers) */

	free(dump_buf);
	close(dumpfd);

	return rc;
}

/**
 * Force Dump for Drive
 * @param device a pointer to the camtape backend
 * @return 0 on success or negative value on error
 */
#define SENDDIAG_BUF_LEN (8)
int camtape_forcedump_drive(struct camtape_data *softc)
{
	union ccb *ccb = NULL;
	unsigned char buf[SENDDIAG_BUF_LEN];
	int rc = DEVICE_GOOD;
	char *msg;
	int timeout;

	ltfsmsg(LTFS_DEBUG, 31393D, "force dump", 0, softc->drive_serial);

	ccb = cam_getccb(softc->cd);
	if (ccb == NULL) {
		rc = -EDEV_NO_MEMORY;
		goto bailout;
	}

	memset(buf, 0, sizeof(buf));

	/* Prepare payload */
	buf[0] = 0x80;		/* page code */
	buf[2] = 0x00;
	buf[3] = 0x04;		/* page length */
	buf[4] = 0x01;
	buf[5] = 0x60;		/* diagnostic id */

	timeout = camtape_get_timeout(softc->timeouts, SEND_DIAGNOSTIC);
	if (timeout < 0) {
		rc = EDEV_UNSUPPORETD_COMMAND;
		goto bailout;
	}
	scsi_send_diagnostic(&ccb->csio,
						 /*retries*/ 1,
						 /*cbfcnp*/ NULL,
						 /*tag_action*/ MSG_SIMPLE_Q_TAG,
						 /*unit_offline*/ 0,
						 /*device_offline*/ 0,
						 /*self_test*/ 0,
						 /*page_format*/ 1,
						 /*self_test_code*/ SSD_SELF_TEST_CODE_NONE,
						 /*data_ptr*/ buf,
						 /*param_list_length*/ sizeof(buf),
						 /*sense_len*/ SSD_FULL_SIZE,
						 /*timeout*/ timeout);

	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS | CAM_PASS_ERR_RECOVER;

	rc = camtape_send_ccb(softc, ccb, &msg);

	if (rc != DEVICE_GOOD)
		camtape_process_errors(softc, rc, msg, "force dump", false);

bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);

	return rc;
}

/**
 * Take normal drive dump and forces drive dump
 * @param device a pointer to the camtape backend
 * @return 0 on success or negative value on error
 */
int camtape_takedump_drive(void *device, bool nonforced_dump)
{
	char fname_base[1024];
	char fname[1024];
	time_t now;
	struct tm *tm_now;
	struct camtape_data *softc = (struct camtape_data *)device;
	unsigned char *serial = softc->drive_serial;

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_TAKEDUMPDRV));
	/* Make base filename */
	time(&now);
	tm_now = localtime(&now);
	sprintf(fname_base, "%s/ltfs_%s_%d_%02d%02d_%02d%02d%02d", ltfs_dump_dir, serial, tm_now->tm_year + 1900,
			tm_now->tm_mon + 1, tm_now->tm_mday, tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);

	if (nonforced_dump) {
		strcpy(fname, fname_base);
		strcat(fname, ".dmp");

		ltfsmsg(LTFS_INFO, 31287I);
		camtape_getdump_drive(softc, fname);
	}

	ltfsmsg(LTFS_INFO, 31288I);
	camtape_forcedump_drive(device);
	strcpy(fname, fname_base);
	strcat(fname, "_f.dmp");
	camtape_getdump_drive(device, fname);

	ltfs_profiler_add_entry(softc->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_TAKEDUMPDRV));

	return 0;
}

/**
 * Get the serial number of the changer device.
 * @param device Handle of the changer device to get the serial number for
 * @param[out] result On success, contains the serial number of the changer. The memory
 *             is allocated on demand and must be freed by the caller once it's gone using it.
 * @return 0 on success or a negative value on error.
 */
int camtape_get_serialnumber(void *device, char **result)
{
	struct camtape_data *softc = (struct camtape_data *) device;

	CHECK_ARG_NULL(device, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(result, -LTFS_NULL_ARG);
	ltfs_profiler_add_entry(softc->profiler, NULL, CHANGER_REQ_ENTER(REQ_TC_GETSER));

	*result = strdup((const char *) softc->drive_serial);
	if (! *result) {
		ltfsmsg(LTFS_ERR, 10001E, "camtape_get_serialnumber: result");
		ltfs_profiler_add_entry(softc->profiler, NULL, CHANGER_REQ_EXIT(REQ_TC_GETSER));
		return -EDEV_NO_MEMORY;
	}

	ltfs_profiler_add_entry(softc->profiler, NULL, CHANGER_REQ_EXIT(REQ_TC_GETSER));
	return 0;
}

/**
 * Enable profiler function
 * @param device a pointer to the tape device
 * @param work_dir work directory to store profiler data
 * @paran enable enable or disable profiler function of this backend
 * @return 0 on success or a negative value on error
 */
int camtape_set_profiler(void *device, char *work_dir, bool enable)
{
	int rc = 0;
	char *path;
	FILE *p;
	struct timer_info timerinfo;
	struct camtape_data *softc = (struct camtape_data *)device;

	if (enable) {
		if (softc->profiler)
			return 0;
		if (!work_dir)
			return -LTFS_BAD_ARG;

		rc = asprintf(&path, "%s/%s%s%s", work_dir, DRIVER_PROFILER_BASE,
					  softc->drive_serial, PROFILER_EXTENSION);
		if (rc < 0) {
			ltfsmsg(LTFS_ERR, 10001E, __FILE__);
			return -LTFS_NO_MEMORY;
		}

		p = fopen(path, PROFILER_FILE_MODE);

		free(path);

		if (! p)
			rc = -LTFS_FILE_ERR;
		else {
			get_timer_info(&timerinfo);
			fwrite((void*)&timerinfo, sizeof(timerinfo), 1, p);
			softc->profiler = p;
			rc = 0;
		}
	} else {
		if (softc->profiler) {
			fclose(softc->profiler);
			softc->profiler = NULL;
		}
	}

	return rc;
}

int camtape_get_timeout(struct timeout_tape *table, int op_code)
{
	int ret;

	ret = ibm_tape_get_timeout(table, op_code);
	if (ret < 0)
		return ret;
	else
		return (ret * 1000);
}
