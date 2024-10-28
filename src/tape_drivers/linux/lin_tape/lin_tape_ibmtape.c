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
** FILE NAME:       tape_drivers/linux/lin_tape/lin_tape_ibmtape.c
**
** DESCRIPTION:     Implements lin_tape tape device operations.
**
** AUTHORS:         Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>
#include <inttypes.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include <ctype.h>

#include "ltfs_copyright.h"

#include "libltfs/ltfs_endian.h"
#include "libltfs/ltfs_fuse_version.h"
#include "libltfs/arch/time_internal.h"
#include "libltfs/arch/time_internal.h"

#include <fuse.h>

#include "libltfs/ltfslogging.h"
#include "libltfs/ltfstrace.h"

#include "tape_drivers/tape_drivers.h"
#include "tape_drivers/vendor_compat.h"

#undef MAXSENSE
#include "IBM_tape.h"

#include "reed_solomon_crc.h"
#include "crc32c_crc.h"

struct lin_tape_ibmtape {
	int                  fd;                   /**< file descriptor of the device */
	bool                 loaded;               /**< Is cartridge loaded? */
	bool                 loadfailed;           /**< Is load/unload failed? */
	unsigned char        drive_serial[255];    /**< serial number of device */
	int                  drive_type;           /**< drive type */
	char                 *devname;             /**< device name */
	long                 fetch_sec_acq_loss_w; /**< Sec to fetch Active CQs loss write */
	bool                 dirty_acq_loss_w;     /**< Is Active CQs loss write dirty */
	float                acq_loss_w;           /**< Active CQs loss write */
	uint64_t             tape_alert;           /**< Latched tape alert flag */
	bool                 is_data_key_set;      /**< Is a valid data key set? */
	unsigned char        dki[12];              /**< key-alias */
	bool                 clear_by_pc;          /**< clear pseudo write perm by partition change */
	uint64_t             force_writeperm;      /**< pseudo write perm threshold */
	uint64_t             force_readperm;       /**< pseudo read perm threashold */
	uint64_t             write_counter;        /**< write call counter for pseudo write perm */
	uint64_t             read_counter;         /**< read call counter for pseudo write perm */
	int                  force_errortype;      /**< 0 is R/W Perm, otherwise no sense */
	bool                 is_worm;              /**< Is worm cartridge loaded? */
	unsigned char        cart_type;            /**< Cartridge type in CM */
	unsigned char        density_code;         /**< Density code */
	crc_enc              f_crc_enc;            /**< Pointer to CRC encode function */
	crc_check            f_crc_check;          /**< Pointer to CRC encode function */
	struct timeout_tape  *timeouts;            /**< Timeout table */
	struct tc_drive_info info;                 /**< Drive information */
	FILE*                profiler;             /**< The file pointer for profiler */
};

struct lin_tape_ibmtape_global_data {
	unsigned          disable_auto_dump; /**< Is auto dump disabled? */
	char              *str_crc_checking; /**< option string for crc_checking */
	unsigned          crc_checking;      /**< Is crc checking enabled? */
	unsigned          strict_drive;      /**< Is bar code length checked strictly? */
};

/*
 * Default tape device
 */
const char *lin_tape_ibmtape_default_device = "/dev/IBMtape0";
volatile char *copyright = LTFS_COPYRIGHT_0"\n"LTFS_COPYRIGHT_1"\n"LTFS_COPYRIGHT_2"\n";

/*
 *  Definitions
 */
#define DMP_DIR "/tmp"

#define LOG_PAGE_HEADER_SIZE      (4)
#define LOG_PAGE_PARAMSIZE_OFFSET (3)
#define LOG_PAGE_PARAM_OFFSET     (4)

#define LINUX_MAX_BLOCK_SIZE (1 * MB)
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define DK_LENGTH 32
#define DKI_LENGTH 12

#define CRC32C_CRC (0x02)

#define MAX_WRITE_RETRY (100)

#define DEFAULT_WRITEPERM               (0)
#define DEFAULT_READPERM                (0)
#define DEFAULT_ERRORTYPE               (0)

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

/*
 *  Global values
 */
struct lin_tape_ibmtape_global_data global_data;
struct error_table *standard_table;
struct error_table *vendor_table;

/**
 *  Macros
 */
#if 0
#define _ioctl(fd,x...) (  \
		fprintf(stderr, "[%s::%d]<@%s> Performing ioctl(%d, " ##x ")", __FILE__, __LINE__, __FUNCTION__, fd), \
		ioctl(fd, x)													\
        )
#else
#define _ioctl(fd,x...) (  \
		ioctl(fd, x)	   \
        )
#endif

/*
 *  Forward reference
 */
int lin_tape_ibmtape_readpos(void *device, struct tc_position *pos);
int lin_tape_ibmtape_rewind(void *device, struct tc_position *pos);
int lin_tape_ibmtape_modesense(void *device, const uint8_t page, const TC_MP_PC_TYPE pc, const uint8_t subpage,
					  unsigned char *buf, const size_t size);
int lin_tape_ibmtape_set_key(void *device, const unsigned char * const keyalias, const unsigned char * const key);
int lin_tape_ibmtape_set_lbp(void *device, bool enable);
int lin_tape_ibmtape_takedump_drive(void *device, bool nonforced_dump);
int lin_tape_ibmtape_get_device_list(struct tc_drive_info *buf, int count);
static const char *generate_product_name(const char *product_id);

/*
 *  Local Functions
 */

/**
 * Get Dump
 * @param device a pointer to the ibmtape backend
 * @return none
 */
static inline void lin_tape_ibmtape_get_dump(void *device, bool nonforced_dump)
{
	lin_tape_ibmtape_takedump_drive(device, nonforced_dump);
	return;
}

static inline int _lin_tape_ibmtape_get_sense(void *device, struct sioc_pass_through *spt_org)
{
	int fd = *((int *) device);
	struct request_sense sense_data;
	int rc, i;

	memset(&sense_data, 0, sizeof(struct request_sense));
	rc = _ioctl(fd, SIOC_REQSENSE, &sense_data);
	if (rc == 0) {
		spt_org->sense_length = sense_data.addlen + 7;
		spt_org->sense[0]  = (sense_data.valid << 7) | (sense_data.err_code & 0x7F);
		spt_org->sense[1]  = sense_data.segnum;
		spt_org->sense[2]  = (sense_data.fm << 7) | (sense_data.eom << 6) | (sense_data.ili << 5) |
			                 (sense_data.resvd1 << 4) | (sense_data.key & 0x0F);
		spt_org->sense[3]  = (sense_data.info >> 24) & 0xFF;
		spt_org->sense[4]  = (sense_data.info >> 16) & 0xFF;
		spt_org->sense[5]  = (sense_data.info >> 8) & 0xFF;
		spt_org->sense[6]  = (sense_data.info & 0xFF);
		spt_org->sense[7]  = sense_data.addlen;
		spt_org->sense[8]  = (sense_data.cmdinfo >> 24) & 0xFF;
		spt_org->sense[9]  = (sense_data.cmdinfo >> 16) & 0xFF;
		spt_org->sense[10] = (sense_data.cmdinfo >> 8) & 0xFF;
		spt_org->sense[11] = (sense_data.cmdinfo & 0xFF);
		spt_org->sense[12] = sense_data.asc;
		spt_org->sense[13] = sense_data.ascq;
		spt_org->sense[14] = sense_data.fru;
		spt_org->sense[15] = (sense_data.sksv << 7) | (sense_data.cd << 6) | (sense_data.resvd2 << 4) |
			                 (sense_data.bpv << 3) | (sense_data.sim & 0x07);
		spt_org->sense[16] = sense_data.field[0];
		spt_org->sense[17] = sense_data.field[1];

		for(i = 0; i < 109; ++i)
			spt_org->sense[18 + i] = sense_data.vendor[i];
	}
	else {
		rc = -EDEV_INTERNAL_ERROR;
		ltfsmsg(LTFS_INFO, 30412I, rc);
	}

	return rc;
}

/**
 * Issue SCSI command through sioc_pass_through interface
 * @param device a pointer to the ibmtape backend
 * @param spt pointer to sioc_pass_through structure
 * @return 0 on success, -1 on command error with sense
 *         -2 on command error without sense or -3 on error of ioctl
 */
static inline int _sioc_paththrough(void *device, struct sioc_pass_through *spt)
{
	int fd = ((struct lin_tape_ibmtape *) device)->fd;
	int ret;

	ret = _ioctl(fd, SIOC_PASS_THROUGH, spt);
	if (ret == -1) {
		ltfsmsg(LTFS_INFO, 30400I, errno, ((struct lin_tape_ibmtape *) device)->drive_serial);
		ret = -3;
	}

	/* check returned status */
	if (spt->target_status || spt->msg_status || spt->host_status || spt->driver_status) {
		if (!spt->sense_length) {
			ltfsmsg(LTFS_DEBUG, 30401D, spt->target_status,
					spt->msg_status, spt->host_status, spt->driver_status,
					((struct lin_tape_ibmtape *) device)->drive_serial);
			if(spt->driver_status & (DRIVER_SENSE | SUGGEST_SENSE))
				_lin_tape_ibmtape_get_sense(device, spt);
		}

		if (spt->sense_length) {
			ltfsmsg(LTFS_DEBUG, 30402D, spt->sense[2] & 0x0F, spt->sense[12], spt->sense[13]);
			ltfsmsg(LTFS_DEBUG, 30403D, spt->sense[45], spt->sense[46], spt->sense[47], spt->sense[48],
					((struct lin_tape_ibmtape *) device)->drive_serial);
			ret = -1;
		} else {
			ltfsmsg(LTFS_INFO, 30404I);
			ltfsmsg(LTFS_INFO, 30405I, spt->target_status, spt->msg_status,
					spt->host_status, spt->driver_status, ((struct lin_tape_ibmtape *) device)->drive_serial);
			if(spt->target_status)
				ret = EDEV_TARGET_ERROR;
			else if (spt->host_status)
				ret = EDEV_HOST_ERROR;
			else if (spt->driver_status)
				ret = EDEV_DRIVER_ERROR;
			else
				ret = -2;
		}
	} else if(ret == -3 && errno == EIO &&
			(spt->cdb[0] == 0x0A || spt->cdb[0] == 0x10)) { /* EIO against write and writefm command */
		ltfsmsg(LTFS_DEBUG, 30401D, spt->target_status,
				spt->msg_status, spt->host_status, spt->driver_status,
				((struct lin_tape_ibmtape *) device)->drive_serial);

		/*
		 *  When the issued command is write or writefm command and hit
		 *  early warning condition, lin_tape doesn't return correct sense.
		 *  (Return 00/0000 with EIO in errno) So always need to call
		 *  _sioc_get_sense() here.
		 */
		_lin_tape_ibmtape_get_sense(device, spt);

		if (spt->sense_length) {
			ltfsmsg(LTFS_DEBUG, 30402D, spt->sense[2] & 0x0F, spt->sense[12], spt->sense[13]);
			ltfsmsg(LTFS_DEBUG, 30403D, spt->sense[45], spt->sense[46], spt->sense[47], spt->sense[48],
					((struct lin_tape_ibmtape *) device)->drive_serial);
			ret = -1;
		} else {
			ltfsmsg(LTFS_INFO, 30404I);
			ltfsmsg(LTFS_INFO, 30405I, spt->target_status, spt->msg_status,
					spt->host_status, spt->driver_status, ((struct lin_tape_ibmtape *) device)->drive_serial);
			if(spt->target_status)
				ret = EDEV_TARGET_ERROR;
			else if (spt->host_status)
				ret = EDEV_HOST_ERROR;
			else if (spt->driver_status)
				ret = EDEV_DRIVER_ERROR;
			else
				ret = -2;
		}
	}

	return ret;
}

/**
 * Convert sense information to negative errno
 * @param spt pointer to sioc_pass_through
 * @return negative errno
 */
static inline int _sioc_sense2errno(struct sioc_pass_through *spt, char **msg)
{
	int rc = -EDEV_UNKNOWN;
	uint32_t sense = 0;

	sense += (uint32_t) (spt->sense[2] & 0x0F) << 16;
	sense += (uint32_t) spt->sense[12] << 8;
	sense += (uint32_t) spt->sense[13];

	rc = _sense2errorcode(sense, standard_table, msg, MASK_WITH_SENSE_KEY);
	if (rc == -EDEV_VENDOR_UNIQUE)
		rc = _sense2errorcode(sense, vendor_table, msg, MASK_WITH_SENSE_KEY);

	return rc;
}

/**
 * Issue SCSI command through sioc_pass_through interface
 * @param device a pointer to the ibmtape backend
 * @param spt pointer to sioc_pass_through structure
 * @param msg pointer to the description of error (on success NULL)
 * @return internal error code defined in ltfs_error.h (Only prefix DEVICE_)
 */
static inline int sioc_paththrough(void *device, struct sioc_pass_through *spt, char **msg)
{
	int sioc_rc, rc;

	sioc_rc = _sioc_paththrough(device, spt);
	if (sioc_rc == 0){
		if (msg)
			*msg = "Command successed";
		rc = DEVICE_GOOD;
	} else if (sioc_rc == -1 && spt->sense_length) {
		rc = _sioc_sense2errno(spt, msg);
	} else if (sioc_rc == -1) {
		/* Program Error (unexpected condition) */
		if (msg)
			*msg = "Program Error (Unexpected condition)";
		rc = -EDEV_INTERNAL_ERROR;
	} else if (sioc_rc == -2) {
		/* Cannot get sense info */
		if (msg)
			*msg = "Cannot get sense information";
		rc = -EDEV_CANNOT_GET_SENSE;
	} else if (sioc_rc == -3) {
		/* Error occures within ioctl */
		if (msg)
			*msg = "Driver error";
		rc = -EDEV_DRIVER_ERROR;
	} else {
		/* Program Error (unexpected condition) */
		if (msg)
			*msg = "Program Error (Unexpected return code)";
		rc  = -EDEV_INTERNAL_ERROR;
	}

	return rc;
}

static inline int lin_tape_ibmtape_ioctlrc2err(void *device, int fd, struct request_sense *sense_data, char **msg)
{
	int sense = 0;
	int rc, rc_sense;

	memset(sense_data, 0, sizeof(struct request_sense));
	rc_sense = ioctl(fd, SIOC_REQSENSE, sense_data);

	if (rc_sense == 0) {
		if (!sense_data->err_code) {
			ltfsmsg(LTFS_DEBUG, 30409D);

			if (msg)
				*msg = "Driver Error";
			rc = -EDEV_DRIVER_ERROR;
		}
		else {
			ltfsmsg(LTFS_DEBUG, 30406D, sense_data->key, sense_data->asc, sense_data->ascq);
			ltfsmsg(LTFS_DEBUG, 30407D, sense_data->vendor[27], sense_data->vendor[28], sense_data->vendor[29], sense_data->vendor[30],
										((struct lin_tape_ibmtape *) device)->drive_serial);

			sense += (uint32_t) sense_data->key << 16;
			sense += (uint32_t) sense_data->asc << 8;
			sense += (uint32_t) sense_data->ascq;

			rc = _sense2errorcode(sense, standard_table, msg, MASK_WITH_SENSE_KEY);

			if (rc == -EDEV_VENDOR_UNIQUE) {
				rc = _sense2errorcode(sense, vendor_table, msg, MASK_WITH_SENSE_KEY);
			}
		}
	}
	else {
		ltfsmsg(LTFS_INFO, 30412I, rc_sense);
		if (msg)
			*msg = "Cannot get sense information";

		rc = -EDEV_CANNOT_GET_SENSE;
	}

	return rc;
}

static inline bool is_expected_error(int cmd, void *param, int rc)
{
	switch (cmd) {
	case SIOC_TEST_UNIT_READY:
		if (rc == -EDEV_NEED_INITIALIZE || rc == -EDEV_CONFIGURE_CHANGED) {
			return true;
		}
		break;
	case STIOC_LOCATE_16:
		if (((struct set_tape_position *)param)->logical_id == TAPE_BLOCK_MAX && rc == -EDEV_EOD_DETECTED) {
			return true;
		}
		break;
	case STIOC_SET_ACTIVE_PARTITION:
		if (((struct set_active_partition *)param)->logical_block_id == TAPE_BLOCK_MAX && rc == -EDEV_EOD_DETECTED) {
			return true;
		}
		break;
	}

	return false;
}

static inline int _sioc_stioc_command(void *device, int cmd, char *cmd_name, void *param, char **msg)
{
	int fd = *((int *) device);
	int rc;
	struct request_sense sense_data;

start:
	rc = ioctl(fd, cmd, param);

	if (rc != 0) {
		rc = lin_tape_ibmtape_ioctlrc2err(device, fd, &sense_data, msg);

		if (rc == -EDEV_TIME_STAMP_CHANGED) {
			ltfsmsg(LTFS_DEBUG, 30411D, cmd_name, cmd, rc);
			goto start;
		}

		if (is_expected_error(cmd, param, rc)) {
			ltfsmsg(LTFS_DEBUG, 30410D, cmd_name, cmd, rc);
		}
		else {
			ltfsmsg(LTFS_INFO, 30408I, cmd_name, cmd, rc, errno, ((struct lin_tape_ibmtape *) device)->drive_serial);
		}
	}
	else {
		*msg = "Command succeeded";
		rc = DEVICE_GOOD;
	}

	return rc;
}

