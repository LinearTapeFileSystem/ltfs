/*
**
**  OO_Copyright_BEGIN
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
**  OO_Copyright_END
**
*************************************************************************************
**
** COMPONENT NAME:  IBM Linear Tape File System
**
** FILE NAME:       tape_drivers/freebsd/cam/camtape_cmn.h
**
** DESCRIPTION:     Implements FreeBSD CAM backend for LTFS.
**
** AUTHORS:         Ken Merry
**                  Spectra Logic Corporation
**                  ken@FreeBSD.ORG, kenm@spectralogic.com
**
*************************************************************************************
*/


#ifndef __camtape_cmn_h
#define __camtape_cmn_h

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/mtio.h>
#include <sys/ioctl.h>
#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_pass.h>
#include <cam/scsi/scsi_sa.h>
#include <camlib.h>
#include <err.h>

#include "libltfs/tape_ops.h"
#include "libltfs/ltfs_error.h"
#include "libltfs/ltfs_endian.h"
#include "tape_drivers/tape_drivers.h"
#undef MAXSENSE
#include "IBM_tape.h"
#include "libltfs/ltfstrace.h"

/*
 * Mode page 0x24.  This is called "Initiator-Specific Extensions" mode page in the IBM TS spec,
 * and "Vendor-Specific Speed Matching Control" mode page in the LTO spec.  The IBM LTO spec
 * only documents 8 bytes (page length field is 6) in the mode page, and that matches what an
 * IBM LTO-6 drive reports at least.  The IBM TS spec documents 24 bytes in the mode page (the
 * page length field is 22).
 */
struct camtape_ibm_initiator_spec_ext_page {
	uint8_t page_code;
#define	CT_ISE_PAGE_CODE						0x24
	uint8_t page_length;
	uint8_t crc_target_support;
#define	CT_ISE_IEEE_CRC_SUPPORT					0x80
#define	CT_ISE_DEVSPEC_CRC_SUPPORT				0x40
	uint8_t crc_target_enablement;
#define	CT_ISE_CRC_DISABLED						0x00
#define	CT_ISE_IEEE_CRC_ENABLED					0x01
#define	CT_ISE_DEVSPEC_CRC_ENABLED				0x02
	uint8_t crc_placement_length;
#define	CT_ISE_CRC_PLACEMENT_MASK				0xc0
#define	CT_ISE_CRC_APPEND						0x80
#define	CT_ISE_CRC_PREFIX						0x40
#define	CT_ISE_CRC_LENGTH_MASK					0x3f
	uint8_t crc_scope;
#define	CT_ISE_CRC_READ_DATA_CHECKED			0x80
#define	CT_ISE_CRC_WRITE_DATA_CHECKED			0x40
#define	CT_ISE_CRC_PARAM_READ_DATA_CHECKED		0x20
#define	CT_ISE_CRC_PARAM_WRITE_DATA_CHECKED		0x10
#define	CT_ISE_CRC_CDB_CHECKED					0x08
#define	CT_ISE_CRC_RDB_DATA_CHECKED				0x04
	uint8_t crc_characteristics;
#define	CT_ISE_CRC_CDB_LENGTH_INCLUDES_CRC		0x80
#define	CT_ISE_CRC_ENDIAN_BIG					0x40
#define	CT_ISE_CRC_READ_REPORTING				0x20
#define	CT_ISE_CRC_WRITE_REPORTING				0x10
#define	CT_ISE_CRC_WRITE_IMMED_CHECK			0x08
	uint8_t support_flags;
#define	CT_ISE_PARTITION_SUPPORT				0x80
#define	CT_ISE_PERF_SEGMENT_SCALING				0x40
#define	CT_ISE_CAPACITY_SCALING					0x20
#define	CT_ISE_WORM_SUPPORT						0x10
#define	CT_ISE_ENCRYPTION_ENABLED				0x08
#define	CT_ISE_FIPS								0x02
#define	CT_ISE_ENCRYPTION_CAPABLE				0x01
	/*
	 * The following fields are only defined in the TS spec.
	 */
	uint8_t vendor_reserved1[5];
	uint8_t transfer_period;
	uint8_t req_ack_offset;
	uint8_t buf_assocation_enablement;
#define	CT_ISE_MANUAL_UNLOAD_ASSOC_ENABLED		0x80
#define	CT_ISE_MANUAL_REWIND_ASSOC_ENABLED		0x40
#define	CT_ISE_UNLOAD_WRITE_ERROR_ASSOC_ENABLED	0x20
	uint8_t vendor_reserved2[8];
};

