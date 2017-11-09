/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2017 IBM Corp.
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
** FILE NAME:       tape_drivers/ibm_tape.h
**
** DESCRIPTION:     Definitions of handling IBM tape devices
**
** AUTHOR:          Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/

#include <stdlib.h>
#include <errno.h>

#include "tape_drivers/spc_op_codes.h"
#include "tape_drivers/ssc_op_codes.h"
#include "tape_drivers/tape_drivers.h"

#include "libltfs/ltfslogging.h"
#include "libltfs/ltfs_error.h"

#ifndef __ibm_tape_h

#define __ibm_tape_h

#ifdef __cplusplus
extern "C" {
#endif

extern struct error_table standard_tape_errors[];
extern struct error_table ibm_tape_errors[];

static inline int _sense2errorcode(uint32_t sense, struct error_table *table, char **msg, uint32_t mask)
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
				*msg = (char*)(table[i].msg);
			break;
		}
		i++;
	}

	if (table[i].err_code == -EDEV_RECOVERED_ERROR)
		rc = DEVICE_GOOD;
	else if (table[i].sense == 0xFFFFFF && table[i].err_code == rc && msg)
		*msg = (char*)(table[i].msg);

	return rc;
}

#define TAPE_FAMILY_MASK       (0xF000)
#define TAPE_FAMILY_ENTERPRISE (0x1000)
#define TAPE_FAMILY_LTO        (0x2000)
#define TAPE_FAMILY_ARCHIVE    (0x4000)

#define TAPE_FORMFACTOR_MASK   (0x0F00)
#define TAPE_FORMFACTOR_FULL   (0x0100)
#define TAPE_FORMFACTOR_HALF   (0x0200)

#define TAPE_GEN_MASK          (0x00FF)

#define IS_ENTERPRISE(type)    (type & TAPE_FAMILY_ENTERPRISE)
#define IS_LTO(type)           (type & TAPE_FAMILY_LTO)
#define IS_FULL_HEIGHT(type)   (type & TAPE_FORMFACTOR_FULL)
#define IS_HALF_HEIGHT(type)   (type & TAPE_FORMFACTOR_HALF)
#define DRIVE_FAMILY_GEN(type) (type & (TAPE_GEN_MASK | TAPE_FAMILY_MASK) )
#define DRIVE_GEN(type)        (type & TAPE_GEN_MASK)

enum {
	DRIVE_UNSUPPORTED = 0x0000, /* Unsupported drive */
	DRIVE_LTO5        = 0x2105, /* IBM Ultrium Gen 5 */
	DRIVE_LTO5_HH     = 0x2205, /* IBM Ultrium Gen 5 Half-High */
	DRIVE_LTO6        = 0x2106, /* IBM Ultrium Gen 6 */
	DRIVE_LTO6_HH     = 0x2206, /* IBM Ultrium Gen 6 Half-High */
	DRIVE_LTO7        = 0x2107, /* IBM Ultrium Gen 7 */
	DRIVE_LTO7_HH     = 0x2207, /* IBM Ultrium Gen 7 Half-High */
	DRIVE_LTO8        = 0x2108, /* IBM Ultrium Gen 8 */
	DRIVE_LTO8_HH     = 0x2208, /* IBM Ultrium Gen 8 Half-High */
	DRIVE_TS1140      = 0x1104, /* TS1140 */
	DRIVE_TS1150      = 0x1105, /* TS1150 */
	DRIVE_TS1155      = 0x5105, /* TS1155 */
};

enum {
	DRIVE_GEN_UNKNOWN = 0,
	DRIVE_GEN_LTO5    = 0x2005,
	DRIVE_GEN_LTO6    = 0x2006,
	DRIVE_GEN_LTO7    = 0x2007,
	DRIVE_GEN_LTO8    = 0x2008,
	DRIVE_GEN_JAG4    = 0x1004,
	DRIVE_GEN_JAG5    = 0x1005,
	DRIVE_GEN_JAG5A   = 0x5005,
};

typedef struct {
	int drive_generation;
	int cartridge_type;
	int density_code;
	int access;
} DRIVE_DENSITY_SUPPORT_MAP;