static inline bool is_dump_required_error(void *device, int ret, bool *nonforced_dump)
{
	int rc, err = -ret;
	bool ans = FALSE;
	char *msg;
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

		rc = _sioc_stioc_command(device, SIOC_LOG_SENSE10_PAGE, "LOGSENSE", &log_page, &msg);

		ans = (rc == DEVICE_GOOD);
	}
	else if (err >= EDEV_NOT_READY && err < EDEV_INTERNAL_ERROR) {
		ans = TRUE;
	}

	*nonforced_dump = (IS_MEDIUM_ERROR(err) || IS_HARDWARE_ERROR(err));

	return ans;
}

static inline void lin_tape_ibmtape_process_errors(void *device, int rc, char *msg, char *cmd, bool take_dump)
{
	struct lin_tape_ibmtape *priv = device;
	bool nonforced_dump = false;

	if(msg != NULL)
		ltfsmsg(LTFS_INFO, 30413I, cmd, msg, rc, priv->drive_serial);
	else
		ltfsmsg(LTFS_ERR, 30414E, cmd, rc, priv->drive_serial);

	if (device) {
		priv = (struct lin_tape_ibmtape *) device;
		if ( take_dump &&
			 !global_data.disable_auto_dump &&
			 is_dump_required_error(device, rc, &nonforced_dump) )
			lin_tape_ibmtape_get_dump(device, nonforced_dump);
	}
}

/* Global Functions */

int lin_tape_ibmtape_check_lin_tape_version(void)
{
	int fd_version;
	char lin_tape_version[64], *tmp;
	int digit;
	int version_num[3];
	int version_base[3];
	static char base_lin_tape_version[] = "2.1.0";

	memset(lin_tape_version, 0, sizeof(lin_tape_version));

	fd_version = open("/sys/module/lin_tape/version", O_RDONLY);

	if (fd_version == -1) {
		ltfsmsg(LTFS_WARN, 30415W);
	} else {
		read(fd_version, lin_tape_version, sizeof(lin_tape_version));
		if ((tmp = strchr(lin_tape_version, '\n')) != NULL)
			*tmp = '\0';
		ltfsmsg(LTFS_INFO, 30416I, lin_tape_version);
		close(fd_version);
	}

	(void)sscanf(base_lin_tape_version, "%d.%d.%d", &version_base[0], &version_base[1], &version_base[2]);
	digit = sscanf(lin_tape_version, "%d.%d.%d", &version_num[0], &version_num[1], &version_num[2]);

	if (digit != 3
		|| (version_num[0] < version_base[0])
		|| (version_num[0] == version_base[0] && version_num[1] < version_base[1])
		|| (version_num[0] == version_base[0] && version_num[1] == version_base[1] && version_num[2] < version_base[2])) {
		ltfsmsg(LTFS_ERR, 30417E);
		return -EDEV_DRIVER_ERROR;
	}

	return DEVICE_GOOD;
}

/**
 * Test Unit Ready
 * @param device a pointer to the ibmtape backend
 * @param inq pointer to inquiry data. This function will update this valure
 * @return 0 on success or negative value on error
 */
int lin_tape_ibmtape_test_unit_ready(void *device)
{
	char *msg;
	int rc;
	bool take_dump = true, print_message = true;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_TUR));
	ltfsmsg(LTFS_DEBUG3, 30592D, "test unit ready",
			((struct lin_tape_ibmtape *) device)->drive_serial);

	rc = _sioc_stioc_command(device, SIOC_TEST_UNIT_READY, "TEST UNIT READY", NULL, &msg);

	if (rc != DEVICE_GOOD) {
		switch (rc) {
		case -EDEV_NEED_INITIALIZE:
		case -EDEV_CONFIGURE_CHANGED:
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
			lin_tape_ibmtape_process_errors(device, rc, msg, "test unit ready", take_dump);
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_TUR));
	return rc;
}

/**
 * Reserve the unit
 * @param device a pointer to the ibmtape backend
 * @param lun lun to reserve
 * @return 0 on success or a negative value on error
 */
int lin_tape_ibmtape_reserve_unit(void *device)
{
	int rc;
	char *msg;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_RESERVEUNIT));
	ltfsmsg(LTFS_DEBUG, 30592D, "reserve unit (6)", ((struct lin_tape_ibmtape *) device)->drive_serial);

	rc = _sioc_stioc_command(device, SIOC_RESERVE, "RESERVE", NULL, &msg);

	if (rc != DEVICE_GOOD)
		lin_tape_ibmtape_process_errors(device, rc, msg, "reserve unit(6)", true);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_RESERVEUNIT));
	return rc;
}

/**
 * Release the unit
 * @param device a pointer to the ibmtape backend
 * @param lun lun to release
 * @return 0 on success or a negative value on error
 */
int lin_tape_ibmtape_release_unit(void *device)
{
	int rc;
	char *msg;
	bool take_dump = true, print_message = true;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_RELEASEUNIT));
	ltfsmsg(LTFS_DEBUG, 30592D, "release unit (6)", ((struct lin_tape_ibmtape *) device)->drive_serial);

	/* Invoke _ioctl to Release Unit */
	rc = _sioc_stioc_command(device, SIOC_RELEASE, "RELEASE", NULL, &msg);

	if (rc != DEVICE_GOOD) {
		switch (rc) {
		case -EDEV_POR_OR_BUS_RESET:
			take_dump = false;
			break;
		default:
			break;
		}
		if (print_message)
			lin_tape_ibmtape_process_errors(device, rc, msg, "release unit(6)", take_dump);
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_RELEASEUNIT));
	return rc;
}

/**
 * Get the serial number of the changer device.
 * @param device Handle of the changer device to get the serial number for
 * @param[out] result On success, contains the serial number of the changer. The memory
 *             is allocated on demand and must be freed by the caller once it's gone using it.
 * @return 0 on success or a negative value on error.
 */
int lin_tape_ibmtape_get_serialnumber(void *device, char **result)
{
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	CHECK_ARG_NULL(device, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(result, -LTFS_NULL_ARG);
	ltfs_profiler_add_entry(priv->profiler, NULL, CHANGER_REQ_ENTER(REQ_TC_GETSER));

	*result = SAFE_STRDUP((const char *) priv->drive_serial);
	if (! *result) {
		ltfsmsg(LTFS_ERR, 10001E, "lin_tape_ibmtape_get_serialnumber: result");
		ltfs_profiler_add_entry(priv->profiler, NULL, CHANGER_REQ_EXIT(REQ_TC_GETSER));
		return -EDEV_NO_MEMORY;
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, CHANGER_REQ_EXIT(REQ_TC_GETSER));
	return 0;
}

/**
 * Get the tape device's information
 * This function must not issue any scsi command to the device.
 * @param device a pointer to the tape device
 * @param[out] info On success, contains device information.
 * @return 0 on success or a negative value on error
 */
int lin_tape_ibmtape_get_info(void *device, struct tc_drive_info *info)
{
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape*)device;

	memcpy(info, &priv->info, sizeof(struct tc_drive_info));

	return 0;
}

/**
 * Enable profiler function
 * @param device a pointer to the tape device
 * @param work_dir work directory to store profiler data
 * @paran enable enable or disable profiler function of this backend
 * @return 0 on success or a negative value on error
 */
int lin_tape_ibmtape_set_profiler(void *device, char *work_dir, bool enable)
{
	int rc = 0;
	char *path;
	FILE *p;
	struct timer_info timerinfo;
	struct lin_tape_ibmtape *priv = ((struct lin_tape_ibmtape *) device);

	if (enable) {
		if (priv->profiler)
			return 0;

		if(!work_dir)
			return -LTFS_BAD_ARG;

		rc = asprintf(&path, "%s/%s%s%s", work_dir, DRIVER_PROFILER_BASE,
					   priv->drive_serial, PROFILER_EXTENSION);
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
				ltfsmsg(LTFS_INFO, 30418I, bufsize, i + LOG_PAGE_PARAM_OFFSET);
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
 * Parse option for IBM tape driver
 * @param devname device name of the LTO tape driver
 * @return a pointer to the ibmtape backend on success or NULL on error
 */
#define LIN_TAPE_IBMTAPE_OPT(templ,offset,value) { templ, offsetof(struct lin_tape_ibmtape_global_data, offset), value }

static struct fuse_opt lin_tape_ibmtape_global_opts[] = {
	LIN_TAPE_IBMTAPE_OPT("autodump",          disable_auto_dump, 0),
	LIN_TAPE_IBMTAPE_OPT("noautodump",        disable_auto_dump, 1),
	LIN_TAPE_IBMTAPE_OPT("scsi_lbprotect=%s", str_crc_checking, 0),
	LIN_TAPE_IBMTAPE_OPT("strict_drive",   strict_drive, 1),
	LIN_TAPE_IBMTAPE_OPT("nostrict_drive", strict_drive, 0),
	FUSE_OPT_END
};

int null_parser(void *priv, const char *arg, int key, struct fuse_args *outargs)
{
	return 1;
}

int lin_tape_ibmtape_parse_opts(void *device, void *opt_args)
{
	struct fuse_args *args = (struct fuse_args *) opt_args;
	int ret;

	/* fuse_opt_parse can handle a NULL device parameter just fine */
	ret = fuse_opt_parse(args, &global_data, lin_tape_ibmtape_global_opts, null_parser);
	if (ret < 0) {
		ltfsmsg(LTFS_INFO, 30419I, ret);
		return ret;
	}

	/* Validate scsi logical block protection */
	if (global_data.str_crc_checking) {
		if (strcasecmp(global_data.str_crc_checking, "on") == 0)
			global_data.crc_checking = 1;
		else if (strcasecmp(global_data.str_crc_checking, "off") == 0)
			global_data.crc_checking = 0;
		else {
			ltfsmsg(LTFS_ERR, 30420E, global_data.str_crc_checking);
			return -EINVAL;
		}
	} else
		global_data.crc_checking = 0;

	return 0;
}

/**
 * Get inquiry data from a specific page
 * @param device tape device
 * @param page page
 * @param inq pointer to inquiry data. This function will update this value
 * @return 0 on success or negative value on error
 */
int _lin_tape_ibmtape_inquiry_page(void *device, unsigned char page, struct tc_inq_page *inq, bool error_handle)
{
	char *msg;
	int rc;
	struct inquiry_page inq_page;

	if (!inq)
		return -EDEV_INVALID_ARG;

	ltfsmsg(LTFS_DEBUG, 30593D, "inquiry", page, ((struct lin_tape_ibmtape *) device)->drive_serial);

	/* init value */
	memset(inq_page.data, 0, sizeof(inq_page.data));
	inq_page.page_code = page;

	rc = _sioc_stioc_command(device, SIOC_INQUIRY_PAGE, "INQUIRY PAGE", &inq_page, &msg);

	if (rc != DEVICE_GOOD) {
		if(error_handle)
			lin_tape_ibmtape_process_errors(device, rc, msg, "inquiry", true);
	} else {
		memcpy(inq->data, inq_page.data, MAX_INQ_LEN);
	}

	return rc;
}

int lin_tape_ibmtape_inquiry_page(void *device, unsigned char page, struct tc_inq_page *inq)
{
	int ret = 0;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_INQUIRYPAGE));
	ret = _lin_tape_ibmtape_inquiry_page(device, page, inq, true);
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_INQUIRYPAGE));
	return ret;
}

/**
 * Get inquiry data
 * @param inq pointer to inquiry data. This function will update this value
 * @return 0 on success or negative value on error
 */
int lin_tape_ibmtape_inquiry(void *device, struct tc_inq *inq)
{
	char *msg;
	int rc;
	struct inquiry_data inq_data;
	int vendor_length;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_INQUIRY));
	memset(&inq_data, 0, sizeof(struct inquiry_data));

	rc = _sioc_stioc_command(device, SIOC_INQUIRY, "INQUIRY", &inq_data, &msg);

	if(rc == DEVICE_GOOD) {
		inq->devicetype = inq_data.type;
		inq->cmdque = inq_data.cmdque;
		strncpy((char *)inq->vid, (char *)inq_data.vid, 8);
		inq->vid[8] = '\0';
		strncpy((char *)inq->pid, (char *)inq_data.pid, 16);
		inq->pid[16] = '\0';
		strncpy((char *)inq->revision, (char *)inq_data.revision, 4);
		inq->revision[4] = '\0';

		vendor_length = 20;

		if (IS_ENTERPRISE(priv->drive_type)) {
			vendor_length = 18;
		}

		strncpy((char *)inq->vendor, (char *)inq_data.vendor1, vendor_length);
		inq->vendor[vendor_length] = '\0';
	}
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_INQUIRY));
	return rc;
}

/**
 * Open IBM tape backend.
 * @param devname device name of the LTO tape driver
 * @param[out] handle contains the handle to the IBM tape backend on success
 * @return 0 on success or a negative value on error
 */
int lin_tape_ibmtape_open(const char *devname, void **handle)
{
	struct lin_tape_ibmtape *priv;
	struct tc_inq inq_data;
	struct tc_inq_page inq_page_data;
	int drive_type = DRIVE_UNSUPPORTED;
	int ret;
	char *devfile = NULL;
	int i, devs = 0, info_devs = 0;
	struct tc_drive_info *buf = NULL;
	struct stat stat_buf;

	CHECK_ARG_NULL(handle, -LTFS_NULL_ARG);
	*handle = NULL;

	ret =  lin_tape_ibmtape_check_lin_tape_version();
	if (ret != DEVICE_GOOD) {
		return ret;
	}

	ltfsmsg(LTFS_INFO, 30423I, devname);

	priv = calloc(1, sizeof(struct lin_tape_ibmtape));
	if (! priv) {
		ltfsmsg(LTFS_ERR, 10001E, "lin_tape_ibmtape_open: device private data");
		return -EDEV_NO_MEMORY;
	}

	ret = stat(devname, &stat_buf);
	if (!ret) {
		/* Specified file is existed. Use it as a device file name */
		devfile = SAFE_STRDUP(devname);
	} else {
		/* Search device by serial number (Assume devname has a drive serial) */
		devs = lin_tape_ibmtape_get_device_list(NULL, 0);
		if (devs) {
			buf = (struct tc_drive_info *)calloc(devs * 2, sizeof(struct tc_drive_info));
			if (! buf) {
				ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
				return -LTFS_NO_MEMORY;
			}
			info_devs = lin_tape_ibmtape_get_device_list(buf, devs * 2);
		}

		for (i = 0; i < info_devs; i++) {
			if (! strncmp(buf[i].serial_number, devname, TAPE_SERIAL_LEN_MAX) ) {
				devfile = SAFE_STRDUP(buf[i].name);
				if (!devfile) {
					ltfsmsg(LTFS_ERR, 10001E, "lin_tape_ibmtape_open: devname");
					if (buf) free(buf);
					free(priv);
					return -EDEV_NO_MEMORY;
				}
				break;
			}
		}

		if (buf) {
			free(buf);
			buf = NULL;
		}
	}

	if (!devfile) {
		free(priv);
		return -LTFS_NO_DEVICE;
	}

	priv->fd = open(devfile, O_RDWR | O_NDELAY);
	if (priv->fd < 0) {
		priv->fd = open(devfile, O_RDONLY | O_NDELAY);
		if (priv->fd < 0) {
			if (errno == EAGAIN) {
				ltfsmsg(LTFS_ERR, 30424E, devname);
				ret = -EDEV_DEVICE_BUSY;
			} else {
				ltfsmsg(LTFS_INFO, 30425I, devname, errno);
				ret = -EDEV_DEVICE_UNOPENABLE;
			}
			free(priv);
			free(devfile);
			return ret;
		}
		ltfsmsg(LTFS_WARN, 30426W, devname);
	}

	ret = lin_tape_ibmtape_inquiry(priv, &inq_data);
	if (ret) {
		ltfsmsg(LTFS_INFO, 30427I, ret);
		close(priv->fd);
		free(priv);
		free(devfile);
		return ret;
	} else {
		ltfsmsg(LTFS_INFO, 30428I, inq_data.pid);
		ltfsmsg(LTFS_INFO, 30429I, inq_data.vid);

		/* Check the drive is supportable */
		struct supported_device **cur = ibm_supported_drives;
		while(*cur) {
			if((! strncmp((char*)inq_data.vid, (*cur)->vendor_id, strlen((*cur)->vendor_id)) ) &&
			   (! strncmp((char*)inq_data.pid, (*cur)->product_id, strlen((*cur)->product_id)) ) ) {
				drive_type = (*cur)->drive_type;
				break;
			}
			cur++;
		}

		if (drive_type > 0) {
			priv->drive_type = drive_type;

			/* Setup IBM tape specific parameters */
			standard_table = standard_tape_errors;
			vendor_table   = ibm_tape_errors;

			/* Set specific timeout value based on drive type */
			ibm_tape_init_timeout(&priv->timeouts, priv->drive_type);
		} else {
			ltfsmsg(LTFS_INFO, 30430I, inq_data.pid);
			close(priv->fd);
			free(priv);
			free(devfile);
			return -EDEV_DEVICE_UNSUPPORTABLE;
		}
	}

	memset(&inq_page_data, 0, sizeof(struct tc_inq_page));
	ret = lin_tape_ibmtape_inquiry_page(priv, TC_INQ_PAGE_DRVSERIAL, &inq_page_data);
	if (ret) {
		ltfsmsg(LTFS_INFO, 30431I, TC_INQ_PAGE_DRVSERIAL, ret);
		close(priv->fd);
		free(priv);
		free(devfile);
		return ret;
	}

	/* Set drive serial number to private data to put it to the dump file name */
	memset(priv->drive_serial, 0, sizeof(priv->drive_serial));
	for (int i = 4; i < (int)sizeof(inq_page_data.data) - 1; i++) {
		if (inq_page_data.data[i] == ' ' || inq_page_data.data[i] == '\0')
			break;
		priv->drive_serial[i - 4] = inq_page_data.data[i];
	}

	ltfsmsg(LTFS_INFO, 30432I, inq_data.revision);
	if (! ibm_tape_is_supported_firmware(priv->drive_type, (unsigned char*)inq_data.revision)) {
		ltfsmsg(LTFS_INFO, 30430I, "firmware");
		close(priv->fd);
		free(priv);
		free(devfile);
		return -EDEV_UNSUPPORTED_FIRMWARE;
	}

	ltfsmsg(LTFS_INFO, 30433I, priv->drive_serial);

	priv->loaded = false; /* Assume tape is not loaded until a successful load call. */
	priv->devname = SAFE_STRDUP(devname);

	priv->clear_by_pc     = false;
	priv->force_writeperm = DEFAULT_WRITEPERM;
	priv->force_readperm  = DEFAULT_READPERM;
	priv->force_errortype = DEFAULT_ERRORTYPE;

	snprintf(priv->info.name, TAPE_DEVNAME_LEN_MAX + 1, "%s", devfile);
	snprintf(priv->info.vendor, TAPE_VENDOR_NAME_LEN_MAX + 1, "%s", inq_data.vid);
	snprintf(priv->info.model, TAPE_MODEL_NAME_LEN_MAX + 1, "%s", inq_data.pid);
	snprintf(priv->info.serial_number, TAPE_SERIAL_LEN_MAX + 1, "%s", priv->drive_serial);
	snprintf(priv->info.product_rev, PRODUCT_REV_LENGTH + 1, "%s", inq_data.revision);
	snprintf(priv->info.product_name, PRODUCT_NAME_LENGTH + 1, "%s", generate_product_name((char *)inq_data.pid));
	priv->info.host    = 0;
	priv->info.channel = 0;
	priv->info.target  = 0;
	priv->info.lun     = -1;

	free(devfile);

	*handle = (void *) priv;

	return DEVICE_GOOD;
}