/*
 * Mode page 0x25, the Read/Write Control page.  This page is undocumented in IBM's LTO spec,
 * but is somewhat documented in IBM's TS spec.
 */
struct camtape_ibm_rw_control_page {
	uint8_t page_code;
#define	CT_RWC_PAGE_CODE						0x25
	uint8_t page_length;
	uint8_t ignore_sequence_checks;
#define	CT_RWC_LOCATE_IGNORE_SEQ_CHECKS			0x04
#define	CT_RWC_SPACE_BLK_IGNORE_SEQ_CHECKS		0x02
#define	CT_RWC_SPACE_FILE_IGNORE_SEQ_CHECKS		0x01
	uint8_t ignore_data_checks;
#define	CT_RWC_LOCATE_IGNORE_DATA_CHECKS		0x04
#define	CT_RWC_SPACE_BLK_IGNORE_DATA_CHECKS		0x02
#define	CT_RWC_SPACE_FILE_IGNORE_DATA_CHECKS	0x01
	uint8_t reserved1;
	uint8_t leop_method;
#define	CT_RWC_LEOP_DENSITY_SPECIFIC			0x00
#define	CT_RWC_LEOP_MAX_CAPACITY				0x01
#define	CT_RWC_LEOP_CONSTANT_CAPACITY			0x02
	uint8_t leop_ew_mbytes[2];
	uint8_t byte8;
#define	CT_RWC_FASTSYNC_DISABLE					0x80
#define	CT_RWC_SKIPSYNC_DISABLE					0x40
#define	CT_RWC_CROSSING_EOD_DISABLE				0x08
#define	CT_RWC_CROSSING_PERM_ERR_DISABLE		0x04
#define	CT_RWC_REPORT_SEG_EW					0x02
#define	CT_RWC_REPORT_HOUSEKEEPING_ERR			0x01
	uint8_t default_write_density_bop;
	uint8_t pending_write_density_bop;
	uint8_t reserved2[5];
	uint8_t reserved3[4];
	uint8_t encryption_state;
#define	CT_RWC_ENCRYPTION_STATE_MASK			0x03
#define	CT_RWC_ENCRYPTION_STATE_OFF				0x00
#define	CT_RWC_ENCRYPTION_STATE_ON				0x01
#define	CT_RWC_ENCRYPTION_STATE_NA				0x02
#define	CT_RWC_ENCRYPTION_STATE_UNKNOWN			0x03
	uint8_t keypath_configured;
	/*
	 * These values are undocumented and somewhat obfuscated in the driver.  The test to see
	 * whether the encryption keypath is configured is:
	 *
	 * is_keypath_config  = (((keypath_configured & CT_RWC_KP_MASK_1) == CT_RWC_KP_VALUE_1 &&
	 *						  (keypath_configured & CT_RWC_KP_MASK_2) == CT_RWC_KP_VALUE_2) ||
	 * 						  (keypath_configured & CT_RWC_KP_MASK_2) == CT_RWC_KP_VALUE_3);
	 */
#define	CT_RWC_KP_MASK_1						0xe0
#define	CT_RWC_KP_VALUE_1						0x20
#define	CT_RWC_KP_MASK_2						0x1c
#define	CT_RWC_KP_VALUE_2						0x00
#define	CT_RWC_KP_VALUE_3						0x04
	uint8_t reserved4[5];
	uint8_t encryption_method;
#define	CT_RWC_ENC_METHOD_NONE					0x00
#define	CT_RWC_ENC_METHOD_SYSTEM				0x10
#define	CT_RWC_ENC_METHOD_APPLICATION			0x50
#define	CT_RWC_ENC_METHOD_LIBRARY				0x60
#define	CT_RWC_ENC_METHOD_INTERNAL				0x70
#define	CT_RWC_ENC_METHOD_CONTROLLER			0x1f
#define	CT_RWC_ENC_METHOD_CUSTOM				0xff
	uint8_t reserved5[4];
};

/*
 * This is mode page 0x25, subpage 0xc0, which controls encryption on IBM tape drives.  It is
 * left out of the LTO drive spec completely.  In the TS spec, it is lumped into a bunch of IBM
 * proprietary encryption parameters that you need to ask IBM about.
 *
 * The values here were obtained from the tape_set_data_key() function in lin_tape_ioctl_tape.c
 * in the IBM Linux tape driver.  They are obfuscated there, thus the very generic names here.
 * If there isn't a value for a particular byte, it is assumed to be 0.
 */