/* For remaining capacity */
#define LOG_VOLUMESTATS         (0x17)
enum {
	VOLSTATS_MOUNTS           = 0x0001,	/* < Volume Mounts */
	VOLSTATS_WRITTEN_DS       = 0x0002,	/* < Volume Written DS */
	VOLSTATS_WRITE_TEMPS      = 0x0003,	/* < Volume Temp Errors on Write */
	VOLSTATS_WRITE_PERMS      = 0x0004,	/* < Volume Perm Errors_on Write */
	VOLSTATS_READ_DS          = 0x0007,	/* < Volume Read DS */
	VOLSTATS_READ_TEMPS       = 0x0008,	/* < Volume Temp Errors on Read */
	VOLSTATS_READ_PERMS       = 0x0009,	/* < Volume Perm Errors_on Read */
	VOLSTATS_WRITE_PERMS_PREV = 0x000C,	/* < Volume Perm Errors_on Write (previous mount)*/
	VOLSTATS_READ_PERMS_PREV  = 0x000D,	/* < Volume Perm Errors_on Read (previous mount) */
	VOLSTATS_WRITE_MB         = 0x0010,	/* < Volume Written MB */
	VOLSTATS_READ_MB          = 0x0011,	/* < Volume Read MB */
	VOLSTATS_PASSES_BEGIN     = 0x0101,	/* < Beginning of medium passes */
	VOLSTATS_PASSES_MIDDLE    = 0x0102,	/* < Middle of medium passes */
	VOLSTATS_ENCRYPTED_REC    = 0x0200,	/* < First encrypted logical object identifier */
	VOLSTATS_PARTITION_CAP    = 0x0202,	/* < Native capacity of partitions */
	VOLSTATS_PART_USED_CAP    = 0x0203,	/* < Used capacity of partitions */
	VOLSTATS_PART_REMAIN_CAP  = 0x0204,	/* < Remaining capacity of partitions */
};

enum {
	NO_WP         = 0x00,
	PARMANENT_WP  = 0x01,
	ASSOCIATED_WP = 0x02,
	PERSISTENT_WP = 0x03,
};

#define PARTITIOIN_REC_HEADER_LEN (4)

#define LOG_TAPECAPACITY         0x31
#define LOG_TAPECAPACITY_SIZE    (32)

enum {
	TAPECAP_REMAIN_0 = 0x0001, /*< Partition0 Remaining Capacity */
	TAPECAP_REMAIN_1 = 0x0002, /*< Partition1 Remaining Capacity */
	TAPECAP_MAX_0    = 0x0003, /*< Partition0 MAX Capacity */
	TAPECAP_MAX_1    = 0x0004, /*< Partition1 MAX Capacity */
	TAPECAP_SIZE     = 0x0005,
};

#define MODE_DEVICE_CONFIG           (0x10) // ModePage 0x10 (Device Configuration)
#define MODE_DEVICE_CONFIG_SIZE      (32)

#define SENDDIAG_BUF_LEN             (8)
#define PRO_BUF_LEN                  (0x18)
#define PRI_BUF_HEADER               (0x08) // Header of PRI
#define PRI_BUF_LEN                  (0xF8) // Initial buffer size (Header + 5 x full info)

enum pro_type {
	PRO_TYPE_NONE          = 0x00,
	PRO_TYPE_EXCLUSIVE     = 0x03,
	PRO_TYPE_EX_REGISTANTS = 0x06
};

enum pro_action {
	PRO_ACT_REGISTER        = 0x00,
	PRO_ACT_RESERVE         = 0x01,
	PRO_ACT_RELEASE         = 0x02,
	PRO_ACT_CLEAR           = 0x03,
	PRO_ACT_PREENPT         = 0x04,
	PRO_ACT_PREEMPT_ABORT   = 0x05,
	PRO_ACT_REGISTER_IGNORE = 0x06,
	PRO_ACT_REGISTER_MOVE   = 0x07
};

extern DRIVE_DENSITY_SUPPORT_MAP jaguar_drive_density[];
extern DRIVE_DENSITY_SUPPORT_MAP jaguar_drive_density_strict[];
extern DRIVE_DENSITY_SUPPORT_MAP lto_drive_density[];
extern DRIVE_DENSITY_SUPPORT_MAP lto_drive_density_strict[];
extern const unsigned char supported_cart[];
extern const unsigned char supported_density[];

extern int num_jaguar_drive_density;
extern int num_jaguar_drive_density_strict;
extern int num_lto_drive_density;
extern int num_lto_drive_density_strict;
extern int num_supported_cart;
extern int num_supported_density;

#define IBM_VENDOR_ID "IBM"
#define LOGSENSEPAGE 1024     /* max data xfer for log sense page ioctl */

int  ibm_tape_init_timeout(struct timeout_tape **table, int type);
void ibm_tape_destroy_timeout(struct timeout_tape **table);
int  ibm_tape_get_timeout(struct timeout_tape *table, int op_code);

int ibmtape_is_mountable(const int drive_type,
						 const char *barcode,
						 const unsigned char cart_type,
						 const unsigned char density_code,
						 const bool strict);