/**
 * Reopen IBM tape backend
 * @param devname device name of the LTO tape driver
 * @param device a pointer to the ibmtape backend
 * @return 0 on success or a negative value on error
 */
int lin_tape_ibmtape_reopen(const char *name, void *vstate)
{
	/* Do nothing */
	return 0;
}

/**
 * Close IBM tape backend
 * @param device a pointer to the ibmtape backend
 * @return 0 on success or a negative value on error
 */
int lin_tape_ibmtape_close(void *device)
{
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;
	struct tc_position pos;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_CLOSE));
	if (priv->loaded)
		lin_tape_ibmtape_rewind(device, &pos);

	lin_tape_ibmtape_set_lbp(device, false);

	if (priv->devname)
		free(priv->devname);

	close(priv->fd);

	ibm_tape_destroy_timeout(&priv->timeouts);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_CLOSE));

	if (priv->profiler) {
		fclose(priv->profiler);
		priv->profiler = NULL;
	}

	free(priv);

	return 0;
}

/**
 * Close only file descriptor
 * @param device a pointer to the ibmtape backend
 * @return 0 on success or a negative value on error
 */
int lin_tape_ibmtape_close_raw(void *device)
{
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_CLOSERAW));
	close(priv->fd);
	priv->fd = -1;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_CLOSERAW));
	return 0;
}

/**
 * Test if a given tape device is connected to the host
 * @param devname device name of the LTO tape driver
 * @return 0 on success, indicating that the drive is connected to the host,
 *  or a negative value on error.
 */
int lin_tape_ibmtape_is_connected(const char *devname)
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
	int fd = *((int *) device);
	struct mtop mt = {.mt_op = cmd,.mt_count = param };
	struct request_sense sense_data;
	int rc;

start:
	rc = ioctl(fd, MTIOCTOP, &mt);

	if (rc != 0) {
		rc = lin_tape_ibmtape_ioctlrc2err(device, fd, &sense_data, msg);
		if (rc == -EDEV_TIME_STAMP_CHANGED) {
			ltfsmsg(LTFS_DEBUG, 30411D, cmd_name, cmd, rc);
			goto start;
		}
		ltfsmsg(LTFS_INFO, 30408I, cmd_name, cmd, rc, errno, ((struct lin_tape_ibmtape *) device)->drive_serial);
	}
	else {
		*msg = "Command succeeded";
		rc = DEVICE_GOOD;
	}

	return rc;
}

int _st_command(void *device, int cmd, char *cmd_name, int param, char **msg)
{
	int fd = *((int *) device);
    struct stop st = {.st_op = cmd, .st_count = param};
	struct request_sense sense_data;
	int rc;

start:
	rc = ioctl(fd, STIOCTOP, &st);

	if (rc != 0) {
		rc = lin_tape_ibmtape_ioctlrc2err(device, fd, &sense_data, msg);
		if (rc == -EDEV_TIME_STAMP_CHANGED) {
			ltfsmsg(LTFS_DEBUG, 30411D, cmd_name, cmd, rc);
			goto start;
		}
		ltfsmsg(LTFS_INFO, 30408I, cmd_name, cmd, rc, errno, ((struct lin_tape_ibmtape *) device)->drive_serial);
	}
	else {
		*msg = "Command succeeded";
		rc = DEVICE_GOOD;
	}

	return rc;
}

/**
 * Read a record from tape
 * @param device a pointer to the ibmtape backend
 * @param buf a pointer to read buffer
 * @param count read size
 * @param pos a pointer to position data. This function will update position infomation.
 * @param unusual_size a flag specified unusual size or not
 * @return read length on success or a negative value on error
 */
int lin_tape_ibmtape_read(void *device, char *buf, size_t count, struct tc_position *pos,
				 const bool unusual_size)
{
	ssize_t len = -1, read_len;
	int rc;
	bool silion = unusual_size;
	char *msg;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;
	int    fd = priv->fd;
	size_t datacount = count;

	/*
	 * TODO: Check count is smaller than max of SSIZE_MAX
	 *       The prototype of read system call is
	 *       ssize_t read(int fd, void *buf, size_t count);
	 */

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_READ));
	ltfsmsg(LTFS_DEBUG3, 30595D, "read", count, ((struct lin_tape_ibmtape *) device)->drive_serial);

	if (priv->force_readperm) {
		priv->read_counter++;
		if (priv->read_counter > priv->force_readperm) {
			ltfsmsg(LTFS_INFO, 30434I, "read");
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READ));
			if (priv->force_errortype)
				return -EDEV_NO_SENSE;
			else
				return -EDEV_READ_PERM;
		}
	}

	if(global_data.crc_checking) {
		datacount = count + 4;
		/* Never fall into this block, fail safe to adjust record length*/
		if (datacount > LINUX_MAX_BLOCK_SIZE)
			datacount = LINUX_MAX_BLOCK_SIZE;
	}
	read_len = read(fd, buf, datacount);

	if ((!silion && (size_t)read_len != datacount) || (read_len <= 0)) {
		struct request_sense sense_data;

		rc = lin_tape_ibmtape_ioctlrc2err(device, fd , &sense_data, &msg);

		switch (rc) {
		case -EDEV_NO_SENSE:
			if (sense_data.fm) {
				/* Filemark Detected */
				ltfsmsg(LTFS_DEBUG, 30436D);
				rc = DEVICE_GOOD;
				pos->block++;
				pos->filemarks++;
				len = 0;
			}
			else if (sense_data.ili) {
				/* Illegal Length */
				int32_t diff_len;

				diff_len = (int32_t)sense_data.info;

				if (diff_len < 0) {
					ltfsmsg(LTFS_INFO, 30437I, diff_len, (int)count - diff_len); // "Detect overrun condition"
					rc = -EDEV_OVERRUN;
				}
				else {
					ltfsmsg(LTFS_DEBUG, 30438D, diff_len, (int)count - diff_len); // "Detect underrun condition"
					len = count - diff_len;
					rc = DEVICE_GOOD;
					pos->block++;
				}
			}
			else if (errno == EOVERFLOW) {
				ltfsmsg(LTFS_INFO, 30437I, (int)(count - read_len), (int)read_len); // "Detect overrun condition"
				rc = -EDEV_OVERRUN;
			}
			else if ((size_t)read_len < count) {
				ltfsmsg(LTFS_DEBUG, 30438D, (int)(count - read_len), (int)read_len); // "Detect underrun condition"
				len = read_len;
				rc = DEVICE_GOOD;
				pos->block++;
			}
			break;
		case -EDEV_FILEMARK_DETECTED:
			ltfsmsg(LTFS_DEBUG, 30436D);
			rc = DEVICE_GOOD;
			pos->block++;
			pos->filemarks++;
			len = 0;
			break;
		}

		if (rc != DEVICE_GOOD) {
			if ((rc != -EDEV_CRYPTO_ERROR && rc != -EDEV_KEY_REQUIRED) || ((struct lin_tape_ibmtape *) device)->is_data_key_set) {
				ltfsmsg(LTFS_INFO, 30408I, "READ", (int)count, rc, errno, ((struct lin_tape_ibmtape *) device)->drive_serial);
				lin_tape_ibmtape_process_errors(device, rc, msg, "read", true);
			}
			len = rc;
		}
	}
	else {
		len = silion ? read_len : (ssize_t)datacount;
		pos->block++;
	}

	if(global_data.crc_checking && len > 4) {
		if (priv->f_crc_check)
			len = priv->f_crc_check(buf, len - 4);
		if (len < 0) {
			ltfsmsg(LTFS_ERR, 30439E);
			len = -EDEV_LBP_READ_ERROR;
		}
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READ));
	return len;
}

#define WRITE_RETRY (-LINUX_MAX_BLOCK_SIZE)

static inline int _handle_block_allocation_failure(void *device, struct tc_position *pos, int *retry)
{
    int ret = 0;
    struct tc_position tmp_pos = {0, 0};

    /* Sleep 3 secs to wait garbage correction in kernel side and retry */
    ltfsmsg(LTFS_WARN, 30440W, ++(*retry));
    sleep(3);

    ret = lin_tape_ibmtape_readpos(device, &tmp_pos);
    if (ret == DEVICE_GOOD && pos->partition == tmp_pos.partition) {
        if (pos->block == tmp_pos.block) {
            /* No record was written on the tape */
            ltfsmsg(LTFS_INFO, 30441I, (unsigned int)tmp_pos.partition, (unsigned long long)tmp_pos.block);
            ret = WRITE_RETRY;
        } else if (pos->block == tmp_pos.block - 1) {
            /* The record was written on the tape */
            ltfsmsg(LTFS_INFO, 30442I,
                    (unsigned int)pos->partition, (unsigned long long)pos->block,
                    (unsigned int)tmp_pos.partition, (unsigned long long)tmp_pos.block);
            ret = DEVICE_GOOD;
            pos->block++;
        } else {
            /* Unexpected position */
            ltfsmsg(LTFS_WARN, 30443W, ret,
                    (unsigned int)pos->partition, (unsigned long long)pos->block,
                    (unsigned int)tmp_pos.partition, (unsigned long long)tmp_pos.block);
            ret = -EDEV_NO_MEMORY;
        }
    } else
        ltfsmsg(LTFS_WARN, 30444W, ret,
                (unsigned int)pos->partition, (unsigned long long)pos->block,
                (unsigned int)tmp_pos.partition, (unsigned long long)tmp_pos.block);

    return ret;
}

/**
 * Write a record to tape
 * When the drive detect early warning condition, this function will return {-ENOSPC, true}
 *
 * @param device a pointer to the ibmtape backend
 * @param buf a pointer to read buffer
 * @param count write size
 * @param pos a pointer to position data. This function will update position infomation.
 * @param unusual_size a flag specified unusual size or not
 * @return rc 0 on success or a negative value on error
 */

int lin_tape_ibmtape_write(void *device, const char *buf, size_t count, struct tc_position *pos)
{
	int rc = -1;
	ssize_t written;
	char *msg = "";
	struct request_sense sense_data;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;
	int    fd = priv->fd, retry = 0, current_errno;
	size_t datacount = count;

	/*
	 * TODO: Check count is smaller than max of SSIZE_MAX
	 *       The prototype of write system call is
	 *       ssize_t write(int fd, const void *buf, size_t count);
	 */

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_WRITE));
	ltfsmsg(LTFS_DEBUG3, 30595D, "write", count, ((struct lin_tape_ibmtape *) device)->drive_serial);

	if ( priv->force_writeperm ) {
		priv->write_counter++;
		if ( priv->write_counter > priv->force_writeperm ) {
			ltfsmsg(LTFS_INFO, 30434I, "write");
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_WRITE));
			if (priv->force_errortype)
				return -EDEV_NO_SENSE;
			else
				return -EDEV_WRITE_PERM;
		} else if ( priv->write_counter > (priv->force_writeperm - THRESHOLD_FORCE_WRITE_NO_WRITE) ) {
			ltfsmsg(LTFS_INFO, 30435I);
			pos->block++;
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_WRITE));
			return DEVICE_GOOD;
		}
	}

	errno = 0;
	/* Invoke _ioctl to Write */
	if(global_data.crc_checking) {
		if (priv->f_crc_enc)
			priv->f_crc_enc((void *)buf, count);
		datacount = count + 4;
	}

write_start:
	written = write(fd, buf, datacount);
	if ((size_t)written != datacount || errno == ENOSPC) {
		ltfsmsg(LTFS_INFO, 30408I, "WRITE", (int)count, (int)written, errno, ((struct lin_tape_ibmtape *) device)->drive_serial);

		if (errno == ENOSPC) {
			lin_tape_ibmtape_readpos(device, pos);
			if (pos->early_warning) {
				ltfsmsg(LTFS_WARN, 30445W, "write");
				rc = DEVICE_GOOD;
			} else if (pos->programmable_early_warning) {
				ltfsmsg(LTFS_WARN, 30446W, "write");
				rc = DEVICE_GOOD;
			}
		} else if (errno == ENOMEM && retry < MAX_WRITE_RETRY) {
			rc = _handle_block_allocation_failure(device, pos, &retry);
			if (rc == WRITE_RETRY) {
				errno = 0;
				goto write_start;
			}
		} else {
			current_errno = errno;
			rc = lin_tape_ibmtape_ioctlrc2err(device, fd , &sense_data, &msg);

			switch (rc) {
			case -EDEV_EARLY_WARNING:
				ltfsmsg(LTFS_WARN, 30445W, "write");
				rc = DEVICE_GOOD;
				lin_tape_ibmtape_readpos(device, pos);
				pos->early_warning = true;
				break;
			case -EDEV_PROG_EARLY_WARNING:
				ltfsmsg(LTFS_WARN, 30446W, "write");
				rc = DEVICE_GOOD;
				lin_tape_ibmtape_readpos(device, pos);
				pos->programmable_early_warning = true;
				break;
			}

			if (retry < MAX_WRITE_RETRY
				&& ((current_errno == EIO && rc == -EDEV_NO_SENSE ) || (rc == -EDEV_CONFIGURE_CHANGED) || (rc == -EDEV_TIME_STAMP_CHANGED))) {
				rc = _handle_block_allocation_failure(device, pos, &retry);
				if (rc == WRITE_RETRY) {
					errno = 0;
					goto write_start;
				}
			}
		}

		if (rc != DEVICE_GOOD)
			lin_tape_ibmtape_process_errors(device, rc, msg, "write", true);

		if (rc == -EDEV_LBP_WRITE_ERROR)
			ltfsmsg(LTFS_ERR, 30447E);
	} else {
		rc = DEVICE_GOOD;
		pos->block++;
	}

	((struct lin_tape_ibmtape *) device)->dirty_acq_loss_w = true;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_WRITE));
	return rc;
}