struct camtape_ibm_enc_param_subpage {
	uint8_t page_code;
#define	CT_ENC_PARAM_SUBPAGE_PAGE_CODE			CT_RWC_PAGE_CODE
	uint8_t subpage_code;
#define	CT_ENC_PARAM_SUBPAGE_CODE				0xc0
	uint8_t page_length[2];
#define	CT_ENC_PARAM_NO_KI_EXTRA_LENGTH			132
#define	CT_ENC_PARAM_KI_EXTRA_LENGTH			144
	uint8_t desc1[3];
#define	CT_ENC_PARAM_DESC_1_BYTE_0_VAL			0x65
#define	CT_ENC_PARAM_DESC_1_BYTE_1_VAL			0xe0
	uint8_t desc1_length;
#define	CT_ENC_PARAM_DESC_1_ADDL_LENGTH_SUB		4
	uint8_t desc2[3];
	uint8_t desc2_length;
#define	CT_ENC_PARAM_DESC_2_ADDL_LENGTH_SUB		8
	uint8_t desc3[3];
	uint8_t desc3_length;
#define	CT_ENC_PARAM_DESC_3_ADDL_LENGTH_SUB		16
	uint8_t desc4[59];
#define	CT_ENC_PARAM_DESC_4_BYTE_57_VAL			0x21
#define	CT_ENC_PARAM_DESC_4_BYTE_58_VAL			0xe0;
	uint8_t desc4_length;
#define	CT_ENC_PARAM_DESC_4_ADDL_LENGTH_SUB		76
	uint8_t byte76;
#define	CT_ENC_PARAM_BYTE_76_MASK				0x78
	uint8_t byte77;
	uint8_t byte78;
	uint8_t byte79;
#define	CT_ENC_PARAM_BYTE_79_VALUE				0x01
	uint8_t byte80;
#define	CT_ENC_PARAM_BYTE_80_VALUE				0x11;
	uint8_t byte81;
	uint8_t byte82;
	uint8_t byte83;
#define	CT_ENC_PARAM_BYTE_83_VALUE				0x20
#define	CT_ENC_PARAM_DATA_KEY_LEN				32
	uint8_t data_key[CT_ENC_PARAM_DATA_KEY_LEN];
	uint8_t byte116;
#define	CT_ENC_PARAM_BYTE_116_VALUE				0x18
	uint8_t byte117;
	uint8_t byte118;
	uint8_t byte119;
#define	CT_ENC_PARAM_BYTE_119_VALUE_1			0x14
#define	CT_ENC_PARAM_BYTE_119_VALUE_2			0x08
	uint8_t byte120;
	uint8_t byte121;
#define	CT_ENC_PARAM_BYTE_121_VALUE				0x02
	uint8_t byte122;
	uint8_t byte123;
	uint8_t byte124;
#define	CT_ENC_PARAM_BYTE_124_VALUE				0x1a
	uint8_t byte125;
	uint8_t byte126;
	union ki_or_no_ki {
		struct ki_set {
			uint8_t byte127;
#define	CT_ENC_PARAM_BYTE127_KI_VALUE			0x0c
#define	CT_ENC_PARAM_KEY_INDEX_LEN				12
			uint8_t key_index[CT_ENC_PARAM_KEY_INDEX_LEN];
			uint8_t byte140;
			uint8_t byte141;
			uint8_t byte142;
			uint8_t byte143;
			uint8_t byte144;
#define	CT_ENC_PARAM_BYTE144_KI_VALUE			0x04
			uint8_t byte145;
			uint8_t byte146;
			uint8_t byte147;
		} ki_is_set;
		struct no_ki_set {
			uint8_t byte127;
			uint8_t byte128;
			uint8_t byte129;
			uint8_t byte130;
			uint8_t byte131;
			uint8_t byte132;
#define	CT_ENC_PARAM_BYTE132_NO_KI_VALUE		0x04
			uint8_t byte133;
			uint8_t byte134;
			uint8_t byte135;
		} ki_not_set;
	} ki_or_not;
};

typedef enum {
	CT_ENC_CAPABLE,
	CT_ENC_NOT_CAPABLE
} camtape_encryption_capable;

