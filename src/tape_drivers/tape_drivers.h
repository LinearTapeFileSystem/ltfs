/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2023 IBM Corp. All rights reserved.
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
** FILE NAME:       tape_drivers/tape_drivers.h
**
** DESCRIPTION:     Prototypes for common tape operations.
**
** AUTHOR:          Yutaka Oishi
**                  IBM Yamato, Japan
**                  OISHI@jp.ibm.com
**
**                  Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/

#include <stdint.h>

#include "libltfs/uthash.h"
#include "libltfs/tape_ops.h"

#include "libltfs/ltfslogging.h"
#include "libltfs/ltfs_error.h"

#ifndef __tape_drivers_h
#define __tape_drivers_h

#define KB   (1024)
#define MB   (KB * 1024)
#define GB   (MB * 1024)

#define REDPOS_LONG_LEN           (32)
#define REDPOS_EXT_LEN            (32)

#define RSOC_BUF_SIZE             (4 * KB)
#define RSOC_ENT_SIZE             (20)
#define RSOC_HEADER_SIZE          (4)
#define RSOC_RECOM_TO_OFFSET      (16)

#ifndef MAXSENSE
#define MAXSENSE                   (255)
#endif

#define MAXLP_SIZE             (0xFFFF)
#define MAXMAM_SIZE            (0xFFFF)

#define MASK_WITH_SENSE_KEY    (0xFFFFFF)
#define MASK_WITHOUT_SENSE_KEY (0x00FFFF)

typedef void  (*crc_enc)(void *buf, size_t n);
typedef int   (*crc_check)(void *buf, size_t n);
typedef void* (*memcpy_crc_enc)(void *dest, const void *src, size_t n);
typedef int   (*memcpy_crc_check)(void *dest, const void *src, size_t n);


#define THRESHOLD_FORCE_WRITE_NO_WRITE  (20)
#define DEFAULT_WRITEPERM               (0)
#define DEFAULT_READPERM                (0)
#define DEFAULT_ERRORTYPE               (0)

struct timeout_tape {
	int  op_code;     /**< SCSI op code */
	int  timeout;     /**< SCSI timeout */
	UT_hash_handle hh;
};

struct error_table {
	uint32_t sense;     /**< SCSI sense data */
	int      err_code;  /**< LTFS internal error code */
	char     *msg;      /**< Description of error */
};

static inline int _sense2errorcode(uint32_t sense, struct error_table *table, char **msg, uint32_t mask)
{
	int rc = -EDEV_UNKNOWN;
	int i;

	if (msg)
		*msg = NULL;

	if (!table)
		return rc;

	if ( (sense & 0xFFFF00) == 0x044000 )
		sense = 0x044000;
	else if ( (sense & 0xFFFF00) == 0x048000 ) /* 04/8xxx in TS3100/TS3200 */
		sense = 0x048000;
	else if ( (sense & 0xFFFF00) == 0x0B4100 ) /* 0B/41xx in TS2900 */
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

struct supported_device {
	char vendor_id[VENDOR_ID_LENGTH + 1];
	char product_id[PRODUCT_ID_LENGTH + 1];
	int  drive_type;
	char product_name[PRODUCT_NAME_LENGTH + 1];
};

#define TAPEDRIVE(v, p, t, n) &(struct supported_device){ v, p, t, n }

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
	VENDOR_UNKNOWN = 0,
	VENDOR_IBM,
	VENDOR_HP,
	VENDOR_QUANTUM,
};

enum {
	DRIVE_UNSUPPORTED = 0x0000, /* Unsupported drive */
	DRIVE_LTO5        = 0x2105, /* Ultrium Gen 5 */
	DRIVE_LTO5_HH     = 0x2205, /* Ultrium Gen 5 Half-High */
	DRIVE_LTO6        = 0x2106, /* Ultrium Gen 6 */
	DRIVE_LTO6_HH     = 0x2206, /* Ultrium Gen 6 Half-High */
	DRIVE_LTO7        = 0x2107, /* Ultrium Gen 7 */
	DRIVE_LTO7_HH     = 0x2207, /* Ultrium Gen 7 Half-High */
	DRIVE_LTO8        = 0x2108, /* Ultrium Gen 8 */
	DRIVE_LTO8_HH     = 0x2208, /* Ultrium Gen 8 Half-High */
	DRIVE_LTO9        = 0x2109, /* Ultrium Gen 9 */
	DRIVE_LTO9_HH     = 0x2209, /* Ultrium Gen 9 Half-High */
	DRIVE_TS1140      = 0x1104, /* TS1140 */
	DRIVE_TS1150      = 0x1105, /* TS1150 */
	DRIVE_TS1155      = 0x5105, /* TS1155 */
	DRIVE_TS1160      = 0x1106, /* TS1160 */
	DRIVE_TS1170      = 0x1107, /* TS1170 */
};