/**
 * Write filemark(s) to tape
 * @param device a pointer to the ibmtape backend
 * @param count count to write filemark. If 0 only flush.
 * @param pos a pointer to position data. This function will update position infomation.
 * @param immed Set immediate bit on
 * @return rc 0 on success or a negative value on error
 */
int lin_tape_ibmtape_writefm(void *device, size_t count, struct tc_position *pos, bool immed)
{
	int rc = -1;
	char *msg;
	size_t written_count;
	tape_filemarks_t cur_fm = pos->filemarks;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_WRITEFM));
	ltfsmsg(LTFS_DEBUG, 30596D, "writefm", count, ((struct lin_tape_ibmtape *) device)->drive_serial);

start_wfm:
	errno = 0;
	rc = _mt_command(device, (immed? MTWEOFI : MTWEOF), "WRITE FM", count, &msg);
	lin_tape_ibmtape_readpos(device, pos);

	if (rc != DEVICE_GOOD) {
		switch (rc) {
		case -EDEV_EARLY_WARNING:
			ltfsmsg(LTFS_WARN, 30445W, "writefm");
			rc = DEVICE_GOOD;
			pos->early_warning = true;
			break;
		case -EDEV_PROG_EARLY_WARNING:
			ltfsmsg(LTFS_WARN, 30446W, "writefm");
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
			else
				rc = DEVICE_GOOD;
			break;
		default:
			if (pos->early_warning) {
				ltfsmsg(LTFS_WARN, 30445W, "writefm");
				rc = DEVICE_GOOD;
			}
			if (pos->programmable_early_warning) {
				ltfsmsg(LTFS_WARN, 30446W, "writefm");
				rc = DEVICE_GOOD;
			}
			break;
		}

		if (rc != DEVICE_GOOD) {
			lin_tape_ibmtape_process_errors(device, rc, msg, "writefm", true);
		}
	} else {
		if (pos->early_warning) {
			ltfsmsg(LTFS_WARN, 30445W, "writefm");
			rc = DEVICE_GOOD;
		}
		if (pos->programmable_early_warning) {
			ltfsmsg(LTFS_WARN, 30446W, "writefm");
			rc = DEVICE_GOOD;
		}
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_WRITEFM));
	return rc;
}

/**
 * Rewind tape
 * @param device a pointer to the ibmtape backend
 * @param pos a pointer to position data. This function will update position infomation.
 * @return 0 on success or a negative value on error
 */
int lin_tape_ibmtape_rewind(void *device, struct tc_position *pos)
{
	int rc;
	char *msg;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_REWIND));
	ltfsmsg(LTFS_DEBUG, 30592D, "rewind", ((struct lin_tape_ibmtape *) device)->drive_serial);

	rc = _mt_command(device, MTREW, "REWIND", 0, &msg);
	lin_tape_ibmtape_readpos(device, pos);

	if (rc != DEVICE_GOOD) {
		lin_tape_ibmtape_process_errors(device, rc, msg, "rewind", true);
	}

	priv->clear_by_pc     = false;
	priv->force_writeperm = DEFAULT_WRITEPERM;
	priv->force_readperm  = DEFAULT_READPERM;
	priv->write_counter = 0;
	priv->read_counter  = 0;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_REWIND));
	return rc;
}

/**
 * Locate to position on tape
 * @param device a pointer to the ibmtape backend
 * @param dest a position data of destination.
 * @param pos a pointer to position data. This function will update position infomation.
 * @return rc 0 on success or a negative value on error
 */
int lin_tape_ibmtape_locate(void *device, struct tc_position dest, struct tc_position *pos)
{
	int rc;
	char *msg;
	struct set_active_partition set_part;
	struct set_tape_position setpos;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_LOCATE));
	ltfsmsg(LTFS_DEBUG, 30597D, "locate", (unsigned long long)dest.partition,
		(unsigned long long)dest.block, ((struct lin_tape_ibmtape *) device)->drive_serial);

	if (pos->partition != dest.partition) {
		memset(&set_part, 0, sizeof(struct set_active_partition));
		set_part.partition_number = dest.partition;
		set_part.logical_block_id = dest.block;

		if (priv->clear_by_pc) {
			priv->clear_by_pc     = false;
			priv->force_writeperm = DEFAULT_WRITEPERM;
			priv->force_readperm  = DEFAULT_READPERM;
			priv->write_counter = 0;
			priv->read_counter  = 0;
		}

		rc = _sioc_stioc_command(device, STIOC_SET_ACTIVE_PARTITION, "LOCATE(PART)", &set_part, &msg);
	} else {
		memset(&setpos, 0, sizeof(struct set_tape_position));
		setpos.logical_id = dest.block;
		setpos.logical_id_type = LOGICAL_ID_BLOCK_TYPE;

		rc = _sioc_stioc_command(device, STIOC_LOCATE_16, "LOCATE", &setpos, &msg);
	}

	if (rc != DEVICE_GOOD) {
		if ((unsigned long long)dest.block == TAPE_BLOCK_MAX && rc == -EDEV_EOD_DETECTED) {
			ltfsmsg(LTFS_DEBUG, 30448D, "Locate");
			rc = DEVICE_GOOD;
		}

		if (rc != DEVICE_GOOD)
			lin_tape_ibmtape_process_errors(device, rc, msg, "locate", true);
	}

	lin_tape_ibmtape_readpos(device, pos);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_LOCATE));
	return rc;
}

/**
 * Space to position on tape
 * @param device a pointer to the ibmtape backend
 * @param count specify record or fm count to move
 * @param type specify type of move
 * @param pos a pointer to position data. This function will update position infomation.
 * @return rc 0 on success or a negative value on error
 */
int lin_tape_ibmtape_space(void *device, size_t count, TC_SPACE_TYPE type, struct tc_position *pos)
{
	int cmd;
	int rc;
	char *msg;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_SPACE));
	switch (type) {
		case TC_SPACE_EOD:
			ltfsmsg(LTFS_DEBUG, 30592D, "space to EOD", ((struct lin_tape_ibmtape *) device)->drive_serial);
			cmd = MTEOM;
			count = 0;
			break;
		case TC_SPACE_FM_F:
			ltfsmsg(LTFS_DEBUG, 30594D, "space forward file marks", (unsigned long long)count,
					((struct lin_tape_ibmtape *) device)->drive_serial);
			cmd = MTFSF;
			break;
		case TC_SPACE_FM_B:
			ltfsmsg(LTFS_DEBUG, 30594D, "space back file marks", (unsigned long long)count,
					((struct lin_tape_ibmtape *) device)->drive_serial);
			cmd = MTBSF;
			break;
		case TC_SPACE_F:
			ltfsmsg(LTFS_DEBUG, 30594D, "space forward records", (unsigned long long)count,
					((struct lin_tape_ibmtape *) device)->drive_serial);
			cmd = MTFSR;
			break;
		case TC_SPACE_B:
			ltfsmsg(LTFS_DEBUG, 30594D, "space back records", (unsigned long long)count,
					((struct lin_tape_ibmtape *) device)->drive_serial);
			cmd = MTBSR;
			break;
		default:
			/* unexpected space type */
			ltfsmsg(LTFS_INFO, 30449I);
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SPACE));
			return EDEV_INVALID_ARG;
	}

	if ((unsigned long long)count > 0xFFFFFF) {
		/* count is too large for SPACE 6 command */
		ltfsmsg(LTFS_INFO, 30450I, (int)count);
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SPACE));
		return EDEV_INVALID_ARG;
	}

	rc = _mt_command(device, cmd, "SPACE", count, &msg);
	lin_tape_ibmtape_readpos(device, pos);

	if (rc != DEVICE_GOOD) {
		lin_tape_ibmtape_process_errors(device, rc, msg, "space", true);
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SPACE));
	return rc;
}

int lin_tape_ibmtape_long_erase(void *device)
{
	struct sioc_pass_through spt;
	int rc;
	unsigned char cdb[6];
	unsigned char sense[MAXSENSE];
	char *msg;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	memset(&spt, 0, sizeof(spt));
	memset(cdb, 0, sizeof(cdb));
	memset(sense, 0, sizeof(sense));

	/* Prepare Data Buffer */
	spt.buffer_length = 0;
	spt.buffer = NULL;

	/* Prepare CDB */
	spt.cmd_length = sizeof(cdb);
	spt.cdb = cdb;
	spt.cdb[0] = 0x19;			/* SCSI erase code */
	spt.cdb[1] = 0x03;			/* set long bit and immed bit */
	spt.data_direction = SCSI_DATA_NONE;

	spt.timeout = ibm_tape_get_timeout(priv->timeouts, spt.cdb[0]);
	if (spt.timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Prepare sense buffer */
	spt.sense_length = sizeof(sense);
	spt.sense = sense;

	/* Invoke _ioctl to send a page */
	rc = sioc_paththrough(device, &spt, &msg);
	if (rc != DEVICE_GOOD)
		lin_tape_ibmtape_process_errors(device, rc, msg, "long erase", true);

	return rc;
}

/**
 * Erase tape from current position
 * @param device a pointer to the ibmtape backend
 * @param pos a pointer to position data. This function will update position infomation.
 * @param long_erase Set long bit and immed bit ON.
 * @return 0 on success or a negative value on error
 */
int lin_tape_ibmtape_erase(void *device, struct tc_position *pos, bool long_erase)
{
	int rc;
	char *msg;
	int fd = *((int *) device);
	struct request_sense sense_data;
	int progress;
	struct ltfs_timespec ts_start, ts_now;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_ERASE));
	if (long_erase) {
		ltfsmsg(LTFS_DEBUG, 30592D, "long erase", ((struct lin_tape_ibmtape *) device)->drive_serial);
		get_current_timespec(&ts_start);

		rc = lin_tape_ibmtape_long_erase(device);
		if (rc == -EDEV_TIME_STAMP_CHANGED) {
			ltfsmsg(LTFS_DEBUG, 30411D, "erase", -1, rc);
			rc = lin_tape_ibmtape_long_erase(device);
		}

		while (true) {
			rc = lin_tape_ibmtape_ioctlrc2err(device, fd , &sense_data, &msg);

			if (rc != -EDEV_OPERATION_IN_PROGRESS) {
				/* Erase operation is NOT in progress */
				if (rc == -EDEV_NO_SENSE)
					rc = DEVICE_GOOD;
				break;
			}

			if (IS_ENTERPRISE(priv->drive_type)) {
				get_current_timespec(&ts_now);
				ltfsmsg(LTFS_INFO, 30451I, (int)(ts_now.tv_sec - ts_start.tv_sec)/60);
			}
			else {
				progress = (int)(sense_data.field[0] & 0xFF)<<8;
				progress += (int)(sense_data.field[1] & 0xFF);
				ltfsmsg(LTFS_INFO, 30452I, progress*100/0xFFFF);
			}
			sleep(60);
		}

	}
	else {
		ltfsmsg(LTFS_DEBUG, 30592D, "erase", ((struct lin_tape_ibmtape *) device)->drive_serial);
		rc = _st_command(device, STERASE, "ERASE", 1, &msg);	// param=1 means invoking short erase.
	}

	lin_tape_ibmtape_readpos(device, pos);

	if (rc != DEVICE_GOOD) {
		lin_tape_ibmtape_process_errors(device, rc, msg, "erase", true);
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_ERASE));
	return rc;
}

/**
 * Load tape or rewind when a tape is already loaded
 * @param device a pointer to the ibmtape backend
 * @param pos a pointer to position data. This function will update position infomation.
 * @return 0 on success or a negative value on error
 */
int _lin_tape_ibmtape_load_unload(void *device, bool load, struct tc_position *pos)
{
	int rc;
	char *msg;
	bool take_dump = true;
	struct lin_tape_ibmtape *priv = ((struct lin_tape_ibmtape *) device);

	if (load) {
		rc = _mt_command(device, MTLOAD, "LOAD", 0, &msg);
	}
	else {
		rc = _mt_command(device, MTUNLOAD, "UNLOAD", 0, &msg);
	}

	if (rc != DEVICE_GOOD) {
		switch (rc) {
		case -EDEV_LOAD_UNLOAD_ERROR:
			if (priv->loadfailed) {
				take_dump = false;
			}
			else {
				priv->loadfailed = true;
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
		lin_tape_ibmtape_readpos(device, pos);
		lin_tape_ibmtape_process_errors(device, rc, msg, "load unload", take_dump);
	}
	else {
		if (load) {
			lin_tape_ibmtape_readpos(device, pos);
			priv->tape_alert = 0;
		}
		else {
			pos->partition = 0;
			pos->block = 0;
			priv->tape_alert = 0;
		}
		priv->loadfailed = false;
	}

	return rc;
}

int lin_tape_ibmtape_load(void *device, struct tc_position *pos)
{
	int rc;
	unsigned char buf[TC_MP_SUPPORTEDPAGE_SIZE];
	struct lin_tape_ibmtape *priv = ((struct lin_tape_ibmtape *) device);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_LOAD));
	ltfsmsg(LTFS_DEBUG, 30592D, "load", priv->drive_serial);

	memset(buf, 0, sizeof(buf));

	rc = _lin_tape_ibmtape_load_unload(device, true, pos);
	if (rc < 0) {
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_LOAD));
		return rc;
	}

	/* Check Cartridge type */
	rc = lin_tape_ibmtape_modesense(device, TC_MP_SUPPORTEDPAGE, TC_MP_PC_CURRENT, 0x00, buf, sizeof(buf));
	if (rc < 0) {
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_LOAD));
		return rc;
	}

	priv->loaded        = true;
	priv->is_worm       = false;

	priv->clear_by_pc     = false;
	priv->force_writeperm = DEFAULT_WRITEPERM;
	priv->force_readperm  = DEFAULT_READPERM;
	priv->write_counter = 0;
	priv->read_counter  = 0;
	priv->cart_type = buf[2];
	priv->density_code = buf[8];

	if (priv->cart_type == 0x00) {
		ltfsmsg(LTFS_WARN, 30453W);
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_LOAD));
		return 0;
	}

	rc = ibm_tape_is_supported_tape(priv->cart_type, priv->density_code, &(priv->is_worm));
	if(rc == -LTFS_UNSUPPORTED_MEDIUM)
		ltfsmsg(LTFS_INFO, 30455I, priv->cart_type, priv->density_code);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_LOAD));
	return rc;
}

/**
 * Unload tape
 * @param device a pointer to the ibmtape backend
 * @param pos a pointer to position data. This function will update position infomation.
 * @return 0 on success or a negative value on error
 */
int lin_tape_ibmtape_unload(void *device, struct tc_position *pos)
{
	int rc;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_UNLOAD));
	ltfsmsg(LTFS_DEBUG, 30592D, "unload", ((struct lin_tape_ibmtape *) device)->drive_serial);

	rc = _lin_tape_ibmtape_load_unload(device, false, pos);

	priv->clear_by_pc     = false;
	priv->force_writeperm = DEFAULT_WRITEPERM;
	priv->force_readperm  = DEFAULT_READPERM;
	priv->write_counter = 0;
	priv->read_counter  = 0;

	if (rc < 0) {
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_UNLOAD));
		return rc;
	} else {
		((struct lin_tape_ibmtape *)device)->loaded = false;
		((struct lin_tape_ibmtape *)device)->is_worm = false;
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_UNLOAD));
		return rc;
	}
}

int lin_tape_ibmtape_get_next_block_to_xfer(void *device, struct tc_position *pos)
{
	int rc;
	char *msg;
	struct read_tape_position rp;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_READPOS));
	memset(&rp, 0, sizeof(struct read_tape_position));

	rp.data_format = RP_EXTENDED_FORM;

	rc = _sioc_stioc_command(device, STIOC_READ_POSITION_EX, "READPOS EXT", &rp, &msg);

	if (rc == DEVICE_GOOD) {
		pos->partition = rp.rp_data.rp_extended.active_partition;
		pos->block = ltfs_betou64(rp.rp_data.rp_extended.last_logical_obj_position);
	}
	else {
		lin_tape_ibmtape_process_errors(device, rc, msg, "get block in buf", true);
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READPOS));
	return rc;
}

/**
 * Tell the current position
 * @param device a pointer to the ibmtape backend
 * @param pos a pointer to position data. This function will update position infomation.
 * @return 0 on success or a negative value on error
 */
int lin_tape_ibmtape_readpos(void *device, struct tc_position *pos)
{
	int rc;
	char *msg;
	struct read_tape_position rp;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_READPOS));
	memset(&rp, 0, sizeof(struct read_tape_position));

	rp.data_format = RP_LONG_FORM;

	rc = _sioc_stioc_command(device, STIOC_READ_POSITION_EX, "READPOS", &rp, &msg);

	if (rc == DEVICE_GOOD) {
		pos->early_warning = rp.rp_data.rp_long.eop? true : false;
		pos->programmable_early_warning = rp.rp_data.rp_long.bpew? true : false;
		pos->partition = rp.rp_data.rp_long.active_partition;
		pos->block = ltfs_betou64(rp.rp_data.rp_long.logical_obj_number);
		pos->filemarks = ltfs_betou64(rp.rp_data.rp_long.logical_file_id);

		ltfsmsg(LTFS_DEBUG, 30598D, "readpos", (unsigned long long)pos->partition,
				(unsigned long long)pos->block, (unsigned long long)pos->filemarks,
				((struct lin_tape_ibmtape *) device)->drive_serial);
	}
	else {
		lin_tape_ibmtape_process_errors(device, rc, msg, "readpos", true);
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READPOS));
	return rc;
}