typedef enum {
	CT_ENC_METHOD_NONE			= CT_RWC_ENC_METHOD_NONE,
	CT_ENC_METHOD_SYSTEM		= CT_RWC_ENC_METHOD_SYSTEM,
	CT_ENC_METHOD_APPLICATION	= CT_RWC_ENC_METHOD_APPLICATION,
	CT_ENC_METHOD_LIBRARY		= CT_RWC_ENC_METHOD_LIBRARY,
	CT_ENC_METHOD_INTERNAL		= CT_RWC_ENC_METHOD_INTERNAL,
	CT_ENC_METHOD_CONTROLLER	= CT_RWC_ENC_METHOD_CONTROLLER,
	CT_ENC_METHOD_CUSTOM		= CT_RWC_ENC_METHOD_CUSTOM,
	CT_ENC_METHOD_UNKNOWN
} camtape_encryption_method;

typedef enum {
	CT_ENC_STATE_OFF			= CT_RWC_ENCRYPTION_STATE_OFF,
	CT_ENC_STATE_ON				= CT_RWC_ENCRYPTION_STATE_ON,
	CT_ENC_STATE_NA				= CT_RWC_ENCRYPTION_STATE_NA,
	CT_ENC_STATE_UNKNOWN		= CT_RWC_ENCRYPTION_STATE_UNKNOWN
} camtape_encryption_state;

struct camtape_encryption_status {
	camtape_encryption_capable	encryption_capable;
	camtape_encryption_method	encryption_method;
	camtape_encryption_state	encryption_state;
};

/**
 *  Status definitions of lower scsi handling code
 *  This statuses are exposed into SIOC pass through interface
 */
enum scsi_status {
	SCSI_GOOD                 = 0x00,
	SCSI_CHECK_CONDITION      = 0x01,
	SCSI_CONDITION_GOOD       = 0x02,
	SCSI_BUSY                 = 0x04,
	SCSI_INTERMEDIATE_GOOD    = 0x08,
	SCSI_INTERMEDIATE_C_GOOD  = 0x0a,
	SCSI_RESERVATION_CONFRICT = 0x0c,
};

enum host_status {
	HOST_GOOD       = 0x00,
	HOST_NO_CONNECT = 0x01,
	HOST_BUS_BUSY   = 0x02,
	HOST_TIME_OUT   = 0x03,
	HOST_BAD_TARGET = 0x04,
	HOST_ABORT      = 0x05,
	HOST_PARITY     = 0x06,
	HOST_ERROR      = 0x07,
	HOST_RESET      = 0x08,
	HOST_BAD_INTR   = 0x09,
};

enum driver_status {
	DRIVER_GOOD     = 0x00,
	DRIVER_BUSY     = 0x01,
	DRIVER_SOFT     = 0x02,
	DRIVER_MEDIA    = 0x03,
	DRIVER_ERROR    = 0x04,
	DRIVER_INVALID  = 0x05,
	DRIVER_TIMEOUT  = 0x06,
	DRIVER_HARD     = 0x07,
	DRIVER_SENSE    = 0x08,
	SUGGEST_RETRY   = 0x10,
	SUGGEST_ABORT   = 0x20,
	SUGGEST_REMAP   = 0x30,
	SUGGEST_DIE     = 0x40,
	SUGGEST_SENSE   = 0x80,
	SUGGEST_IS_OK   = 0xff,
};

/**
 * camtape private data structure. Shared by camtape_tc.c and camtape_cc.c
 */
struct itd_conversion_entry {
	uint16_t src_asc_ascq; /**< ASC/ASCQ receive from device */
	uint16_t dst_asc_ascq; /**< ASC/ASCQ converted */
};

enum _device_type {
	DEVICE_TAPE,
	DEVICE_CHANGER,
};