int ibmtape_is_supported_tape(unsigned char type, unsigned char density, bool *is_worm);

#define PRI_FULL_LEN_BASE (24)

#define KEYLEN (8)
#define KEY_PREFIX_HOST (0x10)
#define KEY_PREFIX_IPV4 (0x40)
#define KEY_PREFIX_IPV6 (0x60)

struct reservation_info {
	unsigned char key_type;
	char hint[64];             /* The longest length is last 7-bytes of IPv6 */
	unsigned char key[KEYLEN]; /* Raw key */
	unsigned char wwid[8];     /* WWPN */
};

int ibmtape_genkey(unsigned char *key);
int ibmtape_parsekey(unsigned char *key, struct reservation_info *r);

extern struct supported_device *ibm_supported_drives[];
extern struct supported_device *usb_supported_drives[];

#ifdef __cplusplus
}
#endif

#endif // __ibm_tape_h

/************************************************************************************

Following is the supported command which is used in the IBM tape device.

E: Enterprise Tape Only
L: LTO tape Only

SPC
---
E S2  40 CHANGE_DEFINITION
E SPC 83 COPY_OPERATION_ABORT
E SPC 83 XCOPY
  SPC 12 INQUIRY
  SPC 4C LOG_SELECT
  SPC 4D LOG_SENSE
  SPC 15 MODE_SELECT6
  SPC 55 MODE_SELECT10
  SPC 1A MODE_SENSE6
  SPC 5A MODE_SENSE10
  SPC 5E PERSISTENT_RESERVE_IN
  SPC 5F PERSISTENT_RESERVE_OUT
  SPC 8C READ_ATTRIBUTE
  SPC 3C READ_BUFFER
  SPC 1C RECEIVE_DIAGNOSTIC_RESULTS
  SPC 17 RELEASE_UNIT6
L SPC 57 RELEASE_UNIT10
  SPC A0 REPORT_LUNS
  SPC 03 REQUEST_SENSE
  SPC 16 RESERVE_UNIT6
L SPC 56 RESERVE_UNIT10
  SPC A2 SPIN
  SPC B5 SPOUT
  SPC 1D SEND_DIAGNOSTIC
  SPC 00 TEST_UNIT_READY
  SPC 8D WRITE_ATTRIBUTE
  SPC 3B WRITE_BUFFER

  XXX 84 3RD_PARTY_COPY_IN
E SPC 84 [05]     RECEIVE_COPY_STATUS
  XXX A3 MAINTENANCE_IN
  SPC A3 [0C]     REPORT_SUPPORTED_OPERATION_CODE
  SPC A3 [0D]     REPORT_SUPPORTED_TASK_MANAGEMENT_FUNCTIONS
  SPC A3 [0F]     REPORT_TIMESTAMP
  XXX A4 MAINTENANCE_OUT
  SPC A4 [0F]     SET_TIMESTAMP

SSC
---
  SSC 82 ALLOW_OVERWRITE
E VU  C0 DISPLAY_MESSAGE
  SSC 19 ERASE
  SSC 04 FORMAT_MEDIUM
  SSC 1B LOAD/UNLOAD
  SSC 2B LOCATE10
L SSC 92 LOCATE16
  SSC 1E PREVENT_ALLOW_MEDIUM_REMOVAL
  SSC 08 READ
  SSC 05 READ_BLOCK_LIMITS
  VU  D1 READ_DYNAMIC_RUNTIME_ATTRIBUTE
  SSC 34 READ_POSITION
E SSC 0F READ_REVERSE
E SSC 14 RECOVER_BUFFERED_DATA
  SSC 44 REPORT_DENSITY_SUPPORT
  SSC 01 REWIND
L SSC 0B SET_CAPACITY
  SSC 11 SPACE6
  SSC 91 SPACE16
E VU  E3 STRING_SEARCH
  SSC 13 VERIFY
  SSC 0A WRITE
  VU  D2 WRITE_DYNAMIC_RUNTIME_ATTRIBUTE
  SSC 10 WRITE_FILEMARKS6

  XXX A3 MAINTENANCE_IN
  SSC A3 [1F][45] READ_END_OF_WRAP_POSITION
  SSC A3 [1F][01] READ_LOGGED_IN_HOST_TABLE
E SSC A3 [1D]     RECEIVE_RECOMMENDED_ACCESS_ORDER
  XXX A4 MAINTENANCE_OUT
E SSC A4 [1D]     GENERATE_RECOMMENDED_ACCESS_ORDER

************************************************************************************/