/**
 * Make/Unmake partition
 * @param device a pointer to the ibmtape backend
 * @param format specify type of format
 * @param vol_name Volume name, unused by libtlfs (HPE extension)
 * @param vol_name Barcode name, unused by libtlfs (HPE extension)
 * @param vol_mam_uuid Volume UUID, unused by libtlfs (HPE extension)
 * @return 0 on success or a negative value on error
 */
int lin_tape_ibmtape_format(void *device, TC_FORMAT_TYPE format, const char *vol_name, const char *barcode_name, const char *vol_mam_uuid)
{
	struct sioc_pass_through spt;
	int rc, aux_rc;
	unsigned char cdb[6];
	unsigned char sense[MAXSENSE];
	unsigned char buf[TC_MP_SUPPORTEDPAGE_SIZE];
	char *msg;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_FORMAT));
	ltfsmsg(LTFS_DEBUG, 30592D, "format", ((struct lin_tape_ibmtape *) device)->drive_serial);

	if ((unsigned char) format >= (unsigned char) TC_FORMAT_MAX) {
		ltfsmsg(LTFS_INFO, 30456I, format);
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_FORMAT));
		return -1;
	}

	memset(&spt, 0, sizeof(spt));
	memset(cdb, 0, sizeof(cdb));
	memset(sense, 0, sizeof(sense));

	/* Prepare Data Buffer */
	spt.buffer_length = 0;
	spt.buffer = NULL;

	/* Prepare CDB */
	spt.cmd_length = sizeof(cdb);
	spt.cdb = cdb;
	spt.cdb[0] = 0x04;			/* SCSI medium format code */
	spt.cdb[2] = (unsigned char) format;
	spt.data_direction = SCSI_DATA_NONE;

	spt.timeout = ibm_tape_get_timeout(priv->timeouts, spt.cdb[0]);
	if (spt.timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Prepare sense buffer */
	spt.sense_length = sizeof(sense);
	spt.sense = sense;

	/* Invoke _ioctl to send a page */
	rc = sioc_paththrough(device, &spt, &msg);
	if (rc != DEVICE_GOOD)
		lin_tape_ibmtape_process_errors(device, rc, msg, "format", true);

	/* Check Cartridge type */
	aux_rc = lin_tape_ibmtape_modesense(device, TC_MP_SUPPORTEDPAGE, TC_MP_PC_CURRENT, 0x00, buf, sizeof(buf));
	if (!aux_rc) {
		priv->cart_type = buf[2];
		priv->density_code = buf[8];
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_FORMAT));
	return rc;
}

/**
 * Tell log data from the drive
 * @param device a pointer to the ibmtape backend
 * @param page page code of log sense
 * @param buf pointer to buffer to store log data
 * @param size length of the buffer
 * @return Page length on success or a negative value on error.
 */
#define MAX_UINT16 (0x0000FFFF)

int lin_tape_ibmtape_logsense(void *device, const uint8_t page, const uint8_t subpage,
							  unsigned char *buf, const size_t size)
{
	int rc;
	char *msg;
	struct log_sense10_page log_page;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_LOGSENSE));
	ltfsmsg(LTFS_DEBUG3, 30597D, "logsense", (unsigned long long)page, (unsigned long long)subpage,
			((struct lin_tape_ibmtape *) device)->drive_serial);

	log_page.page_code = page;
	log_page.subpage_code = subpage;
	log_page.len = 0;
	log_page.parm_pointer = 0;
	memset(log_page.data, 0, LOGSENSEPAGE);

	rc = _sioc_stioc_command(device, SIOC_LOG_SENSE10_PAGE, "LOGSENSE", &log_page, &msg);

	if (rc != DEVICE_GOOD) {
		lin_tape_ibmtape_process_errors(device, rc, msg, "logsense page", true);
		return rc;
	} else {
		memcpy(buf, log_page.data, size);
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_LOGSENSE));

	return log_page.len;
}

/**
 * Tell the remaining capacity
 * @param device a pointer to the ibmtape backend
 * @param cap pointer to teh capasity data. This function will update capasity infomation.
 * @return 0 on success or a negative value on error
 */
#define PARTITIOIN_REC_HEADER_LEN (4)

int lin_tape_ibmtape_remaining_capacity(void *device, struct tc_remaining_cap *cap)
{
	unsigned char logdata[LOGSENSEPAGE];
	unsigned char buf[32];
	int param_size, i;
	int length;
	int offset;
	uint32_t logcap;
	int rc;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_REMAINCAP));
	if (IS_LTO(priv->drive_type) && (DRIVE_GEN(priv->drive_type) == 0x05)) {
		/* Issue LogPage 0x31 */
		rc = lin_tape_ibmtape_logsense(device, LOG_TAPECAPACITY, (uint8_t)0, logdata, LOGSENSEPAGE);
		if (rc < 0) {
			ltfsmsg(LTFS_INFO, 30457I, LOG_TAPECAPACITY, rc);
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_REMAINCAP));
			return rc;
		}

		for(i = TAPECAP_REMAIN_0; i < TAPECAP_SIZE; i++) {
			if (parse_logPage(logdata, (uint16_t) i, &param_size, buf, sizeof(buf))
				|| param_size != sizeof(uint32_t)) {
				ltfsmsg(LTFS_INFO, 30458I);
				ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_REMAINCAP));
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
				ltfsmsg(LTFS_INFO, 30459I, i);
				ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_REMAINCAP));
				return -EDEV_INVALID_ARG;
				break;
			}
		}
	}
	else {
		/* Issue LogPage 0x17 */
		rc = lin_tape_ibmtape_logsense(device, LOG_VOLUMESTATS, (uint8_t)0, logdata, LOGSENSEPAGE);
		if (rc < 0) {
			ltfsmsg(LTFS_INFO, 30457I, LOG_VOLUMESTATS, rc);
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_REMAINCAP));
			return rc;
		}

		/* parse param 0x202 - nominal capacity of the partitions */
		if (parse_logPage(logdata, (uint16_t)VOLSTATS_PARTITION_CAP, &param_size, buf, sizeof(buf))) {
			ltfsmsg(LTFS_INFO, 30458I);
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_REMAINCAP));
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
			ltfsmsg(LTFS_INFO, 30458I);
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_REMAINCAP));
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

	ltfsmsg(LTFS_DEBUG3, 30597D, "capacity part0", (unsigned long long)cap->remaining_p0,
			(unsigned long long)cap->max_p0, ((struct lin_tape_ibmtape *) device)->drive_serial);
	ltfsmsg(LTFS_DEBUG3, 30597D, "capacity part1", (unsigned long long)cap->remaining_p1,
		(unsigned long long)cap->max_p1, ((struct lin_tape_ibmtape *) device)->drive_serial);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_REMAINCAP));
	return 0;
}


/**
 * Get mode data
 * @param device a pointer to the ibmtape backend
 * @param page a page id of mode data
 * @param pc page control value for mode sense command
 * @param buf pointer to mode page data. this function will update this data
 * @param size length of buf
 * @return 0 on success or a negative value on error
 */
int lin_tape_ibmtape_modesense(void *device, const uint8_t page, const TC_MP_PC_TYPE pc, const uint8_t subpage,
					  unsigned char *buf, const size_t size)
{
	struct sioc_pass_through spt;
	int rc;
	unsigned char cdb[10];
	unsigned char sense[MAXSENSE];
	char *msg;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_MODESENSE));
	ltfsmsg(LTFS_DEBUG3, 30593D, "modesense", page, ((struct lin_tape_ibmtape *) device)->drive_serial);

	memset(&spt, 0, sizeof(spt));
	memset(cdb, 0, sizeof(cdb));
	memset(sense, 0, sizeof(sense));

	/* Prepare Data Buffer */
	if (size > MAX_UINT16)
		spt.buffer_length = MAX_UINT16;
	else
		spt.buffer_length = size;
	spt.buffer = buf;

	/* Prepare CDB */
	spt.cmd_length = sizeof(cdb);
	spt.cdb = cdb;
	spt.cdb[0] = 0x5a;			/* SCSI mode sense code */
	spt.cdb[2] = pc | page;
	spt.cdb[3] = subpage;
	ltfs_u16tobe(spt.cdb + 7, spt.buffer_length);
	spt.data_direction = SCSI_DATA_IN;

	spt.timeout = ibm_tape_get_timeout(priv->timeouts, spt.cdb[0]);
	if (spt.timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Prepare sense buffer */
	spt.sense_length = sizeof(sense);
	spt.sense = sense;

	/* Invoke _ioctl to modesense */
	rc = sioc_paththrough(device, &spt, &msg);
	if (rc != DEVICE_GOOD)
		lin_tape_ibmtape_process_errors(device, rc, msg, "modesense", true);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_MODESENSE));
	return rc;
}

/**
 * Set mode data
 * @param device a pointer to the ibmtape backend
 * @param buf pointer to mode page data. This data will be sent to the drive
 * @param size length of buf
 * @return 0 on success or a negative value on error
 */
int lin_tape_ibmtape_modeselect(void *device, unsigned char *buf, const size_t size)
{
	struct sioc_pass_through spt;
	int rc;
	unsigned char cdb[10];
	unsigned char sense[MAXSENSE];
	char *msg;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_MODESELECT));
	ltfsmsg(LTFS_DEBUG3, 30592D, "modeselect", ((struct lin_tape_ibmtape *) device)->drive_serial);

	memset(&spt, 0, sizeof(spt));
	memset(cdb, 0, sizeof(cdb));
	memset(sense, 0, sizeof(sense));

	/* Prepare Data Buffer */
	spt.buffer_length = size;
	spt.buffer = buf;

	/* Prepare CDB */
	spt.cmd_length = sizeof(cdb);
	spt.cdb = cdb;
	spt.cdb[0] = 0x55;			/* SCSI mode select code */
	ltfs_u16tobe(spt.cdb + 7, spt.buffer_length);
	spt.data_direction = SCSI_DATA_OUT;

	spt.timeout = ibm_tape_get_timeout(priv->timeouts, spt.cdb[0]);
	if (spt.timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Prepare sense buffer */
	spt.sense_length = sizeof(sense);
	spt.sense = sense;

	/* Invoke _ioctl to modeselect */
	rc = sioc_paththrough(device, &spt, &msg);
	if (rc != DEVICE_GOOD) {
		if (rc == -EDEV_MODE_PARAMETER_ROUNDED)
			rc = DEVICE_GOOD;

		if (rc != DEVICE_GOOD)
			lin_tape_ibmtape_process_errors(device, rc, msg, "modeselect", true);
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_MODESELECT));
	return rc;
}

/**
 * Prevent medium removal
 * @param device a pointer to the ibmtape backend
 * @return 0 on success or a negative value on error
 */
int lin_tape_ibmtape_prevent_medium_removal(void *device)
{
	int rc;
	char *msg;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_PREVENTM));
	ltfsmsg(LTFS_DEBUG, 30592D, "prevent medium removal", ((struct lin_tape_ibmtape *) device)->drive_serial);

	rc = _sioc_stioc_command(device, STIOC_PREVENT_MEDIUM_REMOVAL, "PREVENT MED REMOVAL", NULL, &msg);

	if (rc != DEVICE_GOOD) {
		lin_tape_ibmtape_process_errors(device, rc, msg, "prevent medium removal", true);
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_PREVENTM));
	return rc;
}

/**
 * Allow medium removal
 * @param device a pointer to the ibmtape backend
 * @return 0 on success or a negative value on error
 */
int lin_tape_ibmtape_allow_medium_removal(void *device)
{
	int rc;
	char *msg;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_ALLOWMREM));
	ltfsmsg(LTFS_DEBUG, 30592D, "allow medium removal", ((struct lin_tape_ibmtape *) device)->drive_serial);

	rc = _sioc_stioc_command(device, STIOC_ALLOW_MEDIUM_REMOVAL, "ALLOW MED REMOVAL", NULL, &msg);

	if (rc != DEVICE_GOOD) {
		lin_tape_ibmtape_process_errors(device, rc, msg, "allow medium removal", true);
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_ALLOWMREM));
	return rc;
}

/**
 * Read attribute
 * @param device a pointer to the ibmtape backend
 * @param part partition to read attribute
 * @param id attribute id to get
 * @param buf pointer to the attribute buffer. This function will update this value.
 * @param size length of the buffer
 * @return 0 on success or a negative value on error
 */
int lin_tape_ibmtape_read_attribute(void *device, const tape_partition_t part, const uint16_t id,
						   unsigned char *buf, const size_t size)
{
	struct sioc_pass_through spt;
	int rc;
	unsigned char cdb[16];
	unsigned char sense[MAXSENSE];
	char *msg;
	bool take_dump= true;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_READATTR));
	ltfsmsg(LTFS_DEBUG3, 30597D, "readattr", (unsigned long long)part,
			(unsigned long long)id, ((struct lin_tape_ibmtape *) device)->drive_serial);

	memset(&spt, 0, sizeof(spt));
	memset(cdb, 0, sizeof(cdb));
	memset(sense, 0, sizeof(sense));

	/* Prepare Data Buffer */
	if (size == MAXMAM_SIZE)
		spt.buffer_length = MAXMAM_SIZE;
	else
		spt.buffer_length = size + 4;

	spt.buffer = calloc(1, spt.buffer_length);
	if (spt.buffer == NULL) {
		ltfsmsg(LTFS_ERR, 10001E, "lin_tape_ibmtape_read_attribute: data buffer");
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READATTR));
		return -EDEV_NO_MEMORY;
	}

	/* Prepare CDB */
	spt.cmd_length = sizeof(cdb);
	spt.cdb = cdb;
	spt.cdb[0] = 0x8C;			/* 0x8C SCSI read Attribute code */
	spt.cdb[1] = 0x00;			/* Service Action 0x00: VALUE */
	spt.cdb[7] = part;
	ltfs_u16tobe(spt.cdb + 8, id);
	ltfs_u32tobe(spt.cdb + 10, spt.buffer_length);
	spt.data_direction = SCSI_DATA_IN;

	spt.timeout = ibm_tape_get_timeout(priv->timeouts, spt.cdb[0]);
	if (spt.timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Prepare sense buffer */
	spt.sense_length = sizeof(sense);
	spt.sense = sense;

	/* Invoke _ioctl to Read Attribute */
	rc = sioc_paththrough(device, &spt, &msg);
	if (rc != DEVICE_GOOD) {
		if ( rc == -EDEV_INVALID_FIELD_CDB )
			take_dump = false;

		lin_tape_ibmtape_process_errors(device, rc, msg, "readattr", take_dump);

		if (rc < 0 &&
			id != TC_MAM_PAGE_COHERENCY &&
			id != TC_MAM_APP_VENDER &&
			id != TC_MAM_APP_NAME &&
			id != TC_MAM_APP_VERSION &&
			id != TC_MAM_USER_MEDIUM_LABEL &&
			id != TC_MAM_TEXT_LOCALIZATION_IDENTIFIER &&
			id != TC_MAM_BARCODE &&
			id != TC_MAM_APP_FORMAT_VERSION)
			ltfsmsg(LTFS_INFO, 30460I, rc);
	} else {
		if (size == MAXMAM_SIZE) {
			/* Include header if request size is MAXMAM_SIZE */
			memcpy(buf, spt.buffer, size);
		} else {
			memcpy(buf, (spt.buffer + 4), size);
		}

		free(spt.buffer);
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_READATTR));
	return rc;
}

/**
 * Write attribute
 * @param device a pointer to the ibmtape backend
 * @param part partition to read attribute
 * @param id attribute id to get
 * @param buf pointer to the attribute buffer. This function will send this value to the tape.
 *            This function expected this buffer does not contain attribute header.
 * @param size length of the buffer
 * @return 0 on success or a negative value on error
 */