struct camtape_data {
	int           fd_sa;             /**< file descriptor of the SA device */
	struct cam_device *cd;           /**< CAM device for PT device, contains file descriptor of the PT device */
	bool          loaded;            /**< Is cartridge loaded? */
	bool          loadfailed;        /**< Is load/unload failed? */
	unsigned char drive_serial[255]; /**< serial number of device */
	int           drive_type;        /**< device type */
	int           itd_command_size;  /**< ITD sense conversion table size for commands */
	struct itd_conversion_entry *itd_command; /**< ITD sense conversion table for commands */
	int           itd_slot_size;     /**< ITD sense conversion table size for RES data */
	struct itd_conversion_entry *itd_slot;    /**< ITD sense conversion table for RES data */
	long          fetch_sec_acq_loss_w; /**< Sec to fetch Active CQs loss write */
	bool          dirty_acq_loss_w;  /**< Is Active CQs loss write dirty */
	float         acq_loss_w;        /**< Active CQs loss write */
	uint64_t      tape_alert;        /**< Latched tape alert flag */
	bool          is_data_key_set;   /**< Is a valid data key set? */
	unsigned char dki[12];           /**< key-alias */
	uint64_t      force_writeperm;   /**< pseudo write perm threshold */
	uint64_t      force_readperm;    /**< pseudo read perm threashold */
	uint64_t      write_counter;     /**< write call counter for pseudo write perm */
	uint64_t      read_counter;      /**< read call counter for pseudo write perm */
	int			  force_errortype;	 /**< 0 is R/W Perm, otherwise no sense */
	char          *devname;          /**< device name */
	bool          is_worm;           /**< Is worm cartridge loaded? */
	unsigned char cart_type;		 /**< Cartridge type in CM */
	unsigned char density_code;		 /**< Density code */
	crc_enc       f_crc_enc;         /**< Pointer to CRC encode function */
	crc_check     f_crc_check;       /**< Pointer to CRC encode function */
	struct timeout_tape *timeouts;	 /**< Timeout table */
	FILE*		  profiler;			 /**< The file pointer for profiler */

};

struct camtape_global_data {
	unsigned          disable_auto_dump; /**< Is auto dump disabled? */
	char              *str_crc_checking; /**< option string for crc_checking */
	unsigned          crc_checking;      /**< Is crc checking enabled? */
	unsigned          strict_drive;      /**< Is bar code length checked strictly? */
};

/**
 *  Macros
 */

#define MASK_WITH_SENSE_KEY    (0xFFFFFF)
#define MASK_WITHOUT_SENSE_KEY (0x00FFFF)

/*
 *  Function Prototypes
 */
extern int camtape_sense2rc(void *device, struct scsi_sense_data *sense, int sense_len);
extern int camtape_ccb2rc(struct camtape_data *softc, union ccb *ccb);
extern int camtape_ioctlrc2err(void *device, int fd, struct scsi_sense_data *sense,
							   int control_cmd, char **msg);
extern int _sioc_stioc_command(void *device, int cmd, char *cmd_name, void *param, char **msg);
extern int camtape_check_lin_tape_version(void);
extern int camtape_inquiry(void *device, struct tc_inq *inq);
extern int _camtape_inquiry_page(void *device, unsigned char page, struct tc_inq_page *inq, bool error_handle);
extern int camtape_inquiry_page(void *device, unsigned char page, struct tc_inq_page *inq);
extern int camtape_request_sense(void *device, struct scsi_sense_data *sense, int alloc_sense_len,
								 int *valid_sense_len);
extern int camtape_test_unit_ready(void *device);
extern int camtape_reserve_unit(void *device);
extern int camtape_release_unit(void *device);

extern int camtape_readbuffer(struct camtape_data *softc, int id, unsigned char *buf,
							  size_t offset, size_t len, int type);
extern int camtape_takedump_drive(void *device, bool nonforced_dump);
extern int camtape_get_serialnumber(void *device, char **result);
extern int camtape_set_profiler(void *device, char *work_dir, bool enable);
extern int camtape_get_timeout(struct timeout_tape *table, int op_code);

extern int camtape_logsense_page(struct camtape_data *softc, const uint8_t page, const uint8_t subpage,
								 unsigned char *buf, const size_t size);

/**
 * Get Dump
 * @param device a pointer to the camtape backend
 * @return none
 */
static inline void camtape_get_dump(void *device, bool nonforced_dump)
{
	camtape_takedump_drive(device, nonforced_dump);
	return;
}

/**
 * Convert sense code to ITD sense code
 * @param sense sense code packed to uint32_t
 * @param table pointer to itd conversion table
 * @param int the size of itd conversion table
 * @return LTFS internal error code
 */
static inline uint32_t camtape_conv_itd(uint32_t sense, struct itd_conversion_entry *table, int size)
{
	uint32_t ret = sense;
	uint16_t src = sense & 0xFFFF;
	int i;

	for(i = 0 ; i < size; i++) {
		if( src == table[i].src_asc_ascq )
			ret = (sense & 0xFF0000) + table[i].dst_asc_ascq;
	}

	return ret;
}

