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
** FILE NAME:       tape_drivers/linux/sg-ibmtape/sg_scsi_tape.h
**
** DESCRIPTION:     Defines SCSI command handling in sg driver
**
** AUTHORS:         Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/

#include <scsi/sg.h>

#include "tape_drivers/tape_drivers.h"

#ifndef __sg_scsi_tape_h
#define __sg_scsi_tape_h

#define MAX_INQ_LEN        (255)

#define SIZE_OF_SENSE_STRING       (256)
#define COMMAND_DESCRIPTION_LENGTH (32)

#define CDB6_LEN    (6)
#define CDB10_LEN   (10)
#define CDB12_LEN   (12)
#define CDB16_LEN   (16)

#define SCSI_FROM_INITIATOR_TO_TARGET (SG_DXFER_TO_DEV)
#define SCSI_FROM_TARGET_TO_INITIATOR (SG_DXFER_FROM_DEV)
#define SCSI_NO_DATA_TRANSFER         (SG_DXFER_NONE)

#define SCSI_GOOD                 (0x00)
#define SCSI_CHECK_CONDITION      (0x01)
#define SCSI_BUSY                 (0x04)
#define SCSI_RESERVATION_CONFLICT (0x0c)

#define SK_ILI_SET (0x20)
#define SK_FM_SET  (0x80)

#define PERIPHERAL_MASK   (0x1F)
#define SEQUENTIAL_DEVICE (0x01)

#define MILLISEC_CONVERSION  (1000) /* Sec to millisec conversion */
#define SGConversion(sec) (sec * MILLISEC_CONVERSION)

struct sg_tape
{
	int  fd;
	bool is_data_key_set;      /**< Is a valid data key set? */
};

typedef struct _scsi_device_identifier {
	char vendor_id[VENDOR_ID_LENGTH + 1];
	char product_id[PRODUCT_ID_LENGTH + 1];
	char product_rev[PRODUCT_REV_LENGTH + 1];
	char unit_serial[UNIT_SERIAL_LENGTH + 1];
} scsi_device_identifier;

extern struct error_table *standard_table;
extern struct error_table *vendor_table;

static inline int init_sg_io_header(sg_io_hdr_t *req)
{
	CHECK_ARG_NULL(req, -LTFS_NULL_ARG);

	memset(req, 0, sizeof(sg_io_hdr_t));

	req->interface_id = 'S';
	req->flags        = SG_FLAG_LUN_INHIBIT;

	return 0;
}

int sg_issue_cdb_command(struct sg_tape *device, sg_io_hdr_t *req, char **msg);
int sg_get_drive_identifier(struct sg_tape *device, scsi_device_identifier *id_data);

#endif // _sg_scsi_tape_h