int lin_tape_ibmtape_write_attribute(void *device, const tape_partition_t part, const unsigned char *buf,
							const size_t size)
{
	struct sioc_pass_through spt;
	int rc;
	unsigned char cdb[16];
	unsigned char sense[MAXSENSE];
	char *msg;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_WRITEATTR));
	ltfsmsg(LTFS_DEBUG3, 30594D, "writeattr", (unsigned long long)part,
			((struct lin_tape_ibmtape *) device)->drive_serial);

	memset(&spt, 0, sizeof(spt));
	memset(cdb, 0, sizeof(cdb));
	memset(sense, 0, sizeof(sense));

	/* Prepare Data Buffer */
	spt.buffer_length = size + 4;
	spt.buffer = calloc(1, spt.buffer_length);
	if (spt.buffer == NULL) {
		ltfsmsg(LTFS_ERR, 10001E, "lin_tape_ibmtape_write_attribute: data buffer");
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_WRITEATTR));
		return -EDEV_NO_MEMORY;
	}
	ltfs_u32tobe(spt.buffer, size);
	memcpy((spt.buffer + 4), buf, size);

	/* Prepare CDB */
	spt.cmd_length = sizeof(cdb);
	spt.cdb = cdb;
	spt.cdb[0] = 0x8D;			/* SCSI Write Attribute code */
	spt.cdb[1] = 0x01;			/* Write through bit on */
	spt.cdb[7] = part;
	ltfs_u32tobe(spt.cdb + 10, spt.buffer_length);
	spt.data_direction = SCSI_DATA_OUT;

	spt.timeout = ibm_tape_get_timeout(priv->timeouts, spt.cdb[0]);
	if (spt.timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Prepare sense buffer */
	spt.sense_length = sizeof(sense);
	spt.sense = sense;

	/* Invoke _ioctl to Write Attribute */
	rc = sioc_paththrough(device, &spt, &msg);
	if (rc != DEVICE_GOOD)
		lin_tape_ibmtape_process_errors(device, rc, msg, "writeattr", true);

	free(spt.buffer);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_WRITEATTR));
	return rc;
}

int lin_tape_ibmtape_allow_overwrite(void *device, const struct tc_position pos)
{
	int rc;
	char *msg;
	struct allow_data_overwrite append_pos;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_ALLOWOVERW));
	ltfsmsg(LTFS_DEBUG, 30597D, "allow overwrite", (unsigned long long)pos.partition,
		(unsigned long long)pos.block, ((struct lin_tape_ibmtape *) device)->drive_serial);

	memset(&append_pos, 0, sizeof(append_pos));

	append_pos.partition_number = pos.partition;
	append_pos.logical_block_id = pos.block;

	rc = _sioc_stioc_command(device, STIOC_ALLOW_DATA_OVERWRITE, "ALLOW OVERWRITE", &append_pos, &msg);

	if (rc != DEVICE_GOOD) {
		if (rc == -EDEV_EOD_DETECTED) {
			ltfsmsg(LTFS_DEBUG, 30448D, "Allow Overwrite");
			rc = DEVICE_GOOD;
		}

		if (rc != DEVICE_GOOD)
			lin_tape_ibmtape_process_errors(device, rc, msg, "allow overwrite", true);
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_ALLOWOVERW));
	return rc;
}

/**
 * GRAO command is currently unsupported on this device
 */
int lin_tape_ibmtape_grao(void *device, unsigned char *buf, const uint32_t len)
{
	int ret = -EDEV_UNSUPPORETD_COMMAND;
	return ret;
}

/**
 * RRAO command is currently unsupported on this device
 */
int lin_tape_ibmtape_rrao(void *device, unsigned char *buf, const uint32_t len, size_t *out_size)
{
	int ret = -EDEV_UNSUPPORETD_COMMAND;
	return ret;
}

/**
 * Set compression setting
 * @param device a pointer to the ibmtape backend
 * @param enable_compression enable: true, disable: false
 * @param pos a pointer to position data. This function will update position infomation.
 * @return 0 on success or a negative value on error
 */
int lin_tape_ibmtape_set_compression(void *device, const bool enable_compression, struct tc_position *pos)
{
	int rc;
	unsigned char buf[TC_MP_COMPRESSION_SIZE];
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_SETCOMPRS));
	rc = lin_tape_ibmtape_modesense(device, TC_MP_COMPRESSION, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
	if (rc != DEVICE_GOOD) {
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETCOMPRS));
		return rc;
	}

	buf[0] = 0x00;
	buf[1] = 0x00;
	if(enable_compression)
		buf[18] |= 0x80; /* Set DCE field*/
	else
		buf[18] &= 0x7F; /* Unset DCE field*/

	rc = lin_tape_ibmtape_modeselect(device, buf, sizeof(buf));

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETCOMPRS));
	return rc;
}

/**
 * Return drive setting in default
 * @param device a pointer to the ibmtape backend
 * @return 0 on success or a negative value on error
 */
int lin_tape_ibmtape_set_default(void *device)
{
	int rc;
	unsigned char buf[TC_MP_READ_WRITE_CTRL_SIZE];
	char *msg;
	struct stchgp_s param;
	struct eot_warn eot;
	int retry;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_SETDEFAULT));
	/* Disable Read across EOD on 3592 drive */
	if (IS_ENTERPRISE(priv->drive_type)) {
		ltfsmsg(LTFS_DEBUG, 30592D, __FUNCTION__, "Disabling read across EOD");
		rc = lin_tape_ibmtape_modesense(device, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
		if (rc != DEVICE_GOOD) {
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
			return rc;
		}

		buf[0]  = 0x00;
		buf[1]  = 0x00;
		buf[24] = 0x0C;

		rc = lin_tape_ibmtape_modeselect(device, buf, sizeof(buf));
		if (rc != DEVICE_GOOD) {
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
			return rc;
		}
	}

	/* set SILI bit */
	ltfsmsg(LTFS_DEBUG, 30592D, __FUNCTION__, "Setting SILI bit");
	while (true) {
		memset(&param, 0, sizeof(struct stchgp_s));
		rc = _sioc_stioc_command(device, STIOCQRYP, "GET PARAM", &param, &msg);
		if (rc != DEVICE_GOOD) {
			lin_tape_ibmtape_process_errors(device, rc, msg, "get default parameter", true);
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
			return rc;
		}

		param.read_sili_bit = true;

		rc = _sioc_stioc_command(device, STIOCSETP, "SET PARAM", &param, &msg);
		if (rc == DEVICE_GOOD || retry > 10)
			break;

		/* In case of STIOCSETP error, reopen the device and retry */
		close(priv->fd);
		priv->fd = open(priv->devname, O_RDWR | O_NDELAY);

		if (priv->fd < 0) {
			priv->fd = open(priv->devname, O_RDONLY | O_NDELAY);
			if (priv->fd < 0) {
				ltfsmsg(LTFS_INFO, 30425I, priv->devname, errno);
				rc = -EDEV_DEVICE_UNOPENABLE;
				break;
			}
			ltfsmsg(LTFS_WARN, 30426W, priv->devname);
		}
		retry ++;
	}

	if (rc != DEVICE_GOOD) {
		lin_tape_ibmtape_process_errors(device, rc, msg, "set default parameter", true);
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
		return rc;
	}

	/* set logical block protection */
	if (global_data.crc_checking) {
		ltfsmsg(LTFS_DEBUG, 30592D, __FUNCTION__, "Setting LBP");
		rc = lin_tape_ibmtape_set_lbp(device, true);
	} else {
		ltfsmsg(LTFS_DEBUG, 30592D, __FUNCTION__, "Resetting LBP");
		rc = lin_tape_ibmtape_set_lbp(device, false);
	}
	if (rc != DEVICE_GOOD) {
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
		return rc;
	}

	/* EOT handling */
	memset(&eot, 0, sizeof(struct eot_warn));
	rc = _sioc_stioc_command(device, STIOC_QUERY_EOT_WARN, "GET EOT WARN", &eot, &msg);
	if (rc != DEVICE_GOOD) {
		lin_tape_ibmtape_process_errors(device, rc, msg, "get default parameter (EOT handling)", true);
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
		return rc;
	}

	if (eot.warn == 0) {
		eot.warn = 1;
		rc = _sioc_stioc_command(device, STIOC_SET_EOT_WARN, "SET EOT WARN", &eot, &msg);
		if (rc != DEVICE_GOOD) {
			lin_tape_ibmtape_process_errors(device, rc, msg, "set default parameter (EOT handling)", true);
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
			return rc;
		}
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETDEFAULT));
	return rc;
}

/**
 * Get cartridge health information
 * @param device a pointer to the ibmtape backend
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

int lin_tape_ibmtape_get_cartridge_health(void *device, struct tc_cartridge_health *cart_health)
{
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	unsigned char logdata[LOGSENSEPAGE];
	unsigned char buf[16];
	int param_size, i;
	uint64_t loghlt;
	int rc;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETCARTHLTH));

	/* Issue LogPage 0x37 */
	cart_health->tape_efficiency  = UNSUPPORTED_CARTRIDGE_HEALTH;
	rc = lin_tape_ibmtape_logsense(device, LOG_PERFORMANCE, (uint8_t)0, logdata, LOGSENSEPAGE);
	if (rc < 0)
		ltfsmsg(LTFS_INFO, 30461I, LOG_PERFORMANCE, rc, "get cart health");
	else {
		for(i = 0; i < (int)((sizeof(perfstats)/sizeof(perfstats[0]))); i++) { /* BEAM: loop doesn't iterate - Use loop for future enhancement. */
			if (parse_logPage(logdata, perfstats[i], &param_size, buf, 16)) {
				ltfsmsg(LTFS_INFO, 30462I, LOG_PERFORMANCE, "get cart health");
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

	rc = lin_tape_ibmtape_logsense(device, LOG_VOLUMESTATS, (uint8_t)0, logdata, LOGSENSEPAGE);
	if (rc < 0)
		ltfsmsg(LTFS_INFO, 30461I, LOG_VOLUMESTATS, rc, "get cart health");
	else {
		for(i = 0; i < (int)((sizeof(volstats)/sizeof(volstats[0]))); i++) {
			if (parse_logPage(logdata, volstats[i], &param_size, buf, 16)) {
				ltfsmsg(LTFS_INFO, 30462I, LOG_VOLUMESTATS, "get cart health");
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

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETCARTHLTH));
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
int lin_tape_ibmtape_get_tape_alert(void *device, uint64_t *tape_alert)
{
	unsigned char logdata[LOGSENSEPAGE];
	unsigned char buf[16];
	int param_size, i;
	int rc;
	uint64_t ta;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETTAPEALT));
	/* Issue LogPage 0x2E */
	ta = 0;
	rc = lin_tape_ibmtape_logsense(device, LOG_TAPE_ALERT, (uint8_t)0, logdata, LOGSENSEPAGE);
	if (rc < 0)
		ltfsmsg(LTFS_INFO, 30461I, LOG_TAPE_ALERT, rc, "get tape alert");
	else {
		for(i = 1; i <= 64; i++) {
			if (parse_logPage(logdata, (uint16_t) i, &param_size, buf, 16)
				|| param_size != sizeof(uint8_t)) {
				ltfsmsg(LTFS_INFO, 30462I, LOG_TAPE_ALERT, "get tape alert");
				ta = 0;
			}

			if(buf[0])
				ta += (uint64_t)(1) << (i - 1);
		}
	}

	priv->tape_alert |= ta;
	*tape_alert = priv->tape_alert;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETTAPEALT));
	return rc;
}

/**
 * clear latched tape alert from the drive
 * @param device Device handle returned by the backend's open().
 * @param tape_alert value to clear tape alert. Backend shall be clear the specicied bits in this value.
 * @return 0 on success or a negative value on error.
 */
int lin_tape_ibmtape_clear_tape_alert(void *device, uint64_t tape_alert)
{
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_CLRTAPEALT));
	priv->tape_alert &= ~tape_alert;
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_CLRTAPEALT));
	return 0;
}

/**
 * Get drive parameter
 * @param device a pointer to the ibmtape backend
 * @param drive_param pointer to the drive parameter infomation. This function will update this value.
 * @return 0 on success or a negative value on error
 */
 uint32_t _lin_tape_ibmtape_get_block_limits(void *device)
{
	struct sioc_pass_through spt;
	int rc;
	unsigned char cdb[6];
	unsigned char buf[6];
	unsigned char sense[MAXSENSE];
	char *msg;
	uint32_t length = 0;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfsmsg(LTFS_DEBUG, 30592D, "read block limits", ((struct lin_tape_ibmtape *) device)->drive_serial);

	memset(&spt, 0, sizeof(spt));
	memset(cdb, 0, sizeof(cdb));
	memset(sense, 0, sizeof(sense));
	memset(buf, 0, sizeof(buf));

	/* Prepare Data Buffer */
	spt.buffer_length = sizeof(buf);
	spt.buffer = buf;

	/* Prepare CDB (Never issue long erase) */
	spt.cmd_length = sizeof(cdb);
	spt.cdb = cdb;
	spt.cdb[0] = 0x05;			/* SCSI read block limits code */
	spt.data_direction = SCSI_DATA_IN;

	spt.timeout = ibm_tape_get_timeout(priv->timeouts, spt.cdb[0]);
	if (spt.timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Prepare sense buffer */
	spt.sense_length = sizeof(sense);
	spt.sense = sense;

	/* Invoke _ioctl to Erase */
	rc = sioc_paththrough(device, &spt, &msg);
	if (rc != DEVICE_GOOD)
		lin_tape_ibmtape_process_errors(device, rc, msg, "read block limits", true);
	else {
		length = (buf[1] << 16) + (buf[2] << 8) + (buf[3] & 0xFF);
		if(length > 1 * MB)
			length = 1 * MB;
	}

	return length;
}

int lin_tape_ibmtape_get_parameters(void *device, struct tc_drive_param *params)
{
	int rc;
	unsigned char buf[TC_MP_MEDIUM_SENSE_SIZE];
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETPARAM));

	memset(params, 0x00, sizeof(struct tc_drive_param));

	if (global_data.crc_checking)
		params->max_blksize = MIN(_lin_tape_ibmtape_get_block_limits(device),  LINUX_MAX_BLOCK_SIZE - 4);
	else
		params->max_blksize = MIN(_lin_tape_ibmtape_get_block_limits(device),  LINUX_MAX_BLOCK_SIZE);

	if (priv->loaded) {
		params->write_protect = 0;

		if (IS_ENTERPRISE(priv->drive_type)) {
			rc = lin_tape_ibmtape_modesense(device, TC_MP_MEDIUM_SENSE, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
			if (rc != DEVICE_GOOD) {
				ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETPARAM));
				return rc;
			}

			char wp_flag = buf[26];

			if (wp_flag & 0x80) {
				params->write_protect |= VOL_PHYSICAL_WP;
			} else if (wp_flag & 0x01) {
				params->write_protect |= VOL_PERM_WP;
			} else if (wp_flag & 0x10) {
				params->write_protect |= VOL_PERS_WP;
			}
		} else {
			rc = lin_tape_ibmtape_modesense(device, 0x00, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
			if (rc != DEVICE_GOOD) {
				ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETPARAM));
				return rc;
			}

			if ( (buf[3] & 0x80) )
				params->write_protect |= VOL_PHYSICAL_WP;
		}

		params->cart_type = priv->cart_type;
		params->density   = priv->density_code;

		/* TODO: Following field shall be implemented in the future */
		/*
		params->is_worm = priv->is_worm;
		if (IS_ENTERPRISE(priv->drive_type)) {
			if (priv->density_code & TEST_CRYPTO)
				params->is_encrypted = true;
		} else {
			// TODO: Store is_crypto based on LP17:200h
		}
		*/
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETPARAM));
	return rc;
}

