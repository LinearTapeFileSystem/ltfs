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
** FILE NAME:       tape_drivers/osx/iokit/iokit_scsi.h
**
** DESCRIPTION:     Defines API for iokit SCSI service functions
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

#include "tape_drivers/tape_drivers.h"

#ifndef __iokit_scsi_h
#define __iokit_scsi_h

#define MAX_INQ_LEN        (255)

#define SIZE_OF_SENSE_STRING       (256)
#define COMMAND_DESCRIPTION_LENGTH (32)

#define CDB6_LEN    (6)
#define CDB10_LEN   (10)
#define CDB12_LEN   (12)
#define CDB16_LEN   (16)

#define SCSI_FROM_INITIATOR_TO_TARGET (kSCSIDataTransfer_FromInitiatorToTarget)
#define SCSI_FROM_TARGET_TO_INITIATOR (kSCSIDataTransfer_FromTargetToInitiator)
#define SCSI_NO_DATA_TRANSFER         (kSCSIDataTransfer_NoDataTransfer)

#define MILLISEC_CONVERSION  (1000) /* Sec to millisec conversion */
#define IOKitConversion(sec) (sec * MILLISEC_CONVERSION)

struct iokit_scsi_request
{
	int             dxfer_direction; /**< [I] Data direction           */
	unsigned char   cmd_len;         /**< [I] CDB length               */
	unsigned char   mx_sb_len;       /**< [I] Max sense buffer         */
	unsigned int    dxfer_len;       /**< [I] Transfer length          */
	unsigned char   *dxferp;         /**< [I] Pointer to data buffer   */
	unsigned char   *cmdp;           /**< [I] Pointer to CDB           */
	SCSI_Sense_Data sense_buffer;    /**< [I] Pointer to sense buffer  */
	unsigned int    timeout;         /**< [I] Timeout (ms)             */
	SCSITaskStatus  status;          /**< [O] SCSI status              */
	unsigned char   sb_len_wr;       /**< [O] Byte count actually written to sbp */
	unsigned int    actual_xfered;   /**< [O] actual_transferred length */
	int             resid;           /**< [O] dxfer_len - actual_transferred */
	char            *desc;           /**< [I] Command Description      */
};

typedef struct _scsi_device_identifier {
	char vendor_id[VENDOR_ID_LENGTH + 1];
	char product_id[PRODUCT_ID_LENGTH + 1];
	char product_rev[PRODUCT_REV_LENGTH + 1];
	char unit_serial[UNIT_SERIAL_LENGTH + 1];
} scsi_device_identifier;

extern struct error_table *standard_table;
extern struct error_table *vendor_table;

int iokit_issue_cdb_command(struct iokit_device *device,
							struct iokit_scsi_request *req,
							char **msg);

int iokit_get_drive_identifier(struct iokit_device *device,
							   scsi_device_identifier *id_data);

#endif // _iokit_scsi_h
