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

#ifndef __tape_drivers_h
#define __tape_drivers_h

#define KB   (1024)
#define MB   (KB * 1024)
#define GB   (MB * 1024)

#define REDPOS_LONG_LEN           (32)
#define REDPOS_EXT_LEN            (32)

#ifndef MAXSENSE
#define MAXSENSE                   (255)
#endif

#define MASK_WITH_SENSE_KEY    (0xFFFFFF)
#define MASK_WITHOUT_SENSE_KEY (0x00FFFF)

typedef void  (*crc_enc)(void *buf, size_t n);
typedef int   (*crc_check)(void *buf, size_t n);
typedef void* (*memcpy_crc_enc)(void *dest, const void *src, size_t n);
typedef int   (*memcpy_crc_check)(void *dest, const void *src, size_t n);

#define THREASHOLD_FORCE_WRITE_NO_WRITE (5)
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

struct supported_device {
	char vendor_id[VENDOR_ID_LENGTH + 1];
	char product_id[PRODUCT_ID_LENGTH + 1];
	int  drive_type;
	char product_name[PRODUCT_NAME_LENGTH + 1];
};

#define TAPEDRIVE(v, p, t, n) &(struct supported_device){ v, p, t, n }

/* Cartridge type in mode page header */
enum {
	TC_MP_LTO1D_CART  = 0x18,   /* LTO1 Data cartridge */
	TC_MP_LTO2D_CART  = 0x28,   /* LTO2 Data cartridge */
	TC_MP_LTO3D_CART  = 0x38,   /* LTO3 Data cartridge */
	TC_MP_LTO4D_CART  = 0x48,   /* LTO4 Data cartridge */
	TC_MP_LTO5D_CART  = 0x58,   /* LTO5 Data cartridge */
	TC_MP_LTO6D_CART  = 0x68,   /* LTO6 Data cartridge */
	TC_MP_LTO7D_CART  = 0x78,   /* LTO7 Data cartridge */
	TC_MP_LTO8D_CART  = 0x88,   /* LTO8 Data cartridge */
	TC_MP_LTO3W_CART  = 0x3C,   /* LTO3 WORM cartridge */
	TC_MP_LTO4W_CART  = 0x4C,   /* LTO4 WORM cartridge */
	TC_MP_LTO5W_CART  = 0x5C,   /* LTO5 WORM cartridge */
	TC_MP_LTO6W_CART  = 0x6C,   /* LTO6 WORM cartridge */
	TC_MP_LTO7W_CART  = 0x7C,   /* LTO7 WORM cartridge */
	TC_MP_LTO8W_CART  = 0x8C,   /* LTO8 WORM cartridge */
	TC_MP_JA          = 0x91,   /* IBM TS11x0 JA cartridge */
	TC_MP_JW          = 0xA1,   /* IBM TS11x0 JW cartridge */
	TC_MP_JJ          = 0xB1,   /* IBM TS11x0 JJ cartridge */
	TC_MP_JR          = 0xC1,   /* IBM TS11x0 JR cartridge */
	TC_MP_JB          = 0x92,   /* IBM TS11x0 JB cartridge */
	TC_MP_JX          = 0xA2,   /* IBM TS11x0 JX cartridge */
	TC_MP_JK          = 0xB2,   /* IBM TS11x0 JK cartridge */
	TC_MP_JC          = 0x93,   /* IBM TS11x0 JC cartridge */
	TC_MP_JY          = 0xA3,   /* IBM TS11x0 JY cartridge */
	TC_MP_JL          = 0xB3,   /* IBM TS11x0 JL cartridge */
	TC_MP_JD          = 0x94,   /* IBM TS11x0 JD cartridge */
	TC_MP_JZ          = 0xA4,   /* IBM TS11x0 JZ cartridge */
};

#define IS_REFORMATTABLE_TAPE(t) \
	( t == TC_MP_JB ||			 \
	  t == TC_MP_JC ||			 \
	  t == TC_MP_JD ||			 \
	  t == TC_MP_JK ||			 \
	  t == TC_MP_JL ||			 \
	  t == TC_MP_JY ||			 \
	  t == TC_MP_JZ ||			 \
	  t == TC_MP_LTO7D_CART )

#endif // __tape_drivers_h