static const char *generate_product_name(const char *product_id)
{
	const char *product_name = "";
	int i = 0;

	for (i = 0; ibm_supported_drives[i]; ++i) {
		if (strncmp(ibm_supported_drives[i]->product_id, product_id, strlen(ibm_supported_drives[i]->product_id)) == 0) {
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
#define DEV_LIST_FILE "/proc/scsi/IBMtape"
int lin_tape_ibmtape_get_device_list(struct tc_drive_info *buf, int count)
{
	FILE *list;
	int  i = 0, n, dev;
	char line[1024];
	char *name, *model, *sn, *cur;

	list = fopen(DEV_LIST_FILE, "r");
	if (!list) {
		/* Failed to open file '%s' (%d) */
		ltfsmsg(LTFS_ERR, 30463E, DEV_LIST_FILE, errno);
		return i;
	}

	while (fgets(line, sizeof(line), list) != NULL) {
		cur = line;
		cur = strtok(cur, " ");
		if(! cur ) continue;
		name = cur;
		cur += strlen(cur) + 1;
		while(*cur == ' ') cur++;

		cur = strtok(cur, " ");
		if(! cur ) continue;
		model = cur;
		cur += strlen(cur) + 1;
		while(*cur == ' ') cur++;

		cur = strtok(cur, " ");
		if(! cur ) continue;
		sn = cur;

		n = sscanf(name, "%d", &dev);
		if (n == 1) {
			if (buf && i < count) {
				snprintf(buf[i].name, TAPE_DEVNAME_LEN_MAX, "/dev/IBMtape%d", dev);
				snprintf(buf[i].vendor, TAPE_VENDOR_NAME_LEN_MAX, "IBM");
				snprintf(buf[i].model, TAPE_MODEL_NAME_LEN_MAX, "%s", model);
				snprintf(buf[i].serial_number, TAPE_SERIAL_LEN_MAX, "%s", sn);
				snprintf(buf[i].product_name, PRODUCT_NAME_LENGTH, "%s", generate_product_name(model));
				buf[i].host    = 0;
				buf[i].channel = 0;
				buf[i].target  = 0;
				buf[i].lun     = -1;
			}
			i++;
		}
	}

	fclose(list);

	return i;
}

/**
 * Set the capacity proprotion of the medium
 * @param device a pointer to the ibmtape backend
 * @param proportion specify the proportion
 * @return 0 on success or a negative value on error
 */
int lin_tape_ibmtape_setcap(void *device, uint16_t proportion)
{
	struct sioc_pass_through spt;
	int rc;
	unsigned char cdb[6];
	unsigned char sense[MAXSENSE];
	char *msg;
	unsigned char buf[TC_MP_MEDIUM_SENSE_SIZE];
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_SETCAP));
	if (IS_ENTERPRISE(priv->drive_type)) {
		memset(buf, 0, sizeof(buf));

		/* Scale media instead of setcap */
		rc = lin_tape_ibmtape_modesense(device, TC_MP_MEDIUM_SENSE, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
		if (rc < 0) {
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETCAP));
			return rc;
		}

		/* Check Cartridge type */
		if (IS_SHORT_MEDIUM(buf[2]) || IS_WORM_MEDIUM(buf[2])) {
			/* Short or WORM cartridge cannot be scaled */
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETCAP));
			return DEVICE_GOOD;
		} else {
			return rc;
		}

		buf[0]   = 0x00;
		buf[1]   = 0x00;
		buf[27] |= 0x01;
		buf[28]  = 0x00;

		rc = lin_tape_ibmtape_modeselect(device, buf, sizeof(buf));
	} else {

		memset(&spt, 0, sizeof(spt));
		memset(cdb, 0, sizeof(cdb));
		memset(sense, 0, sizeof(sense));

		/* Prepare Data Buffer */
		spt.buffer_length = 0;
		spt.buffer = NULL;

		/* Prepare CDB */
		spt.cmd_length = sizeof(cdb);
		spt.cdb = cdb;
		spt.cdb[0] = 0x0B;			/* SCSI medium set capacity code */
		spt.cdb[3] = (unsigned char) (proportion >> 8);
		spt.cdb[4] = (unsigned char) (proportion & 0xFF);
		spt.data_direction = SCSI_DATA_NONE;

		spt.timeout = ibm_tape_get_timeout(priv->timeouts, spt.cdb[0]);
		if (spt.timeout < 0)
			return -EDEV_UNSUPPORETD_COMMAND;

		/* Prepare sense buffer */
		spt.sense_length = sizeof(sense);
		spt.sense = sense;

		/* Invoke _ioctl to send a page */
		rc = sioc_paththrough(device, &spt, &msg);
		if (rc != DEVICE_GOOD)
			lin_tape_ibmtape_process_errors(device, rc, msg, "setcap", true);
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETCAP));
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

int lin_tape_ibmtape_get_eod_status(void *device, int part)
{
	/*
	 * This feature requires new tape drive firmware
	 * to support logpage 17h correctly
	 */

	unsigned char logdata[LOGSENSEPAGE];
	unsigned char buf[16] = {0};
	int param_size, rc;
	unsigned int i;
	uint32_t part_cap[2] = {EOD_UNKNOWN, EOD_UNKNOWN};
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETEODSTAT));
	/* Issue LogPage 0x17 */
	rc = lin_tape_ibmtape_logsense(device, LOG_VOL_STATISTICS, (uint8_t)0, logdata, LOGSENSEPAGE);
	if (rc < 0) {
		ltfsmsg(LTFS_WARN, 30464W, LOG_VOL_STATISTICS, rc);
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETEODSTAT));
		return EOD_UNKNOWN;
	}

	/* Parse Approximate used native capacity of partitions (0x203)*/
	if (parse_logPage(logdata, (uint16_t)LOG_VOL_USED_CAPACITY, &param_size, buf, sizeof(buf))
		|| (param_size != sizeof(buf) ) ) {
		ltfsmsg(LTFS_WARN, 30465W);
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETEODSTAT));
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
			ltfsmsg(LTFS_WARN, 30466W, i, part_buf, len);

		i += (len + 1);
	}

	/* Create return value */
	if(part_cap[part] == 0xFFFFFFFF)
		rc = EOD_MISSING;
	else
		rc = EOD_GOOD;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETEODSTAT));
	return rc;
}

/**
 * Get vendor unique backend xattr
 * @param device Device handle returned by the backend's open().
 * @param name   Name of xattr
 * @param buf    On success, fill this value with the pointer of data buffer for xattr
 * @return 0 on success or a negative value on error.
 */
int lin_tape_ibmtape_get_xattr(void *device, const char *name, char **buf)
{
	unsigned char logdata[LOGSENSEPAGE];
	unsigned char logbuf[16];
	int param_size;
	int rc = -LTFS_NO_XATTR;
	uint32_t value32;
	struct ltfs_timespec now;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETXATTR));
	if (! strcmp(name, "ltfs.vendor.IBM.mediaCQsLossRate")) {
		rc = DEVICE_GOOD;

		/* If first fetch or cache value is too old and valuie is dirty, refetch value */
		get_current_timespec(&now);
		if ( priv->fetch_sec_acq_loss_w == 0 ||
			 ((priv->fetch_sec_acq_loss_w + 60 < now.tv_sec) && priv->dirty_acq_loss_w)) {
			rc = lin_tape_ibmtape_logsense(device, LOG_PERFORMANCE, LOG_PERFORMANCE_CAPACITY_SUB,
										   logdata, LOGSENSEPAGE);
			if (rc < 0)
				ltfsmsg(LTFS_INFO, 30461I, LOG_PERFORMANCE, rc, "get xattr");
			else {
				if (parse_logPage(logdata, PERF_ACTIVE_CQ_LOSS_W, &param_size, logbuf, 16)) {
					ltfsmsg(LTFS_INFO, 30462I, LOG_PERFORMANCE, "get xattr");
					rc = -LTFS_NO_XATTR;
				} else {
					switch(param_size) {
					case sizeof(uint32_t):
						value32 = (uint32_t)ltfs_betou32(logbuf);
						priv->acq_loss_w = (float)value32 / 65536.0;
						priv->fetch_sec_acq_loss_w = now.tv_sec;
						priv->dirty_acq_loss_w = false;
						break;
					default:
						ltfsmsg(LTFS_INFO, 30467I, param_size);
						rc = -LTFS_NO_XATTR;
						break;
					}
				}
			}
		}

		if(rc == DEVICE_GOOD) {
			/* The buf allocated here shall be freed in xattr_get_virtual() */
			rc = asprintf(buf, "%2.2f", priv->acq_loss_w);
			if (rc < 0) {
				rc = -LTFS_NO_MEMORY;
				ltfsmsg(LTFS_INFO, 30468I, "getting active CQ loss write");
			}
			else
				rc = DEVICE_GOOD;
		} else
			priv->fetch_sec_acq_loss_w = 0;
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETXATTR));
	return rc;
}

/**
 * Set vendor unique backend xattr
 * @param device Device handle returned by the backend's open().
 * @param name   Name of xattr
 * @param buf    Data buffer to set the value
 * @param size   Length of data buffer
 * @return 0 on success or a negative value on error.
 */
int lin_tape_ibmtape_set_xattr(void *device, const char *name, const char *buf, size_t size)
{
	int rc = -LTFS_NO_XATTR;
	char *null_terminated;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;
	int64_t perm_count = 0;

	if (!size)
		return -LTFS_BAD_ARG;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_SETXATTR));

	null_terminated = malloc(size + 1);
	if (! null_terminated) {
		ltfsmsg(LTFS_ERR, 10001E, "lin_tape_ibmtape_set_xattr: null_term");
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
		rc = DEVICE_GOOD;
	} else if (! strcmp(name, "ltfs.vendor.IBM.forceErrorType")) {
		priv->force_errortype = strtol(null_terminated, NULL, 0);
		rc = DEVICE_GOOD;
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
		rc = DEVICE_GOOD;
	}
	free(null_terminated);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETXATTR));
	return rc;
}

void lin_tape_ibmtape_help_message(const char *progname)
{
	ltfsresult(30599I, lin_tape_ibmtape_default_device);
}

const char *lin_tape_ibmtape_default_device_name(void)
{
	const char *devname;

	devname = lin_tape_ibmtape_default_device;

	return devname;
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

	ltfsmsg(LTFS_DEBUG, 30592D, title, s);
}

static bool is_ame(void *device)
{
	unsigned char buf[TC_MP_READ_WRITE_CTRL_SIZE] = {0};
	const int rc = lin_tape_ibmtape_modesense(device, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));

	if (rc != 0) {
		char message[100] = {0};
		sprintf(message, "failed to get MP %02Xh (%d)", TC_MP_READ_WRITE_CTRL, rc);
		ltfsmsg(LTFS_DEBUG, 30592D, __FUNCTION__, message);

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
		ltfsmsg(LTFS_DEBUG, 30592D, __FUNCTION__, message);

		if (encryption_method != 0x50) {
			ltfsmsg(LTFS_ERR, 30469E, method, encryption_method);
		}
		return encryption_method == 0x50;
	}
}

static int is_encryption_capable(void *device)
{
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	if (IS_ENTERPRISE(priv->drive_type)) {
		ltfsmsg(LTFS_ERR, 30470E, priv->drive_type);
		return -EDEV_INTERNAL_ERROR;
	}

	if (! is_ame(device))
		return -EDEV_INTERNAL_ERROR;

	return DEVICE_GOOD;
}

/**
 * Security protocol out (SPOUT)
 * @param device a pointer to the ibmtape backend
 * @param sps a security protocol specific
 * @param buf pointer to spout. This data will be sent to the drive
 * @param size length of buf
 * @return 0 on success or a negative value on error
 */
int lin_tape_ibmtape_security_protocol_out(void *device, const uint16_t sps, unsigned char *buf, const size_t size)
{
	struct sioc_pass_through spt = {0};
	unsigned char cdb[12] = {0};
	unsigned char sense[MAXSENSE] = {0};
	char *msg = NULL;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfsmsg(LTFS_DEBUG, 30592D, "Security Protocol Out (SPOUT)", ((struct lin_tape_ibmtape *) device)->drive_serial);

	/* Prepare Data Buffer */
	spt.buffer_length = size;
	spt.buffer = buf;

	/* Prepare CDB */
	spt.cmd_length = sizeof(cdb);
	spt.cdb = cdb;
	spt.cdb[0] = 0xB5; /* SCSI SECURITY PROTOCOL OUT */
	spt.cdb[1] = 0x20; /* Tape Data Encryption security protocol */
	ltfs_u16tobe(spt.cdb + 2, sps); /* SECURITY PROTOCOL SPECIFIC */
	ltfs_u32tobe(spt.cdb + 6, spt.buffer_length);
	spt.data_direction = SCSI_DATA_OUT;

	spt.timeout = ibm_tape_get_timeout(priv->timeouts, spt.cdb[0]);
	if (spt.timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Prepare sense buffer */
	spt.sense_length = sizeof(sense);
	spt.sense = sense;

	/* Invoke _ioctl to modeselect */
	const int rc = sioc_paththrough(device, &spt, &msg);
	if (rc != DEVICE_GOOD) {
		lin_tape_ibmtape_process_errors(device, rc, msg, "security protocol out", true);
	}

	return rc;
}

int lin_tape_ibmtape_set_key(void *device, const unsigned char * const keyalias, const unsigned char * const key)
{
	/*
	 * Encryption  Decryption     Key         DKi      keyalias
	 *    Mode        Mode
	 * 0h Disable  0h Disable  Prohibited  Prohibited    NULL
	 * 2h Encrypt  3h Mixed    Mandatory    Optional    !NULL
	 */
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_SETKEY));

	int rc = is_encryption_capable(device);
	if (rc < 0) {
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETKEY));
		return rc;
	}

	const uint16_t sps = 0x10;
	const size_t size = keyalias ? 20 + DK_LENGTH + 4 + DKI_LENGTH : 20;
	unsigned char *buffer = calloc(size, sizeof(unsigned char));

	if (! buffer) {
		rc = -ENOMEM;
		goto out;
	}

	unsigned char buf[TC_MP_READ_WRITE_CTRL_SIZE] = {0};
	rc = lin_tape_ibmtape_modesense(device, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
	if (rc != DEVICE_GOOD)
		goto free;

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
			rc = -EINVAL;
			goto free;
		}
		memcpy(buffer + 20, key, DK_LENGTH); /* LOGICAL BLOCK ENCRYPTION KEY */
		buffer[20 + DK_LENGTH] = 0x01; /* KEY DESCRIPTOR TYPE: 01h DKi (Data Key Identifier) */
		ltfs_u16tobe(buffer + 20 + DK_LENGTH + 2, DKI_LENGTH);
		memcpy(buffer + 20 + 0x20 + 4, keyalias, DKI_LENGTH);
	}

	const char * const title = "set key:";
	ltfsmsg_keyalias(title, keyalias);

	rc = lin_tape_ibmtape_security_protocol_out(device, sps, buffer, size);
	if (rc != DEVICE_GOOD)
		goto free;

	((struct lin_tape_ibmtape *) device)->is_data_key_set = keyalias != NULL;

	memset(buf, 0, sizeof(buf));
	rc = lin_tape_ibmtape_modesense(device, TC_MP_READ_WRITE_CTRL, TC_MP_PC_CURRENT, 0, buf, sizeof(buf));
	if (rc != DEVICE_GOOD)
		goto free;

free:
	free(buffer);

out:
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_SETKEY));
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

	ltfsmsg(LTFS_DEBUG, 30592D, title, s);
}

int lin_tape_ibmtape_get_keyalias(void *device, unsigned char **keyalias) /* This is not IBM method but T10 method. */
{
	int i;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETKEYALIAS));

	int rc = is_encryption_capable(device);
	if (rc < 0) {
		ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETKEYALIAS));
		return rc;
	}

	static const int page_header_length = 4;
	struct sioc_pass_through spt = { .buffer_length = page_header_length };

	memset(((struct lin_tape_ibmtape*) device)->dki, 0, sizeof(((struct lin_tape_ibmtape*) device)->dki));
	*keyalias = NULL;

	/*
	 * 1st loop: Get the page length.
	 * 2nd loop: Get full data in the page.
	 */
	for (i = 0; i < 2; ++i) {
		/* Prepare Data Buffer */
		free(spt.buffer);
		spt.buffer = (unsigned char*) calloc(spt.buffer_length, sizeof(unsigned char));
		if (spt.buffer == NULL) {
			ltfsmsg(LTFS_ERR, 10001E, "lin_tape_ibmtape_get_keyalias: data buffer");
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETKEYALIAS));
			return -EDEV_NO_MEMORY;
		}

		/* Prepare CDB */
		unsigned char cdb[12] = {0};
		spt.cmd_length = sizeof(cdb);
		spt.cdb = cdb;
		spt.cdb[0] = 0xA2; /* operation code: SECURITY PROTOCOL IN */
		spt.cdb[1] = 0x20; /* security protocol */
		spt.cdb[3] = 0x21; /* security protocol specific: Next Block Encryption Status page */
		ltfs_u32tobe(spt.cdb + 6, spt.buffer_length); /* allocation length */
		spt.data_direction = SCSI_DATA_IN;

		spt.timeout = ibm_tape_get_timeout(priv->timeouts, spt.cdb[0]);
		if (spt.timeout < 0)
			return -EDEV_UNSUPPORETD_COMMAND;

		/* Prepare sense buffer */
		unsigned char sense[MAXSENSE] = {0};
		spt.sense_length = sizeof(sense);
		spt.sense = sense;

		/* Invoke _ioctl to get key-alias */
		char *msg = NULL;
		rc = sioc_paththrough(device, &spt, &msg);

		if (rc != DEVICE_GOOD) {
			lin_tape_ibmtape_process_errors(device, rc, msg, "get key-alias", true);
			free(spt.buffer);
			ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETKEYALIAS));
			return rc;
		}

		show_hex_dump("SPIN:", spt.buffer, spt.buffer_length);

		spt.buffer_length = page_header_length + ltfs_betou16(spt.buffer + 2);
	}

	const unsigned char encryption_status = spt.buffer[12] & 0xF;
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
		while (offset + 4 <= spt.buffer_length && spt.buffer[offset] != 1) {
			offset += ltfs_betou16(spt.buffer + offset + 2) + 4;
		}
		if (offset + 4 <= spt.buffer_length && spt.buffer[offset] == 1) {
			const uint dki_length = ((int) spt.buffer[offset + 2]) << 8 | spt.buffer[offset + 3];
			if (offset + 4 + dki_length <= spt.buffer_length) {
				int n = dki_length < sizeof(((struct lin_tape_ibmtape*) device)->dki) ? dki_length :
					sizeof(((struct lin_tape_ibmtape*) device)->dki);
				memcpy(((struct lin_tape_ibmtape*) device)->dki, &spt.buffer[offset + 4], n);
				*keyalias = ((struct lin_tape_ibmtape*) device)->dki;
			}
		}
	}

	const char * const title = "get key-alias:";
	ltfsmsg_keyalias(title, ((struct lin_tape_ibmtape*) device)->dki);

	free(spt.buffer);
	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETKEYALIAS));
	return rc;
}