/**
 * Convert sense information to negative errno
 * @param spt pointer to sioc_pass_through
 * @return negative errno
 */
static inline int _sense2errcode(uint32_t sense, struct error_table *table, char **msg, uint32_t mask)
{
	int rc = -EDEV_UNKNOWN;
	int i;

	if (msg)
		*msg = NULL;

	if ( (sense & 0xFFFF00) == 0x044000 )
		sense = 0x044000;
	else if ( (sense & 0xFFF000) == 0x048000 ) /* 04/8xxx in TS3100/TS3200 */
		sense = 0x048000;
	else if ( (sense & 0xFFF000) == 0x0B4100 ) /* 0B/41xx in TS2900 */
		sense = 0x0B4100;

	if ( (sense & 0x00FF00) >= 0x008000 || (sense & 0x0000FF) >= 0x000080)
		rc = -EDEV_VENDOR_UNIQUE;

	i = 0;
	while (table[i].sense != 0xFFFFFF) {
		if((table[i].sense & mask) == (sense & mask)) {
			rc  = table[i].err_code;
			if (msg)
				*msg = strdup(table[i].msg);
			break;
		}
		i++;
	}

	if (table[i].err_code == -EDEV_RECOVERED_ERROR)
		rc = DEVICE_GOOD;
	else if (table[i].sense == 0xFFFFFF && table[i].err_code == rc && msg)
		*msg = strdup(table[i].msg);

	return rc;
}

/**
 * Send a CAM passthrough CCB and decode any errors.
 * @param softc pointer to camtape_data structre
 * @param ccb pointer to CAM CCB
 * @param msg pointer to a description of the error
 * @return internal error code defined in ltfs_error.h
 */
static inline int camtape_send_ccb(struct camtape_data *softc, union ccb *ccb, char **msg)
{
	int rc;

	*msg = NULL;
	if (cam_send_ccb(softc->cd, ccb) == -1) {
		char tmpstr[512];

		snprintf(tmpstr, sizeof(tmpstr), "cam_send_ccb() failed: %s", strerror(errno));
		*msg = strdup(tmpstr);
		rc = -errno;
	} else
		rc = camtape_ccb2rc(softc, ccb);

	if ((rc != DEVICE_GOOD) && (*msg == NULL)) {
		char *tmpstr;

		tmpstr = malloc(2048);
		if (tmpstr == NULL)
			goto bailout;

		cam_error_string(softc->cd, ccb, tmpstr, 2048, CAM_ESF_ALL, CAM_EPF_ALL);
		*msg = tmpstr;
	}
bailout:
	return rc;
}

static inline bool is_dump_required_error(struct camtape_data *softc, int ret, bool *nonforced_dump)
{
	int rc, err = -ret;
	bool ans = FALSE;
	struct log_sense10_page log_page;

	if (err == EDEV_NO_SENSE || err == EDEV_OVERRUN) {
		/* Sense Key 0 situation. */
		/* Drive may not exist or may not be able to transfer any data. */
		/* Checking capability of data transfer by logsense. */
		log_page.page_code = 0x17;	// volume status
		log_page.subpage_code = 0;
		log_page.len = 0;
		log_page.parm_pointer = 0;
		memset(log_page.data, 0, LOGSENSEPAGE);

		rc = camtape_logsense_page(softc, log_page.page_code, 0, (unsigned char *)&log_page.data,
								   LOGSENSEPAGE);

		ans = (rc == DEVICE_GOOD);
	}
	else if (err >= EDEV_NOT_READY && err < EDEV_INTERNAL_ERROR) {
		ans = TRUE;
	}

	*nonforced_dump = (IS_MEDIUM_ERROR(err) || IS_HARDWARE_ERROR(err));

	return ans;
}


extern struct camtape_global_data global_data;

static inline void camtape_process_errors(struct camtape_data *softc, int rc, char *msg, char *cmd, bool take_dump)
{
	bool nonforced_dump = false;

	if (msg != NULL) {
		ltfsmsg(LTFS_INFO, 30413I, cmd, msg, rc, softc->drive_serial);
		free(msg);
	} else
		ltfsmsg(LTFS_ERR, 30414E, cmd, rc, softc->drive_serial);

	if (softc) {
		if ( take_dump &&
			 !global_data.disable_auto_dump &&
			 is_dump_required_error(softc, rc, &nonforced_dump))
			camtape_get_dump(softc, nonforced_dump);
	}
}

#endif // __camtape_cmn_h