enum {
	DRIVE_GEN_UNKNOWN = 0,
	DRIVE_GEN_LTO5    = 0x2005,
	DRIVE_GEN_LTO6    = 0x2006,
	DRIVE_GEN_LTO7    = 0x2007,
	DRIVE_GEN_LTO8    = 0x2008,
	DRIVE_GEN_LTO9    = 0x2009,
	DRIVE_GEN_JAG4    = 0x1004,
	DRIVE_GEN_JAG5    = 0x1005,
	DRIVE_GEN_JAG5A   = 0x5005,
	DRIVE_GEN_JAG6    = 0x1006,
	DRIVE_GEN_JAG7    = 0x1007,
};

/* LTO cartridge type in mode page header */
enum {
	TC_MP_LTO1D_CART  = 0x18,   /* LTO1 Data cartridge */
	TC_MP_LTO2D_CART  = 0x28,   /* LTO2 Data cartridge */
	TC_MP_LTO3D_CART  = 0x38,   /* LTO3 Data cartridge */
	TC_MP_LTO4D_CART  = 0x48,   /* LTO4 Data cartridge */
	TC_MP_LTO5D_CART  = 0x58,   /* LTO5 Data cartridge */
	TC_MP_LTO6D_CART  = 0x68,   /* LTO6 Data cartridge */
	TC_MP_LTO7D_CART  = 0x78,   /* LTO7 Data cartridge */
	TC_MP_LTO8D_CART  = 0x88,   /* LTO8 Data cartridge */
	TC_MP_LTO9D_CART  = 0x98,   /* LTO9 Data cartridge */
	TC_MP_LTO3W_CART  = 0x3C,   /* LTO3 WORM cartridge */
	TC_MP_LTO4W_CART  = 0x4C,   /* LTO4 WORM cartridge */
	TC_MP_LTO5W_CART  = 0x5C,   /* LTO5 WORM cartridge */
	TC_MP_LTO6W_CART  = 0x6C,   /* LTO6 WORM cartridge */
	TC_MP_LTO7W_CART  = 0x7C,   /* LTO7 WORM cartridge */
	TC_MP_LTO8W_CART  = 0x8C,   /* LTO8 WORM cartridge */
	TC_MP_LTO9W_CART  = 0x9C,   /* LTO9 WORM cartridge */
};

/* Enterprise cartridge type in mode page header */
enum {
	/* 1st gen cart */
	TC_MP_JA          = 0x91,   /* IBM TS11x0 JA cartridge */
	TC_MP_JW          = 0xA1,   /* IBM TS11x0 JW cartridge */
	TC_MP_JJ          = 0xB1,   /* IBM TS11x0 JJ cartridge */
	TC_MP_JR          = 0xC1,   /* IBM TS11x0 JR cartridge */
	/* 2nd gen cart */
	TC_MP_JB          = 0x92,   /* IBM TS11x0 JB cartridge */
	TC_MP_JX          = 0xA2,   /* IBM TS11x0 JX cartridge */
	/* 3rd gen cart */
	TC_MP_JC          = 0x93,   /* IBM TS11x0 JC cartridge */
	TC_MP_JY          = 0xA3,   /* IBM TS11x0 JY cartridge */
	TC_MP_JK          = 0xB2,   /* IBM TS11x0 JK cartridge */
	/* 4th gen cart */
	TC_MP_JD          = 0x94,   /* IBM TS11x0 JD cartridge */
	TC_MP_JZ          = 0xA4,   /* IBM TS11x0 JZ cartridge */
	TC_MP_JL          = 0xB3,   /* IBM TS11x0 JL cartridge */
	/* 5th gen */
	TC_MP_JE          = 0x95,   /* IBM TS11x0 JE cartridge */
	TC_MP_JV          = 0xA5,   /* IBM TS11x0 JV cartridge */
	TC_MP_JM          = 0xB4,   /* IBM TS11x0 JM cartridge */
	/* 6th gen */
	TC_MP_JF          = 0x96,   /* IBM TS11x0 JF cartridge */
};

#define IS_REFORMATTABLE_TAPE(t) \
	( t == TC_MP_JB ||			 \
	  t == TC_MP_JX ||			 \
	  t == TC_MP_JK ||			 \
	  t == TC_MP_JC ||			 \
	  t == TC_MP_JY ||			 \
	  t == TC_MP_JL ||			 \
	  t == TC_MP_JD ||			 \
	  t == TC_MP_JZ ||			 \
	  t == TC_MP_JE ||			 \
	  t == TC_MP_JV ||			 \
	  t == TC_MP_JM ||			 \
	  t == TC_MP_JF ||			 \
	  t == TC_MP_LTO7D_CART )

#endif // __tape_drivers_h