#define TC_MP_INIT_EXT_LBP_RS         (0x40)
#define TC_MP_INIT_EXT_LBP_CRC32C     (0x20)

int lin_tape_ibmtape_set_lbp(void *device, bool enable)
{
	struct logical_block_protection lbp;
	char *msg;
	unsigned char lbp_method = LBP_DISABLE;
	unsigned char buf[TC_MP_INIT_EXT_SIZE];
	int rc = DEVICE_GOOD;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	memset(buf, 0, sizeof(buf));

	/* Check logical block protection capability */
	rc = lin_tape_ibmtape_modesense(device, TC_MP_INIT_EXT, TC_MP_PC_CURRENT, 0x00, buf, sizeof(buf));
	if (rc < 0)
		return rc;

	if (buf[0x12] & TC_MP_INIT_EXT_LBP_CRC32C)
		lbp_method = CRC32C_CRC;
	else
		lbp_method = REED_SOLOMON_CRC;

	/* set logical block protection */
	ltfsmsg(LTFS_DEBUG, 30593D, "LBP Enable", enable, "");
	ltfsmsg(LTFS_DEBUG, 30593D, "LBP Method", lbp_method, "");
	memset(&lbp, 0, sizeof(struct logical_block_protection));
	rc = _sioc_stioc_command(device, STIOC_QUERY_BLK_PROTECTION, "GET LBP", &lbp, &msg);
	if (rc != DEVICE_GOOD) {
		lin_tape_ibmtape_process_errors(device, rc, msg, "get lbp", true);
		return rc;
	}

	if (enable && lbp.lbp_capable) {
		lbp.lbp_method      = lbp_method;
		lbp.lbp_info_length = 4;
		lbp.lbp_w           = 1;
		lbp.lbp_r           = 1;
	} else
		lbp.lbp_method = LBP_DISABLE;

	rc = _sioc_stioc_command(device, STIOC_SET_BLK_PROTECTION, "SET LBP", &lbp, &msg);
	if (rc != DEVICE_GOOD) {
		lin_tape_ibmtape_process_errors(device, rc, msg, "set lbp", true);
		return rc;
	}

	if (enable && lbp.lbp_capable) {
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
		ltfsmsg(LTFS_INFO, 30471I);
	} else {
		priv->f_crc_enc   = NULL;
		priv->f_crc_check = NULL;
		ltfsmsg(LTFS_INFO, 30472I);
	}

	return rc;
}

int lin_tape_ibmtape_is_mountable(void *device, const char *barcode, const unsigned char cart_type,
						  const unsigned char density)
{
	int ret;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_ISMOUNTABLE));

	ret = ibm_tape_is_mountable( priv->drive_type,
								barcode,
								cart_type,
								density,
								global_data.strict_drive);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_ISMOUNTABLE));

	return ret;
}

bool lin_tape_ibmtape_is_readonly(void *device)
{
	int ret;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *)device;

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

/**
 * Read buffer
 * @param device a pointer to the ibmtape backend
 * @param id
 * @param buf
 * @param offset
 * @param len
 * @param type
 * @return 0 on success or negative value on error
 */
int lin_tape_ibmtape_readbuffer(void *device, int id, unsigned char *buf, size_t offset, size_t len,
					   int type)
{
	struct sioc_pass_through spt;
	unsigned char cdb[10];
	unsigned char sense[MAXSENSE];
	char *msg;
	int rc;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfsmsg(LTFS_DEBUG, 30593D, "read buffer", id, ((struct lin_tape_ibmtape *) device)->drive_serial);

	memset(&spt, 0, sizeof(spt));
	memset(cdb, 0, sizeof(cdb));
	memset(sense, 0, sizeof(sense));

	/* Prepare Data Buffer */
	spt.buffer_length = len;
	spt.buffer = (unsigned char *) buf;
	memset(spt.buffer, 0, spt.buffer_length);

	/* Prepare CDB */
	spt.cmd_length = sizeof(cdb);
	spt.cdb = cdb;
	spt.cdb[0] = 0x3c;			/* SCSI ReadBuffer(10) Code */
	spt.cdb[1] = type;
	spt.cdb[2] = id;
	spt.cdb[3] = (unsigned char) (offset >> 16);
	spt.cdb[4] = (unsigned char) (offset >> 8);
	spt.cdb[5] = (unsigned char) (offset & 0xFF);
	spt.cdb[6] = (unsigned char) (len >> 16);
	spt.cdb[7] = (unsigned char) (len >> 8);
	spt.cdb[8] = (unsigned char) (len & 0xFF);

	spt.data_direction = SCSI_DATA_IN;

	spt.timeout = ibm_tape_get_timeout(priv->timeouts, spt.cdb[0]);
	if (spt.timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Prepare sense buffer */
	spt.sense_length = sizeof(sense);
	spt.sense = sense;

	/* Invoke SCSI command and check error */
	rc = sioc_paththrough(device, &spt, &msg);
	if (rc != DEVICE_GOOD)
		lin_tape_ibmtape_process_errors(device, rc, msg, "read buffer", false);

	return rc;
}

/**
 * Take drive dump
 * @param device a pointer to the ibmtape backend
 * @param fname a file name of dump
 * @return 0 on success or negative value on error
 */
#define DUMP_HEADER_SIZE   (4)
#define DUMP_TRANSFER_SIZE (512 * KB)

int lin_tape_ibmtape_getdump_drive(void *device, const char *fname)
{
	long long data_length, buf_offset;
	int dumpfd = -1;
	int transfer_size, num_transfers, excess_transfer;
	int rc = 0;
	int i, bytes;
	int buf_id;
	unsigned char cap_buf[DUMP_HEADER_SIZE];
	unsigned char *dump_buf;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfsmsg(LTFS_INFO, 30478I, fname);

	/* Set transfer size */
	transfer_size = DUMP_TRANSFER_SIZE;
	dump_buf = calloc(1, DUMP_TRANSFER_SIZE);
	if (dump_buf == NULL) {
		ltfsmsg(LTFS_ERR, 10001E, "lin_tape_ibmtape_getdump_drive: dump buffer");
		return -EDEV_NO_MEMORY;
	}

	/* Set buffer ID */
	if (IS_ENTERPRISE(priv->drive_type)) {
		buf_id = 0x00;
	} else {
		buf_id = 0x01;
	}

	/* Get buffer capacity */
	lin_tape_ibmtape_readbuffer(device, buf_id, cap_buf, 0, sizeof(cap_buf), 0x03);
	data_length = (cap_buf[1] << 16) + (cap_buf[2] << 8) + (int) cap_buf[3];

	/* Open dump file for write only */
	dumpfd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (dumpfd < 0) {
		rc = -errno;
		ltfsmsg(LTFS_WARN, 30479W, rc);
		free(dump_buf);
		return rc;
	}

	/* get the total number of transfers */
	num_transfers = data_length / transfer_size;
	excess_transfer = data_length % transfer_size;
	if (excess_transfer)
		num_transfers += 1;

	/* Total dump data length is %lld. Total number of transfers is %d. */
	ltfsmsg(LTFS_DEBUG, 30480D, data_length);
	ltfsmsg(LTFS_DEBUG, 30481D, num_transfers);

	/* start to transfer data */
	buf_offset = 0;
	i = 0;
	ltfsmsg(LTFS_DEBUG, 30482D);
	while (num_transfers) {
		int length;

		i++;

		/* Allocation Length is transfer_size or excess_transfer */
		if (excess_transfer && num_transfers == 1)
			length = excess_transfer;
		else
			length = transfer_size;

		rc = lin_tape_ibmtape_readbuffer(device, buf_id, dump_buf, buf_offset, length, 0x02);
		if (rc) {
			ltfsmsg(LTFS_WARN, 30483W, rc);
			free(dump_buf);
			close(dumpfd);
			return rc;
		}

		/* write buffer data into dump file */
		bytes = write(dumpfd, dump_buf, length);
		if (bytes == -1) {
			rc = -errno;
			ltfsmsg(LTFS_WARN, 30484W, rc);
			free(dump_buf);
			close(dumpfd);
			return rc;
		}

		ltfsmsg(LTFS_DEBUG, 30485D, i, bytes);
		if (bytes != length) {
			ltfsmsg(LTFS_WARN, 30486W, bytes, length);
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
 * @param device a pointer to the ibmtape backend
 * @return 0 on success or negative value on error
 */
#define SENDDIAG_BUF_LEN (8)
int lin_tape_ibmtape_forcedump_drive(void *device)
{
	struct sioc_pass_through spt;
	unsigned char cdb[6];
	unsigned char buf[SENDDIAG_BUF_LEN];
	unsigned char sense[MAXSENSE];
	int rc;
	char *msg;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfsmsg(LTFS_DEBUG, 30593D, "force dump", 0, ((struct lin_tape_ibmtape *) device)->drive_serial);

	memset(&spt, 0, sizeof(spt));
	memset(cdb, 0, sizeof(cdb));
	memset(sense, 0, sizeof(sense));

	/* Prepare Data Buffer */
	spt.buffer_length = SENDDIAG_BUF_LEN;
	spt.buffer = buf;
	memset(spt.buffer, 0, spt.buffer_length);

	/* Prepare CDB */
	spt.cmd_length = sizeof(cdb);
	spt.cdb = cdb;
	spt.cdb[0] = 0x1d;			/* SCSI Send Diag Code */
	spt.cdb[1] = 0x10;			/* PF bit is set to 1 */
	spt.cdb[3] = 0x00;
	spt.cdb[4] = 0x08;			/* parameter length is 0x0008 */

	/* Prepare payload */
	spt.buffer[0] = 0x80;		/* page code */
	spt.buffer[2] = 0x00;
	spt.buffer[3] = 0x04;		/* page length */
	spt.buffer[4] = 0x01;
	spt.buffer[5] = 0x60;		/* diagnostic id */

	spt.data_direction = SCSI_DATA_OUT;

	spt.timeout = ibm_tape_get_timeout(priv->timeouts, spt.cdb[0]);
	if (spt.timeout < 0)
		return -EDEV_UNSUPPORETD_COMMAND;

	/* Prepare sense buffer */
	spt.sense_length = sizeof(sense);
	spt.sense = sense;

	/* Invoke SCSI command and check error */
	rc = sioc_paththrough(device, &spt, &msg);
	if (rc != DEVICE_GOOD)
		lin_tape_ibmtape_process_errors(device, rc, msg, "force dump", false);

	return rc;
}

/**
 * Take normal drive dump and forces drive dump
 * @param device a pointer to the ibmtape backend
 * @return 0 on success or negative value on error
 */
int lin_tape_ibmtape_takedump_drive(void *device, bool nonforced_dump)
{
	char fname_base[1024];
	char fname[1024];
	time_t now;
	struct tm *tm_now;
	unsigned char *serial = ((struct lin_tape_ibmtape *) device)->drive_serial;
	struct lin_tape_ibmtape *priv = (struct lin_tape_ibmtape *) device;

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_TAKEDUMPDRV));

	/* Make base filename */
	time(&now);
	tm_now = localtime(&now);
	sprintf(fname_base, DMP_DIR "/ltfs_%s_%d_%02d%02d_%02d%02d%02d", serial, tm_now->tm_year + 1900,
			tm_now->tm_mon + 1, tm_now->tm_mday, tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);

	if (nonforced_dump) {
		strcpy(fname, fname_base);
		strcat(fname, ".dmp");

		ltfsmsg(LTFS_INFO, 30487I);
		lin_tape_ibmtape_getdump_drive(device, fname);
	}

	ltfsmsg(LTFS_INFO, 30488I);
	lin_tape_ibmtape_forcedump_drive(device);
	strcpy(fname, fname_base);
	strcat(fname, "_f.dmp");
	lin_tape_ibmtape_getdump_drive(device, fname);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_TAKEDUMPDRV));
	return 0;
}

/* This function should be called after the cartridge is loaded. */
int lin_tape_ibmtape_get_worm_status(void *device, bool *is_worm)
{
	int rc = 0;
	struct lin_tape_ibmtape *priv = ((struct lin_tape_ibmtape *) device);

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_ENTER(REQ_TC_GETWORMSTAT));
	if (priv->loaded) {
		*is_worm = priv->is_worm;
	} else {
		ltfsmsg(LTFS_INFO, 30489I);
		*is_worm = false;
		rc = -1;
	}

	ltfs_profiler_add_entry(priv->profiler, NULL, TAPEBEND_REQ_EXIT(REQ_TC_GETWORMSTAT));
	return rc;
}

struct tape_ops lin_tape_ibmtape_drive_handler = {
	.open                   = lin_tape_ibmtape_open,
	.reopen                 = lin_tape_ibmtape_reopen,
	.close                  = lin_tape_ibmtape_close,
	.close_raw              = lin_tape_ibmtape_close_raw,
	.is_connected           = lin_tape_ibmtape_is_connected,
	.inquiry                = lin_tape_ibmtape_inquiry,
	.inquiry_page           = lin_tape_ibmtape_inquiry_page,
	.test_unit_ready        = lin_tape_ibmtape_test_unit_ready,
	.read                   = lin_tape_ibmtape_read,
	.write                  = lin_tape_ibmtape_write,
	.writefm                = lin_tape_ibmtape_writefm,
	.rewind                 = lin_tape_ibmtape_rewind,
	.locate                 = lin_tape_ibmtape_locate,
	.space                  = lin_tape_ibmtape_space,
	.erase                  = lin_tape_ibmtape_erase,
	.load                   = lin_tape_ibmtape_load,
	.unload                 = lin_tape_ibmtape_unload,
	.readpos                = lin_tape_ibmtape_readpos,
	.setcap                 = lin_tape_ibmtape_setcap,
	.format                 = lin_tape_ibmtape_format,
	.remaining_capacity     = lin_tape_ibmtape_remaining_capacity,
	.logsense               = lin_tape_ibmtape_logsense,
	.modesense              = lin_tape_ibmtape_modesense,
	.modeselect             = lin_tape_ibmtape_modeselect,
	.reserve_unit           = lin_tape_ibmtape_reserve_unit,
	.release_unit           = lin_tape_ibmtape_release_unit,
	.prevent_medium_removal = lin_tape_ibmtape_prevent_medium_removal,
	.allow_medium_removal   = lin_tape_ibmtape_allow_medium_removal,
	.write_attribute        = lin_tape_ibmtape_write_attribute,
	.read_attribute         = lin_tape_ibmtape_read_attribute,
	.allow_overwrite        = lin_tape_ibmtape_allow_overwrite,
	.grao                   = lin_tape_ibmtape_grao,
	.rrao                   = lin_tape_ibmtape_rrao,
	// May be command combination
	.set_compression        = lin_tape_ibmtape_set_compression,
	.set_default            = lin_tape_ibmtape_set_default,
	.get_cartridge_health   = lin_tape_ibmtape_get_cartridge_health,
	.get_tape_alert         = lin_tape_ibmtape_get_tape_alert,
	.clear_tape_alert       = lin_tape_ibmtape_clear_tape_alert,
	.get_xattr              = lin_tape_ibmtape_get_xattr,
	.set_xattr              = lin_tape_ibmtape_set_xattr,
	.get_parameters         = lin_tape_ibmtape_get_parameters,
	.get_eod_status         = lin_tape_ibmtape_get_eod_status,
	.get_device_list        = lin_tape_ibmtape_get_device_list,
	.help_message           = lin_tape_ibmtape_help_message,
	.parse_opts             = lin_tape_ibmtape_parse_opts,
	.default_device_name    = lin_tape_ibmtape_default_device_name,
	.set_key                = lin_tape_ibmtape_set_key,
	.get_keyalias           = lin_tape_ibmtape_get_keyalias,
	.takedump_drive         = lin_tape_ibmtape_takedump_drive,
	.is_mountable           = lin_tape_ibmtape_is_mountable,
	.get_worm_status        = lin_tape_ibmtape_get_worm_status,
	.get_serialnumber       = lin_tape_ibmtape_get_serialnumber,
	.get_info               = lin_tape_ibmtape_get_info,
	.set_profiler           = lin_tape_ibmtape_set_profiler,
	.get_next_block_to_xfer = lin_tape_ibmtape_get_next_block_to_xfer,
	.is_readonly            = lin_tape_ibmtape_is_readonly,
};

struct tape_ops *tape_dev_get_ops(void)
{
	return &lin_tape_ibmtape_drive_handler;
}

extern char tape_linux_lin_tape_dat[];

const char *tape_dev_get_message_bundle_name(void **message_data)
{
	*message_data = tape_linux_lin_tape_dat;
	return "tape_linux_lin_tape";
}
