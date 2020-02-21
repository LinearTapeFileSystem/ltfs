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
** FILE NAME:       tape_drivers/ibm_tape.c
**
** DESCRIPTION:     General handling of IBM tape devices
**
** AUTHOR:          Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/

#ifndef mingw_PLATFORM
#if defined (__FreeBSD__) || defined(__NetBSD__)
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif /* __FreeBSD__ */
#include <netdb.h>
#include <ifaddrs.h>
#include <unistd.h>

#define LOOP_BACK_DEVICE "lo"
#endif

#include "tape_drivers/ibm_tape.h"
#include "libltfs/ltfs_endian.h"

DRIVE_DENSITY_SUPPORT_MAP jaguar_drive_density[] = {
	/* TS1160 */
	{ DRIVE_GEN_JAG6,  TC_MP_JE, TC_DC_JAG6,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG6,  TC_MP_JE, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG6,  TC_MP_JV, TC_DC_JAG6,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG6,  TC_MP_JV, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG6,  TC_MP_JM, TC_DC_JAG6,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG6,  TC_MP_JM, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG6,  TC_MP_JD, TC_DC_JAG5A,   MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG6,  TC_MP_JD, TC_DC_JAG5,    MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG6,  TC_MP_JD, TC_DC_UNKNOWN, MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG6,  TC_MP_JL, TC_DC_JAG5A,   MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG6,  TC_MP_JL, TC_DC_JAG5,    MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG6,  TC_MP_JL, TC_DC_UNKNOWN, MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG6,  TC_MP_JZ, TC_DC_JAG5A,   MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG6,  TC_MP_JZ, TC_DC_JAG5,    MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG6,  TC_MP_JZ, TC_DC_UNKNOWN, MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG6,  TC_MP_JC, TC_DC_JAG5,    MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG6,  TC_MP_JC, TC_DC_JAG4,    MEDIUM_READONLY },
	{ DRIVE_GEN_JAG6,  TC_MP_JC, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG6,  TC_MP_JK, TC_DC_JAG5,    MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG6,  TC_MP_JK, TC_DC_JAG4,    MEDIUM_READONLY },
	{ DRIVE_GEN_JAG6,  TC_MP_JK, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG6,  TC_MP_JY, TC_DC_JAG5,    MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG6,  TC_MP_JY, TC_DC_JAG4,    MEDIUM_READONLY },
	{ DRIVE_GEN_JAG6,  TC_MP_JY, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },

	/* TS1155 */
	{ DRIVE_GEN_JAG5A, TC_MP_JD, TC_DC_JAG5A,   MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG5A, TC_MP_JD, TC_DC_JAG5,    MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG5A, TC_MP_JD, TC_DC_UNKNOWN, MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG5A, TC_MP_JL, TC_DC_JAG5A,   MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG5A, TC_MP_JL, TC_DC_JAG5,    MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG5A, TC_MP_JL, TC_DC_UNKNOWN, MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG5A, TC_MP_JZ, TC_DC_JAG5A,   MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG5A, TC_MP_JZ, TC_DC_JAG5,    MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG5A, TC_MP_JZ, TC_DC_UNKNOWN, MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG5A, TC_MP_JC, TC_DC_JAG5,    MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG5A, TC_MP_JC, TC_DC_JAG4,    MEDIUM_READONLY },
	{ DRIVE_GEN_JAG5A, TC_MP_JC, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG5A, TC_MP_JK, TC_DC_JAG5,    MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG5A, TC_MP_JK, TC_DC_JAG4,    MEDIUM_READONLY },
	{ DRIVE_GEN_JAG5A, TC_MP_JK, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG5A, TC_MP_JY, TC_DC_JAG5,    MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG5A, TC_MP_JY, TC_DC_JAG4,    MEDIUM_READONLY },
	{ DRIVE_GEN_JAG5A, TC_MP_JY, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },

	/* TS1150 */
	{ DRIVE_GEN_JAG5,  TC_MP_JD, TC_DC_JAG5,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG5,  TC_MP_JD, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG5,  TC_MP_JL, TC_DC_JAG5,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG5,  TC_MP_JL, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG5,  TC_MP_JZ, TC_DC_JAG5,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG5,  TC_MP_JZ, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG5,  TC_MP_JC, TC_DC_JAG5,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG5,  TC_MP_JC, TC_DC_JAG4,    MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG5,  TC_MP_JC, TC_DC_UNKNOWN, MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG5,  TC_MP_JK, TC_DC_JAG5,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG5,  TC_MP_JK, TC_DC_JAG4,    MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG5,  TC_MP_JK, TC_DC_UNKNOWN, MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG5,  TC_MP_JY, TC_DC_JAG5,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG5,  TC_MP_JY, TC_DC_JAG4,    MEDIUM_WRITABLE },
	{ DRIVE_GEN_JAG5,  TC_MP_JY, TC_DC_UNKNOWN, MEDIUM_WRITABLE },

	/* TS1140 */
	{ DRIVE_GEN_JAG4,  TC_MP_JC, TC_DC_JAG4,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG4,  TC_MP_JC, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG4,  TC_MP_JK, TC_DC_JAG4,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG4,  TC_MP_JK, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG4,  TC_MP_JY, TC_DC_JAG4,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG4,  TC_MP_JY, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG4,  TC_MP_JB, TC_DC_JAG4,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG4,  TC_MP_JB, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG4,  TC_MP_JX, TC_DC_JAG4,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG4,  TC_MP_JX, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
};

DRIVE_DENSITY_SUPPORT_MAP jaguar_drive_density_strict[] = {
	/* TS1160 */
	{ DRIVE_GEN_JAG6,  TC_MP_JE, TC_DC_JAG6,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG6,  TC_MP_JE, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG6,  TC_MP_JV, TC_DC_JAG6,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG6,  TC_MP_JV, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG6,  TC_MP_JM, TC_DC_JAG6,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG6,  TC_MP_JM, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },

	/* TS1155 */
	{ DRIVE_GEN_JAG5A, TC_MP_JD, TC_DC_JAG5A,   MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG5A, TC_MP_JD, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG5A, TC_MP_JL, TC_DC_JAG5A,   MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG5A, TC_MP_JL, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG5A, TC_MP_JZ, TC_DC_JAG5A,   MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG5A, TC_MP_JZ, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },

	/* TS1150 */
	{ DRIVE_GEN_JAG5,  TC_MP_JD, TC_DC_JAG5,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG5,  TC_MP_JD, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG5,  TC_MP_JL, TC_DC_JAG5,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG5,  TC_MP_JL, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG5,  TC_MP_JZ, TC_DC_JAG5,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG5,  TC_MP_JZ, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG5,  TC_MP_JC, TC_DC_JAG5,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG5,  TC_MP_JC, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG5,  TC_MP_JK, TC_DC_JAG5,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG5,  TC_MP_JK, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG5,  TC_MP_JY, TC_DC_JAG5,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG5,  TC_MP_JY, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },

	/* TS1140 */
	{ DRIVE_GEN_JAG4,  TC_MP_JC, TC_DC_JAG4,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG4,  TC_MP_JC, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG4,  TC_MP_JK, TC_DC_JAG4,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG4,  TC_MP_JK, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG4,  TC_MP_JY, TC_DC_JAG4,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG4,  TC_MP_JY, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG4,  TC_MP_JB, TC_DC_JAG4,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG4,  TC_MP_JB, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
	{ DRIVE_GEN_JAG4,  TC_MP_JX, TC_DC_JAG4,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG4,  TC_MP_JX, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },
};

DRIVE_DENSITY_SUPPORT_MAP lto_drive_density[] = {
	/* LTO8 */
	{ DRIVE_GEN_LTO8, TC_MP_LTO8D_CART, TC_DC_LTO8,    MEDIUM_PERFECT_MATCH},
	{ DRIVE_GEN_LTO8, TC_MP_LTO7D_CART, TC_DC_LTOM8,   MEDIUM_PERFECT_MATCH},
	{ DRIVE_GEN_LTO8, TC_MP_LTO8D_CART, TC_DC_UNKNOWN, MEDIUM_PERFECT_MATCH},
	{ DRIVE_GEN_LTO8, TC_MP_LTO7D_CART, TC_DC_LTO7,    MEDIUM_WRITABLE},
	{ DRIVE_GEN_LTO8, TC_MP_LTO7D_CART, TC_DC_UNKNOWN, MEDIUM_WRITABLE},

	/* LTO7 */
	{ DRIVE_GEN_LTO7, TC_MP_LTO7D_CART, TC_DC_LTO7,    MEDIUM_PERFECT_MATCH},
	{ DRIVE_GEN_LTO7, TC_MP_LTO7D_CART, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE},
	{ DRIVE_GEN_LTO7, TC_MP_LTO6D_CART, TC_DC_LTO6,    MEDIUM_WRITABLE},
	{ DRIVE_GEN_LTO7, TC_MP_LTO6D_CART, TC_DC_UNKNOWN, MEDIUM_WRITABLE},
	{ DRIVE_GEN_LTO7, TC_MP_LTO5D_CART, TC_DC_LTO5,    MEDIUM_READONLY},
	{ DRIVE_GEN_LTO7, TC_MP_LTO5D_CART, TC_DC_UNKNOWN, MEDIUM_READONLY},

	/* LTO6 */
	{ DRIVE_GEN_LTO6, TC_MP_LTO6D_CART, TC_DC_LTO6,    MEDIUM_PERFECT_MATCH},
	{ DRIVE_GEN_LTO6, TC_MP_LTO6D_CART, TC_DC_UNKNOWN, MEDIUM_PERFECT_MATCH},
	{ DRIVE_GEN_LTO6, TC_MP_LTO5D_CART, TC_DC_LTO5,    MEDIUM_WRITABLE},
	{ DRIVE_GEN_LTO6, TC_MP_LTO5D_CART, TC_DC_UNKNOWN, MEDIUM_WRITABLE},

	/* LTO5 */
	{ DRIVE_GEN_LTO5, TC_MP_LTO5D_CART, TC_DC_LTO5,    MEDIUM_PERFECT_MATCH},
	{ DRIVE_GEN_LTO5, TC_MP_LTO5D_CART, TC_DC_UNKNOWN, MEDIUM_PERFECT_MATCH},
};

DRIVE_DENSITY_SUPPORT_MAP lto_drive_density_strict[] = {
	/* LTO8 */
	{ DRIVE_GEN_LTO8, TC_MP_LTO8D_CART, TC_DC_LTO8,    MEDIUM_PERFECT_MATCH},
	{ DRIVE_GEN_LTO8, TC_MP_LTO8D_CART, TC_DC_LTOM8,   MEDIUM_PERFECT_MATCH},
	{ DRIVE_GEN_LTO8, TC_MP_LTO8D_CART, TC_DC_UNKNOWN, MEDIUM_PERFECT_MATCH},

	/* LTO7 */
	{ DRIVE_GEN_LTO7, TC_MP_LTO7D_CART, TC_DC_LTO7,    MEDIUM_PERFECT_MATCH},
	{ DRIVE_GEN_LTO7, TC_MP_LTO7D_CART, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE},

	/* LTO6 */
	{ DRIVE_GEN_LTO6, TC_MP_LTO6D_CART, TC_DC_LTO6,    MEDIUM_PERFECT_MATCH},
	{ DRIVE_GEN_LTO6, TC_MP_LTO6D_CART, TC_DC_UNKNOWN, MEDIUM_PERFECT_MATCH},

	/* LTO5 */
	{ DRIVE_GEN_LTO5, TC_MP_LTO5D_CART, TC_DC_LTO5,    MEDIUM_PERFECT_MATCH},
	{ DRIVE_GEN_LTO5, TC_MP_LTO5D_CART, TC_DC_UNKNOWN, MEDIUM_PERFECT_MATCH},
};

const unsigned char supported_cart[] = {
	TC_MP_LTO8D_CART,
	TC_MP_LTO7D_CART,
	TC_MP_LTO6D_CART,
	TC_MP_LTO5D_CART,
	TC_MP_JB,
	TC_MP_JC,
	TC_MP_JD,
	TC_MP_JK,
	TC_MP_JY,
	TC_MP_JL,
	TC_MP_JZ,
	TC_MP_JE,
	TC_MP_JV,
	TC_MP_JM,
};

const unsigned char supported_density[] = {
	TC_DC_JAG6E,
	TC_DC_JAG5AE,
	TC_DC_JAG5E,
	TC_DC_JAG4E,
	TC_DC_JAG6,
	TC_DC_JAG5A,
	TC_DC_JAG5,
	TC_DC_JAG4,
	TC_DC_LTO8,
	TC_DC_LTOM8,
	TC_DC_LTO7,
	TC_DC_LTO6,
	TC_DC_LTO5,
};

int num_jaguar_drive_density        = sizeof(jaguar_drive_density) / sizeof(DRIVE_DENSITY_SUPPORT_MAP);
int num_jaguar_drive_density_strict = sizeof(jaguar_drive_density_strict) / sizeof(DRIVE_DENSITY_SUPPORT_MAP);
int num_lto_drive_density           = sizeof(lto_drive_density) / sizeof(DRIVE_DENSITY_SUPPORT_MAP);
int num_lto_drive_density_strict    = sizeof(lto_drive_density_strict) / sizeof(DRIVE_DENSITY_SUPPORT_MAP);
int num_supported_cart              = sizeof(supported_cart)/sizeof(supported_cart[0]);
int num_supported_density           = sizeof(supported_density)/sizeof(supported_density[0]);

struct supported_device *ibm_supported_drives[] = {
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-TD5",  DRIVE_LTO5,    "[ULTRIUM-TD5]" ),  /* IBM Ultrium Gen 5 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-TD5",  DRIVE_LTO5,    "[ULT3580-TD5]" ),  /* IBM Ultrium Gen 5 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-HH5",  DRIVE_LTO5_HH, "[ULTRIUM-HH5]" ),  /* IBM Ultrium Gen 5 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-HH5",  DRIVE_LTO5_HH, "[ULT3580-HH5]" ),  /* IBM Ultrium Gen 5 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "HH LTO Gen 5", DRIVE_LTO5_HH, "[HH LTO Gen 5]" ), /* IBM Ultrium Gen 5 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-TD6",  DRIVE_LTO6,    "[ULTRIUM-TD6]" ),  /* IBM Ultrium Gen 6 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-TD6",  DRIVE_LTO6,    "[ULT3580-TD6]" ),  /* IBM Ultrium Gen 6 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-HH6",  DRIVE_LTO6_HH, "[ULTRIUM-HH6]" ),  /* IBM Ultrium Gen 6 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-HH6",  DRIVE_LTO6_HH, "[ULT3580-HH6]" ),  /* IBM Ultrium Gen 6 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "HH LTO Gen 6", DRIVE_LTO6_HH, "[HH LTO Gen 6]" ), /* IBM Ultrium Gen 6 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-TD7",  DRIVE_LTO7,    "[ULTRIUM-TD7]" ),  /* IBM Ultrium Gen 7 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-TD7",  DRIVE_LTO7,    "[ULT3580-TD7]" ),  /* IBM Ultrium Gen 7 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-HH7",  DRIVE_LTO7_HH, "[ULTRIUM-HH7]" ),  /* IBM Ultrium Gen 7 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-HH7",  DRIVE_LTO7_HH, "[ULT3580-HH7]" ),  /* IBM Ultrium Gen 7 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "HH LTO Gen 7", DRIVE_LTO7_HH, "[HH LTO Gen 7]" ), /* IBM Ultrium Gen 7 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-TD8",  DRIVE_LTO8,    "[ULTRIUM-TD8]" ),  /* IBM Ultrium Gen 8 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-TD8",  DRIVE_LTO8,    "[ULT3580-TD8]" ),  /* IBM Ultrium Gen 8 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-HH8",  DRIVE_LTO8_HH, "[ULTRIUM-HH8]" ),  /* IBM Ultrium Gen 8 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-HH8",  DRIVE_LTO8_HH, "[ULT3580-HH8]" ),  /* IBM Ultrium Gen 8 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "HH LTO Gen 8", DRIVE_LTO8_HH, "[HH LTO Gen 8]" ), /* IBM Ultrium Gen 8 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "03592E07",     DRIVE_TS1140,  "[03592E07]" ),     /* IBM TS1140 */
		TAPEDRIVE( IBM_VENDOR_ID, "03592E08",     DRIVE_TS1150,  "[03592E08]" ),     /* IBM TS1150 */
		TAPEDRIVE( IBM_VENDOR_ID, "0359255F",     DRIVE_TS1155,  "[0359255F]" ),     /* IBM TS1155 */
		TAPEDRIVE( IBM_VENDOR_ID, "0359255E",     DRIVE_TS1155,  "[0359255E]" ),     /* IBM TS1155 */
		TAPEDRIVE( IBM_VENDOR_ID, "0359260F",     DRIVE_TS1160,  "[0359260F]" ),     /* IBM TS1160 */
		TAPEDRIVE( IBM_VENDOR_ID, "0359260E",     DRIVE_TS1160,  "[0359260E]" ),     /* IBM TS1160 */
		/* End of supported_devices */
		NULL
};

struct supported_device *usb_supported_drives[] = {
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-TD5",  DRIVE_LTO5,    "[ULT3580-TD5]" ), /* IBM Ultrium Gen 5 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-HH5",  DRIVE_LTO5_HH, "[ULTRIUM-HH5]" ), /* IBM Ultrium Gen 5 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-HH5",  DRIVE_LTO5_HH, "[ULT3580-HH5]" ), /* IBM Ultrium Gen 5 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-TD6",  DRIVE_LTO6,    "[ULT3580-TD6]" ), /* IBM Ultrium Gen 6 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-HH6",  DRIVE_LTO6_HH, "[ULTRIUM-HH6]" ), /* IBM Ultrium Gen 6 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-HH6",  DRIVE_LTO6_HH, "[ULT3580-HH6]" ), /* IBM Ultrium Gen 6 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-TD7",  DRIVE_LTO7,    "[ULT3580-TD7]" ), /* IBM Ultrium Gen 7 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-HH7",  DRIVE_LTO7_HH, "[ULTRIUM-HH7]" ), /* IBM Ultrium Gen 7 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-HH7",  DRIVE_LTO7_HH, "[ULT3580-HH7]" ), /* IBM Ultrium Gen 7 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-TD8",  DRIVE_LTO8,    "[ULT3580-TD8]" ), /* IBM Ultrium Gen 8 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-HH8",  DRIVE_LTO8_HH, "[ULTRIUM-HH8]" ), /* IBM Ultrium Gen 8 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-HH8",  DRIVE_LTO8_HH, "[ULT3580-HH8]" ), /* IBM Ultrium Gen 8 Half-High */
		/* End of supported_devices */
		NULL
};

/* Standard SCSI sense table */
struct error_table standard_tape_errors[] = {
	/* Sense Key 0 (No Sense) */
	{0x000000, -EDEV_NO_SENSE,                  "No Additional Sense Information"},
	{0x000001, -EDEV_FILEMARK_DETECTED,         "Filemark Detected"},
	{0x000002, -EDEV_EARLY_WARNING,             "End-of-Partition/Medium Detected (Early Warning)"},
	{0x000004, -EDEV_BOP_DETECTED,              "Beginning-of-Partition/Medium Detected"},
	{0x000007, -EDEV_PROG_EARLY_WARNING,        "End-of-Partition/Medium Detected (Programable Early Warning)"},
	{0x000016, -EDEV_OPERATION_IN_PROGRESS,     "Operation in Progress"},
	{0x000017, -EDEV_CLEANING_REQUIRED,         "Cleaning Required"},
	{0x000018, -EDEV_OPERATION_IN_PROGRESS,     "Erase Operation in Progress"},
	{0x001401, -EDEV_RECORD_NOT_FOUND,          "Record Not Found (String Search)"},
	{0x002E00, -EDEV_INSUFFICIENT_TIME,         "Insufficient Time For Operation (String Search)"},
	{0x003003, -EDEV_CLEANING_CART,             "Cleaning tape installed"},
	/* Sense Key 1 (Recovered Error) */
	{0x010000, -EDEV_RECOVERED_ERROR,           "No Additional Sense Information"},
	{0x010017, -EDEV_CLEANING_REQUIRED,         "Drive Needs Cleaning"},
	{0x010A00, -EDEV_RECOVERED_ERROR,           "Error log overflow"},
	{0x010C00, -EDEV_RECOVERED_ERROR,           "Write Error: A write error occurred, but was recovered."},
	{0x011100, -EDEV_RECOVERED_ERROR,           "Read Error: A read error occurred, but was recovered."},
	{0x011701, -EDEV_RECOVERED_ERROR,           "Recovered Data with Retries"},
	{0x011800, -EDEV_RECOVERED_ERROR,           "Recovered Data with Error Correction Applied"},
	{0x013700, -EDEV_MODE_PARAMETER_ROUNDED,    "Mode Parameters Rounded"},
	{0x014700, -EDEV_RECOVERED_ERROR,           "SCSI parity error"},
	{0x015B02, -EDEV_RECOVERED_ERROR,           "Log counter at maximum"},
	{0x015D00, -EDEV_RECOVERED_ERROR,           "Failure Prediction Threshold Exceeded"},
	{0x015DFF, -EDEV_RECOVERED_ERROR,           "Failure Prediction Threshold Exceeded (FALSE)"},
	{0x01EF13, -EDEV_RECOVERED_ERROR,           "Encryption - Key Translate"},
	/* Sense Key 2 (Not Ready) */
	{0x020017, -EDEV_CLEANING_IN_PROGRESS,      "Drive cleaning requested"},
	{0x020400, -EDEV_NOT_REPORTABLE,            "Logical Unit Not Ready, Cause Not Reportable"},
	{0x020401, -EDEV_BECOMING_READY,            "Logical Unit Is in Process of Becoming Ready"},
	{0x020402, -EDEV_NEED_INITIALIZE,           "Initializing Command Required"},
	{0x020403, -EDEV_NO_MEDIUM,                 "Logical Unit Not Ready, Manual Intervention Required"},
	{0x020404, -EDEV_OPERATION_IN_PROGRESS,     "Logical Unit Not Ready, Format in Progress"},
	{0x020407, -EDEV_OPERATION_IN_PROGRESS,     "Operation in progress"},
	{0x020412, -EDEV_OFFLINE,                   "Logical Unit Not Ready, Offline"},
	{0x020413, -EDEV_OPERATION_IN_PROGRESS,     "Logical Unit Not Ready, SA Creation in Progress"},
	{0x020B01, -EDEV_OVER_TEMPERATURE,          "Warning - Specified Temperature Exceeded"},
	{0x023003, -EDEV_CLEANING_IN_PROGRESS,      "Cleaning Cartridge Installed"},
	{0x023007, -EDEV_NOT_READY,                 "Cleaning Failure"},
	{0x023A00, -EDEV_NO_MEDIUM,                 "Medium Not Present"},
	{0x023A02, -EDEV_IE_OPEN,                   "Medium Not Present - Tray Open"},
	{0x023A04, -EDEV_NO_MEDIUM,                 "Not Ready - Medium Auxiliary Memory Accessible"},
	{0x023B12, -EDEV_DOOR_OPEN,                 "Magazine removed"},
	{0x023E00, -EDEV_NOT_SELF_CONFIGURED_YET,   "Logical Unit Has Not Self-configured"},
	{0x025300, -EDEV_LOAD_UNLOAD_ERROR,         "Media Load or Eject Failed"},
	{0x027411, -EDEV_PARAMETER_VALUE_REJECTED,  "SA Creation Parameter Value Rejected"},
	/* Sense Key 3 (Medium Error) */
	{0x030302, -EDEV_WRITE_PERM,                "Excessive Write Errors"},
	{0x030410, -EDEV_CM_PERM,                   "Logical Unit Not Ready, Auxiliary Memory Not Accessible"},
	{0x030900, -EDEV_RW_PERM,                   "Track Following Error (Servo)"},
	{0x030C00, -EDEV_WRITE_PERM,                "Write Error"},
	{0x031100, -EDEV_READ_PERM,                 "Unrecovered Read Error"},
	{0x031101, -EDEV_READ_PERM,                 "Read Retries Exhausted"},
	{0x031108, -EDEV_READ_PERM,                 "Incomplete Block Read"},
	{0x031112, -EDEV_CM_PERM,                   "Auxiliary Memory Read Error"},
	{0x031400, -EDEV_RW_PERM,                   "Recorded Entity Not Found"},
	{0x031401, -EDEV_RW_PERM,                   "Record Not Found"},
	{0x031402, -EDEV_RW_PERM,                   "Filemark or Setmark Not Found"},
	{0x031403, -EDEV_RW_PERM,                   "End-of-Data Not Found"},
	{0x031404, -EDEV_MEDIUM_ERROR,              "Block Sequence Error"},
	{0x033000, -EDEV_MEDIUM_FORMAT_ERROR,       "Incompatible Medium Installed"},
	{0x033001, -EDEV_MEDIUM_FORMAT_ERROR,       "Cannot Read Medium, Unknown Format"},
	{0x033002, -EDEV_MEDIUM_FORMAT_ERROR,       "Cannot Read Medium, Incompatible Format"},
	{0x033003, -EDEV_MEDIUM_FORMAT_ERROR,       "Cleaning tape installed"},
	{0x033007, -EDEV_CLEANING_FALIURE,          "Cleaning failure"},
	{0x03300D, -EDEV_MEDIUM_ERROR,              "Medium Error/WORM Medium"},
	{0x033100, -EDEV_MEDIUM_FORMAT_CORRUPTED,   "Medium Format Corrupted"},
	{0x033101, -EDEV_MEDIUM_ERROR,              "Format Command Failed"},
	{0x033300, -EDEV_MEDIUM_ERROR,              "Tape Length Error"},
	{0x033B00, -EDEV_RW_PERM,                   "Sequential Positioning Error"},
	{0x035000, -EDEV_RW_PERM,                   "Write Append Error"},
	{0x035100, -EDEV_MEDIUM_ERROR,              "Erase Failure"},
	{0x035200, -EDEV_RW_PERM,                   "Cartridge Fault"},
	{0x035300, -EDEV_LOAD_UNLOAD_ERROR,         "Media Load or Eject Failed"},
	{0x035304, -EDEV_LOAD_UNLOAD_ERROR,         "Medium Thread or Unthread Failure"},
	/* Sense Key 4 (Hardware or Firmware Error) */
	{0x040403, -EDEV_HARDWARE_ERROR,            "Manual Intervention Required"},
	{0x040801, -EDEV_HARDWARE_ERROR,            "Logical Unit Communication Failure"},
	{0x040900, -EDEV_HARDWARE_ERROR,            "Track Following Error"},
	{0x041001, -EDEV_LBP_WRITE_ERROR,           "Logical Block Guard Check Failed"},
	{0x041004, -EDEV_HARDWARE_ERROR,            "Logical Block Protection Error On Recover Buffered Data"},
	{0x041501, -EDEV_HARDWARE_ERROR,            "Machanical Position Error"},
	{0x043B00, -EDEV_HARDWARE_ERROR,            "Sequential Positioning Error"},
	{0x043B08, -EDEV_HARDWARE_ERROR,            "Reposition Error"},
	{0x043B0D, -EDEV_HARDWARE_ERROR,            "Medium Destination Element Full"},
	{0x043B0E, -EDEV_HARDWARE_ERROR,            "Medium Source Element Empty"},
	{0x043F0F, -EDEV_HARDWARE_ERROR,            "Echo buffer overwritten"},
	{0x044000, -EDEV_HARDWARE_ERROR,            "Diagnostic Failure"},
	{0x044100, -EDEV_HARDWARE_ERROR,            "Data Path Failure"},
	{0x044400, -EDEV_HARDWARE_ERROR,            "Internal Target Failure"},
	{0x044C00, -EDEV_HARDWARE_ERROR,            "Logical Unit Failed Self-Configuration"},
	{0x045100, -EDEV_HARDWARE_ERROR,            "Erase Failure"},
	{0x045200, -EDEV_HARDWARE_ERROR,            "Cartridge Fault"},
	{0x045300, -EDEV_HARDWARE_ERROR,            "Media Load or Eject Failed"},
	{0x045301, -EDEV_HARDWARE_ERROR,            "A drive did not unload a cartridge."},
	{0x045304, -EDEV_HARDWARE_ERROR,            "Medium Thread or Unthread Failure"},
	/* Sense Key 5 (Illegal Request) */
	{0x050E03, -EDEV_ILLEGAL_REQUEST,           "Invalid Field in Command Information Unit (e.g., FCP_DL error)"},
	{0x051A00, -EDEV_ILLEGAL_REQUEST,           "Parameter List Length Error"},
	{0x052000, -EDEV_ILLEGAL_REQUEST,           "Invalid Command Operation Code"},
	{0x05200C, -EDEV_ILLEGAL_REQUEST,           "Illegal Command When Not In Append-Only Mode"},
	{0x052101, -EDEV_INVALID_ADDRESS,           "Invalid Element Address"},
	{0x052400, -EDEV_INVALID_FIELD_CDB,         "Invalid Field in CDB"},
	{0x052500, -EDEV_ILLEGAL_REQUEST,           "Logical Unit Not Supported"},
	{0x052600, -EDEV_ILLEGAL_REQUEST,           "Invalid Field in Parameter List"},
	{0x052601, -EDEV_ILLEGAL_REQUEST,           "Parameter list error: parameter not supported"},
	{0x052602, -EDEV_ILLEGAL_REQUEST,           "Parameter value invalid"},
	{0x052603, -EDEV_ILLEGAL_REQUEST,           "Threshold Parameters Not Supported"},
	{0x052604, -EDEV_ILLEGAL_REQUEST,           "Invalid release of persistent reservation"},
	{0x052611, -EDEV_ILLEGAL_REQUEST,           "Encryption - Incomplete Key-Associate Data Set"},
	{0x052612, -EDEV_ILLEGAL_REQUEST,           "Vendor Specific Key Reference Not Found"},
	{0x052690, -EDEV_ILLEGAL_REQUEST,           "Wrong firmware image, does not fit boot code"},
	{0x052691, -EDEV_ILLEGAL_REQUEST,           "Wrong personality firmware image"},
	{0x052693, -EDEV_ILLEGAL_REQUEST,           "Wrong firmware image, checksum error"},
	{0x052904, -EDEV_ILLEGAL_REQUEST,           "Device Internal Reset"},
	{0x052C00, -EDEV_ILLEGAL_REQUEST,           "Command Sequence Error"},
	{0x052C0B, -EDEV_ILLEGAL_REQUEST,           "Not Reserved"},
	{0x053000, -EDEV_ILLEGAL_REQUEST,           "Incompatible Medium Installed"},
	{0x053005, -EDEV_ILLEGAL_REQUEST,           "Cannot Write Medium - Incompatible Format"},
	{0x053900, -EDEV_ILLEGAL_REQUEST,           "Saving Parameters Not Supported"},
	{0x053B00, -EDEV_ILLEGAL_REQUEST,           "Sequential Positioning Error"},
	{0x053B0C, -EDEV_ILLEGAL_REQUEST,           "Position Past Beginning of Medium"},
	{0x053B0D, -EDEV_DEST_FULL,                 "Medium Destination Element Full"},
	{0x053B0E, -EDEV_SRC_EMPTY,                 "Medium Source Element Empty"},
	{0x053B11, -EDEV_MAGAZINE_INACCESSIBLE,     "Medium magazine not accessible"},
	{0x053B12, -EDEV_MAGAZINE_INACCESSIBLE,     "Media magazine not installed."},
	{0x053D00, -EDEV_ILLEGAL_REQUEST,           "Invalid Bits in Identify Message"},
	{0x054900, -EDEV_ILLEGAL_REQUEST,           "Invalid Message Error"},
	{0x055301, -EDEV_MEDIUM_LOCKED,            "A drive did not unload a cartridge."},
	{0x055302, -EDEV_MEDIUM_LOCKED,             "Medium Removal Prevented"},
	{0x055303, -EDEV_MEDIUM_LOCKED,             "Drive media removal prevented state set"},
	{0x055508, -EDEV_ILLEGAL_REQUEST,           "Maximum Number of Supplemental Decryption Keys Exceeded"},
	{0x055B03, -EDEV_ILLEGAL_REQUEST,           "Log List Codes Exhausted"},
	{0x057408, -EDEV_ILLEGAL_REQUEST,           "Digital Signature Validation Failure"},
	{0x05740C, -EDEV_ILLEGAL_REQUEST,           "Unable to Decrypt Parameter List"},
	{0x057410, -EDEV_ILLEGAL_REQUEST,           "SA Creation Parameter Value Invalid"},
	{0x057411, -EDEV_ILLEGAL_REQUEST,           "SA Creation Parameter Value Rejected"},
	{0x057412, -EDEV_ILLEGAL_REQUEST,           "Invalid SA Usage"},
	{0x057430, -EDEV_ILLEGAL_REQUEST,           "SA Creation Parameter not Supported"},
	/* Sense Key 6 (Unit Attention) */
	{0x060002, -EDEV_EARLY_WARNING,             "End-of-Partition/Medium Detected, Early Warning"},
	{0x062800, -EDEV_MEDIUM_MAY_BE_CHANGED,     "Not Ready to Ready Transition, Medium May Have Changed"},
	{0x062801, -EDEV_IE_ACCESSED,               "Import or Export Element Accessed"},
	{0x062900, -EDEV_POR_OR_BUS_RESET,          "Power On, Reset, or Bus Device Reset Occurred"},
	{0x062901, -EDEV_POR_OR_BUS_RESET,          "Power on occurred"},
	{0x062902, -EDEV_POR_OR_BUS_RESET,          "SCSI Bus reset occurred"},
	{0x062903, -EDEV_POR_OR_BUS_RESET,          "Internal reset occurred"},
	{0x062904, -EDEV_POR_OR_BUS_RESET,          "Internal reset occurred"},
	{0x062905, -EDEV_UNIT_ATTENTION,            "Transceiver Mode Changed To Single-ended"},
	{0x062906, -EDEV_UNIT_ATTENTION,            "Transceiver Mode Changed To LVD"},
	{0x062A01, -EDEV_CONFIGURE_CHANGED,         "Mode Parameters Changed"},
	{0x062A02, -EDEV_CONFIGURE_CHANGED,         "Mode Parameters Changed"},
	{0x062A03, -EDEV_RESERVATION_PREEMPTED,     "Reservations preempted"},
	{0x062A04, -EDEV_RESERVATION_RELEASED,      "Reservations released"},
	{0x062A05, -EDEV_REGISTRATION_PREEMPTED,    "Registrations preempted"},
	{0x062A10, -EDEV_TIME_STAMP_CHANGED,        "Time stamp changed"},
	{0x062A11, -EDEV_CRYPTO_ERROR,              "Encryption - Data Encryption Parameters Changed by Another I_T Nexus"},
	{0x062A12, -EDEV_CRYPTO_ERROR,              "Encryption - Data Encryption Parameters Changed by Vendor Specific Event"},
	{0x062A14, -EDEV_UNIT_ATTENTION,            "SA Creation Capabilities Data Has Changed"},
	{0x062F00, -EDEV_COMMAND_CLEARED,           "Commands Cleared by Another Initiator"},
	{0x063000, -EDEV_MEDIUM_ERROR,              "Incompatible Medium Installed"},
	{0x063B12, -EDEV_DOOR_CLOSED,               "Medium magazine removed"},
	{0x063B13, -EDEV_DOOR_CLOSED,               "Medium magazine inserted"},
	{0x063F01, -EDEV_CONFIGURE_CHANGED,         "Microcode Has Been Changed"},
	{0x063F02, -EDEV_CONFIGURE_CHANGED,         "Changed Operating Definition"},
	{0x063F03, -EDEV_CONFIGURE_CHANGED,         "Inquiry Data Has Changed"},
	{0x063F05, -EDEV_CONFIGURE_CHANGED,         "Device Identifier Changed"},
	{0x063F0E, -EDEV_CONFIGURE_CHANGED,         "Reported LUNs Data Has Changed"},
	{0x065302, -EDEV_MEDIA_REMOVAL_PREV,        "Media removal prevented"},
	{0x065A01, -EDEV_MEDIUM_REMOVAL_REQ,        "Operator Medium Removal Request"},
	/* Sense Key 7 (Data Protect) */
	{0x072610, -EDEV_CRYPTO_ERROR,              "Encryption - Data Decryption Key Fail Limit"},
	{0x072700, -EDEV_WRITE_PROTECTED,           "Write Protected"},
	{0x072A13, -EDEV_CRYPTO_ERROR,              "Encryption - Data Encryption Key Instance Counter Has Changed"},
	{0x073005, -EDEV_DATA_PROTECT,              "Cannot Write Medium, Incompatible Format"},
	{0x073000, -EDEV_WRITE_PROTECTED_WORM,      "Data Protect/WORM Medium"},
	{0x07300C, -EDEV_WRITE_PROTECTED_WORM,      "Data Protect/WORM Medium - Overwrite Attempted"},
	{0x07300D, -EDEV_WRITE_PROTECTED_WORM,      "Data Protect/WORM Medium - Integrity Check"},
	{0x075001, -EDEV_WRITE_PROTECTED_WORM,      "Write Append Position Error (WORM)"},
	{0x075200, -EDEV_DATA_PROTECT,              "Cartridge Fault"},
	{0x075A02, -EDEV_WRITE_PROTECTED_OPERATOR,  "Data Protect/Operator - Overwrite Attempted"},
	{0x077400, -EDEV_WRITE_PROTECTED_WORM,      "Security Error"},
	{0x077401, -EDEV_CRYPTO_ERROR,              "Encryption - Unable to Decrypt Data"},
	{0x077402, -EDEV_CRYPTO_ERROR,              "Encryption - Unencrypted Data Encountered While Decrypting"},
	{0x077403, -EDEV_CRYPTO_ERROR,              "Encryption - Incorrect Data Encryption Key"},
	{0x077404, -EDEV_CRYPTO_ERROR,              "Encryption - Cryptographic Integrity Validation Failed"},
	{0x077405, -EDEV_CRYPTO_ERROR,              "Encryption - Error Decrypting Data"},
	/* Sense Key 8 (Blank Check) */
	{0x080005, -EDEV_EOD_DETECTED,              "End-of-Data (EOD) Detected"},
	{0x081401, -EDEV_RECORD_NOT_FOUND,          "Record Not Found, Void Tape"},
	{0x081403, -EDEV_EOD_NOT_FOUND,             "End-of-Data (EOD) not found"},
	{0x080B01, -EDEV_OVER_TEMPERATURE,          "The drive detected an overtemperature condition."},
	/* Sense Key B (Aborted Command) */
	{0x0B0E01, -EDEV_ABORTED_COMMAND,           "Information Unit Too Short"},
	{0x0B1400, -EDEV_ABORTED_COMMAND,           "Recorded Entity Not Found"},
	{0x0B1401, -EDEV_ABORTED_COMMAND,           "Record Not Found"},
	{0x0B1402, -EDEV_ABORTED_COMMAND,           "Filemark or Setmark Not Found"},
	{0x0B1B00, -EDEV_ABORTED_COMMAND,           "Synchronous Data Transfer Error"},
	{0x0B3D00, -EDEV_ABORTED_COMMAND,           "Invalid Bits in Identify Message"},
	{0x0B3F0F, -EDEV_ABORTED_COMMAND,           "Echo Buffer Overwritten"},
	{0x0B4100, -EDEV_ABORTED_COMMAND,           "LDI command Failure"},
	{0x0B4300, -EDEV_ABORTED_COMMAND,           "Message Error"},
	{0x0B4400, -EDEV_ABORTED_COMMAND,           "Internal Target Failure"},
	{0x0B4500, -EDEV_ABORTED_COMMAND,           "Select/Reselect Failure"},
	{0x0B4700, -EDEV_ABORTED_COMMAND,           "SCSI Parity Error"},
	{0x0B4703, -EDEV_ABORTED_COMMAND,           "Information Unit iuCRC Error Detected"},
	{0x0B4800, -EDEV_ABORTED_COMMAND,           "Initiator Detected Error Message Received"},
	{0x0B4900, -EDEV_ABORTED_COMMAND,           "Invalid Message Error"},
	{0x0B4A00, -EDEV_ABORTED_COMMAND,           "Command Phase Error"},
	{0x0B4B00, -EDEV_ABORTED_COMMAND,           "Data Phase Error"},
	{0x0B4B02, -EDEV_ABORTED_COMMAND,           "Too Much Write Data"},
	{0x0B4B03, -EDEV_ABORTED_COMMAND,           "ACK/NAK Timeout"},
	{0x0B4B04, -EDEV_ABORTED_COMMAND,           "NAK Received"},
	{0x0B4B05, -EDEV_ABORTED_COMMAND,           "Data Offset Error"},
	{0x0B4B06, -EDEV_TIMEOUT,                   "Initiator Response Timeout"},
	{0x0B4E00, -EDEV_OVERLAPPED,                "Overlapped Commands"},
	{0x0B0801, -EDEV_ABORTED_COMMAND,           "LU Communication - Timeout"},

	/* Sense Key D (Volume Overflow) */
	{0x0D0002, -EDEV_OVERFLOW,                  "End-of-Partition/Medium Detected"},
	/* END MARK*/
	{0xFFFFFF, -EDEV_UNKNOWN,                   "Unknown Error code"},
};

/* IBM LTO tape drive vendor unique sense table */
struct error_table ibm_tape_errors[] = {
	/* Sense Key 0 (No Sense) */
	{0x008282, -EDEV_CLEANING_REQUIRED,         "IBM LTO - Cleaning Required"},
	/* Sense Key 1 (Recoverd Error) */
	{0x018252, -EDEV_DEGRADED_MEDIA,            "IBM LTO - Degraded Media"},
	{0x018383, -EDEV_RECOVERED_ERROR,           "Drive Has Been Cleaned"},
	{0x018500, -EDEV_RECOVERED_ERROR,           "Search Match List Limit (warning)"},
	{0x018501, -EDEV_RECOVERED_ERROR,           "Search Snoop Match Found"},
	/* Sense Key 3 (Medium Error) */
	{0x038500, -EDEV_DATA_PROTECT,              "Write Protected Because of Tape or Drive Failure"},
	{0x038501, -EDEV_DATA_PROTECT,              "Write Protected Because of Tape Failure"},
	{0x038502, -EDEV_DATA_PROTECT,              "Write Protected Because of Drive Failure"},
	/* Sense Key 5 (Illegal Request) */
	{0x058000, -EDEV_ILLEGAL_REQUEST,           "CU Mode, Vendor-Unique"},
	{0x058283, -EDEV_ILLEGAL_REQUEST,           "Bad Microcode Detected"},
	{0x058503, -EDEV_ILLEGAL_REQUEST,           "Write Protected Because of Current Tape Position"},
	{0x05A301, -EDEV_ILLEGAL_REQUEST,           "OEM Vendor-Specific"},
	/* Sense Key 6 (Unit Attention) */
	{0x065DFF, -EDEV_UNIT_ATTENTION,            "Failure Prediction False"},
	{0x068283, -EDEV_UNIT_ATTENTION,            "Drive Has Been Cleaned (older versions of microcode)"},
	{0x068500, -EDEV_UNIT_ATTENTION,            "Search Match List Limit (alert)"},
	/* Crypto Related Sense Code */
	{0x00EF13, -EDEV_CRYPTO_ERROR,              "Encryption - Key Translate"},
	{0x03EE60, -EDEV_CRYPTO_ERROR,              "Encryption - Proxy Command Error"},
	{0x03EED0, -EDEV_CRYPTO_ERROR,              "Encryption - Data Read Decryption Failure"},
	{0x03EED1, -EDEV_CRYPTO_ERROR,              "Encryption - Data Read after Write Decryption Failure"},
	{0x03EEE0, -EDEV_CRYPTO_ERROR,              "Encryption - Key Translation Failure"},
	{0x03EEE1, -EDEV_CRYPTO_ERROR,              "Encryption - Key Translation Ambiguous"},
	{0x03EEF0, -EDEV_CRYPTO_ERROR,              "Encryption - Decryption Fenced (Read)"},
	{0x03EEF1, -EDEV_CRYPTO_ERROR,              "Encryption - Encryption Fenced (Write)"},
	{0x044780, -EDEV_HARDWARE_ERROR,            "IBM LTO - Read Internal CRC Error"},
	{0x044781, -EDEV_HARDWARE_ERROR,            "IBM LTO - Write Internal CRC Error"},
	{0x04EE0E, -EDEV_KEY_SERVICE_ERROR,         "Encryption - Key Service Timeout"}, /* LTO5, Jag4 and earlier */
	{0x04EE0F, -EDEV_KEY_SERVICE_ERROR,         "Encryption - Key Service Failure"}, /* LTO5, Jag4 and earlier */
	{0x05EE00, -EDEV_CRYPTO_ERROR,              "Encryption - Key Service Not Enabled"},
	{0x05EE01, -EDEV_CRYPTO_ERROR,              "Encryption - Key Service Not Configured"},
	{0x05EE02, -EDEV_CRYPTO_ERROR,              "Encryption - Key Service Not Available"},
	{0x05EE0D, -EDEV_CRYPTO_ERROR,              "Encryption - Message Content Error"},
	{0x05EE10, -EDEV_CRYPTO_ERROR,              "Encryption - Key Required"},
	{0x05EE20, -EDEV_CRYPTO_ERROR,              "Encryption - Key Count Exceeded"},
	{0x05EE21, -EDEV_CRYPTO_ERROR,              "Encryption - Key Alias Exceeded"},
	{0x05EE22, -EDEV_CRYPTO_ERROR,              "Encryption - Key Reserved"},
	{0x05EE23, -EDEV_CRYPTO_ERROR,              "Encryption - Key Conflict"},
	{0x05EE24, -EDEV_CRYPTO_ERROR,              "Encryption - Key Method Change"},
	{0x05EE25, -EDEV_CRYPTO_ERROR,              "Encryption - Key Format Not Supported"},
	{0x05EE26, -EDEV_CRYPTO_ERROR,              "Encryption - Unauthorized Request - dAK"},
	{0x05EE27, -EDEV_CRYPTO_ERROR,              "Encryption - Unauthorized Request - dSK"},
	{0x05EE28, -EDEV_CRYPTO_ERROR,              "Encryption - Unauthorized Request - eAK"},
	{0x05EE29, -EDEV_CRYPTO_ERROR,              "Encryption - Authentication Failure"},
	{0x05EE2A, -EDEV_CRYPTO_ERROR,              "Encryption - Invalid RDKi"},
	{0x05EE2B, -EDEV_CRYPTO_ERROR,              "Encryption - Key Incorrect"},
	{0x05EE2C, -EDEV_CRYPTO_ERROR,              "Encryption - Key Wrapping Failure"},
	{0x05EE2D, -EDEV_CRYPTO_ERROR,              "Encryption - Sequencing Failure"},
	{0x05EE2E, -EDEV_CRYPTO_ERROR,              "Encryption - Unsupported Type"},
	{0x05EE2F, -EDEV_CRYPTO_ERROR,              "Encryption - New Key Encrypted Write Pending"},
	{0x05EE30, -EDEV_CRYPTO_ERROR,              "Encryption - Prohibited Request"},
	{0x05EE31, -EDEV_CRYPTO_ERROR,              "Encryption - Key Unknown"},
	{0x05EE32, -EDEV_CRYPTO_ERROR,              "Encryption - Unauthorized Request - dCERT"},
	{0x05EE42, -EDEV_CRYPTO_ERROR,              "Encryption - EKM Challenge Pending"},
	{0x05EEE2, -EDEV_CRYPTO_ERROR,              "Encryption - Key Translation Disallowed"},
	{0x05EEFF, -EDEV_CRYPTO_ERROR,              "Encryption - Security Prohibited Function"},
	{0x05EF01, -EDEV_CRYPTO_ERROR,              "Encryption - Key Service Not Configured"},
	{0x06EE11, -EDEV_CRYPTO_ERROR,              "Encryption - Key Generation"},
	{0x06EE12, -EDEV_KEY_CHANGE_DETECTED,       "Encryption - Key Change Detected"},
	{0x06EE13, -EDEV_CRYPTO_ERROR,              "Encryption - Key Translation"},
	{0x06EE18, -EDEV_KEY_CHANGE_DETECTED,       "Encryption - Changed (Read)"},
	{0x06EE19, -EDEV_KEY_CHANGE_DETECTED,       "Encryption - Changed (Write)"},
	{0x06EE40, -EDEV_CRYPTO_ERROR,              "Encryption - EKM Identifier Changed"},
	{0x06EE41, -EDEV_CRYPTO_ERROR,              "Encryption - EKM Challenge Changed"},
	{0x06EE50, -EDEV_CRYPTO_ERROR,              "Encryption - Initiator Identifier Changed"},
	{0x06EE51, -EDEV_CRYPTO_ERROR,              "Encryption - Initiator Response Changed"},
	{0x06EF01, -EDEV_CRYPTO_ERROR,              "Encryption - Key Service Not Configured"},
	{0x06EF10, -EDEV_CRYPTO_ERROR,              "Encryption - Key Required"},
	{0x06EF11, -EDEV_CRYPTO_ERROR,              "Encryption - Key Generation"},
	{0x06EF13, -EDEV_CRYPTO_ERROR,              "Encryption - Key Translation"},
	{0x06EF1A, -EDEV_CRYPTO_ERROR,              "Encryption - Key Optional (i.e., chose encryption enabled/disabled)"},
	{0x07EE0E, -EDEV_KEY_SERVICE_ERROR,         "Encryption - Key Service Timeout"}, /* LTO6, Jag5 and later */
	{0x07EE0F, -EDEV_KEY_SERVICE_ERROR,         "Encryption - Key Service Failure"}, /* LTO6, Jag5 and later */
	{0x07EF10, -EDEV_KEY_REQUIRED,              "Encryption - Key Required"},
	{0x07EF11, -EDEV_CRYPTO_ERROR,              "Encryption - Key Generation"},
	{0x07EF13, -EDEV_CRYPTO_ERROR,              "Encryption - Key Translate"},
	{0x07EF1A, -EDEV_CRYPTO_ERROR,              "Encryption - Key Optional"},
	{0x07EF31, -EDEV_CRYPTO_ERROR,              "Encryption - Key Unknown"},
	{0x07EFC0, -EDEV_CRYPTO_ERROR,              "Encryption - No Operation"},
	/* END MARK*/
	{0xFFFFFF, -EDEV_UNKNOWN,                   "Unknown Error code"},
};

/* TODO: Choose best HASH functions defined into UThash ! */
//#undef HASH_FCN
//#define HASH_FCN HASH_BER

#define DEFAULT_TIMEOUT (60)

struct _timeout_tape{
	int  op_code;     /**< SCSI op code */
	int  timeout;     /**< SCSI timeout */
};

/* Base timeout value for LTO */
static struct _timeout_tape timeout_lto[] = {
	{ CHANGE_DEFINITION,               -1    },
	{ XCOPY,                           -1    },
	{ INQUIRY,                         60    },
	{ LOG_SELECT,                      60    },
	{ LOG_SENSE,                       60    },
	{ MODE_SELECT6,                    60    },
	{ MODE_SELECT10,                   60    },
	{ MODE_SENSE6,                     60    },
	{ MODE_SENSE10,                    60    },
	{ PERSISTENT_RESERVE_IN,           60    },
	{ PERSISTENT_RESERVE_OUT,          60    },
	{ READ_ATTRIBUTE,                  60    },
	{ RECEIVE_DIAGNOSTIC_RESULTS,      60    },
	{ RELEASE_UNIT6,                   60    },
	{ RELEASE_UNIT10,                  60    },
	{ REPORT_LUNS,                     60    },
	{ REQUEST_SENSE,                   60    },
	{ RESERVE_UNIT6,                   60    },
	{ RESERVE_UNIT10,                  60    },
	{ SPIN,                            60    },
	{ SPOUT,                           60    },
	{ TEST_UNIT_READY,                 60    },
	{ WRITE_ATTRIBUTE,                 60    },
	{ ALLOW_OVERWRITE,                 60    },
	{ DISPLAY_MESSAGE,                 -1    },
	{ PREVENT_ALLOW_MEDIUM_REMOVAL,    60    },
	{ READ_BLOCK_LIMITS,               60    },
	{ READ_DYNAMIC_RUNTIME_ATTRIBUTE,  60    },
	{ READ_POSITION,                   60    },
	{ READ_REVERSE,                    -1    },
	{ RECOVER_BUFFERED_DATA,           -1    },
	{ REPORT_DENSITY_SUPPORT,          60    },
	{ STRING_SEARCH,                   -1    },
	{ WRITE_DYNAMIC_RUNTIME_ATTRIBUTE, 60    },
	{-1, -1}
};

static struct _timeout_tape timeout_lto5[] = {
	{ ERASE,                           16380 },
	{ FORMAT_MEDIUM,                   1560  },
	{ LOAD_UNLOAD,                     780   },
	{ LOCATE10,                        2040  },
	{ LOCATE16,                        2040  },
	{ READ,                            1500  },
	{ READ_BUFFER,                     480   },
	{ REWIND,                          600   },
	{ SEND_DIAGNOSTIC,                 2100  },
	{ SET_CAPACITY,                    780   },
	{ SPACE6,                          2040  },
	{ SPACE16,                         2040  },
	{ VERIFY,                          16920 },
	{ WRITE,                           1500  },
	{ WRITE_BUFFER,                    540   },
	{ WRITE_FILEMARKS6,                1620  },
	{-1, -1}
};

static struct _timeout_tape timeout_lto6[] = {
	{ ERASE,                           24600 },
	{ FORMAT_MEDIUM,                   3000  },
	{ LOAD_UNLOAD,                     780   },
	{ LOCATE10,                        2040  },
	{ LOCATE16,                        2040  },
	{ READ,                            1500  },
	{ READ_BUFFER,                     480   },
	{ REWIND,                          600   },
	{ SEND_DIAGNOSTIC,                 2100  },
	{ SET_CAPACITY,                    780   },
	{ SPACE6,                          2040  },
	{ SPACE16,                         2040  },
	{ VERIFY,                          25200 },
	{ WRITE,                           1500  },
	{ WRITE_BUFFER,                    540   },
	{ WRITE_FILEMARKS6,                1620  },
	{-1, -1}
};

static struct _timeout_tape timeout_lto7[] = {
	{ ERASE,                           27540 },
	{ FORMAT_MEDIUM,                   3000  },
	{ LOAD_UNLOAD,                     780   },
	{ LOCATE10,                        2880  },
	{ LOCATE16,                        2880  },
	{ READ,                            2280  },
	{ READ_BUFFER,                     480   },
	{ REWIND,                          600   },
	{ SEND_DIAGNOSTIC,                 1980  },
	{ SET_CAPACITY,                    780   },
	{ SPACE6,                          2880  },
	{ SPACE16,                         2880  },
	{ VERIFY,                          28860 },
	{ WRITE,                           1500  },
	{ WRITE_BUFFER,                    540   },
	{ WRITE_FILEMARKS6,                1620  },
	{-1, -1}
};

static struct _timeout_tape timeout_lto8[] = {
	{ ERASE,                           46380 },
	{ FORMAT_MEDIUM,                   3000  },
	{ LOAD_UNLOAD,                     780   },
	{ LOCATE10,                        2880  },
	{ LOCATE16,                        2880  },
	{ READ,                            2280  },
	{ READ_BUFFER,                     480   },
	{ REWIND,                          600   },
	{ SEND_DIAGNOSTIC,                 1980  },
	{ SET_CAPACITY,                    780   },
	{ SPACE6,                          2880  },
	{ SPACE16,                         2880  },
	{ VERIFY,                          28860 },
	{ WRITE,                           1500  },
	{ WRITE_BUFFER,                    540   },
	{ WRITE_FILEMARKS6,                1620  },
	{-1, -1}
};

static struct _timeout_tape timeout_lto5_hh[] = {
	{ ERASE,                           19200 },
	{ FORMAT_MEDIUM,                   1980  },
	{ LOAD_UNLOAD,                     1020  },
	{ LOCATE10,                        2700  },
	{ LOCATE16,                        2700  },
	{ READ,                            1920  },
	{ READ_BUFFER,                     660   },
	{ REWIND,                          780   },
	{ SEND_DIAGNOSTIC,                 3120  },
	{ SET_CAPACITY,                    960   },
	{ SPACE6,                          2700  },
	{ SPACE16,                         2700  },
	{ VERIFY,                          19980 },
	{ WRITE,                           1920  },
	{ WRITE_BUFFER,                    720   },
	{ WRITE_FILEMARKS6,                1740  },
	{-1, -1}
};

static struct _timeout_tape timeout_lto6_hh[] = {
	{ ERASE,                           29400 },
	{ FORMAT_MEDIUM,                   3840  },
	{ LOAD_UNLOAD,                     1020  },
	{ LOCATE10,                        2700  },
	{ LOCATE16,                        2700  },
	{ READ,                            1920  },
	{ READ_BUFFER,                     660   },
	{ REWIND,                          780   },
	{ SEND_DIAGNOSTIC,                 3120  },
	{ SET_CAPACITY,                    960   },
	{ SPACE6,                          2700  },
	{ SPACE16,                         2700  },
	{ VERIFY,                          30000 },
	{ WRITE,                           1920  },
	{ WRITE_BUFFER,                    720   },
	{ WRITE_FILEMARKS6,                1740  },
	{-1, -1}
};

static struct _timeout_tape timeout_lto7_hh[] = {
	{ ERASE,                           27540 },
	{ FORMAT_MEDIUM,                   3240  },
	{ LOAD_UNLOAD,                     840   },
	{ LOCATE10,                        2940  },
	{ LOCATE16,                        2940  },
	{ READ,                            2340  },
	{ READ_BUFFER,                     480   },
	{ REWIND,                          660   },
	{ SEND_DIAGNOSTIC,                 2040  },
	{ SET_CAPACITY,                    960   },
	{ SPACE6,                          2940  },
	{ SPACE16,                         2940  },
	{ VERIFY,                          28860 },
	{ WRITE,                           1560  },
	{ WRITE_BUFFER,                    540   },
	{ WRITE_FILEMARKS6,                1680  },
	{-1, -1}
};

static struct _timeout_tape timeout_lto8_hh[] = {
	{ ERASE,                           46380 },
	{ FORMAT_MEDIUM,                   3240  },
	{ LOAD_UNLOAD,                     840   },
	{ LOCATE10,                        2940  },
	{ LOCATE16,                        2940  },
	{ READ,                            2340  },
	{ READ_BUFFER,                     480   },
	{ REWIND,                          660   },
	{ SEND_DIAGNOSTIC,                 2040  },
	{ SET_CAPACITY,                    960   },
	{ SPACE6,                          2940  },
	{ SPACE16,                         2940  },
	{ VERIFY,                          47700 },
	{ WRITE,                           1560  },
	{ WRITE_BUFFER,                    540   },
	{ WRITE_FILEMARKS6,                1680  },
	{-1, -1}
};

static struct _timeout_tape timeout_11x0[] = {
	{ CHANGE_DEFINITION,               30    },
	{ INQUIRY,                         30    },
	{ LOG_SELECT,                      30    },
	{ LOG_SENSE,                       30    },
	{ MODE_SELECT6,                    300   }, /* For scale change */
	{ MODE_SELECT10,                   300   }, /* For scale change */
	{ MODE_SENSE6,                     30    },
	{ MODE_SENSE10,                    30    },
	{ PERSISTENT_RESERVE_IN,           30    },
	{ PERSISTENT_RESERVE_OUT,          900   },
	{ READ_ATTRIBUTE,                  30    },
	{ RECEIVE_DIAGNOSTIC_RESULTS,      60    },
	{ RELEASE_UNIT6,                   60    },
	{ RELEASE_UNIT10,                  60    },
	{ REPORT_LUNS,                     60    },
	{ REQUEST_SENSE,                   60    },
	{ RESERVE_UNIT6,                   60    },
	{ RESERVE_UNIT10,                  60    },
	{ SPIN,                           300    },
	{ SPOUT,                          300    },
	{ TEST_UNIT_READY,                 30    },
	{ WRITE_ATTRIBUTE,                 30    },
	{ ALLOW_OVERWRITE,                 30    },
	{ DISPLAY_MESSAGE,                 30    },
	{ PREVENT_ALLOW_MEDIUM_REMOVAL,    30    },
	{ READ_BLOCK_LIMITS,               30    },
	{ READ_DYNAMIC_RUNTIME_ATTRIBUTE,  30    },
	{ READ_POSITION,                   30    },
	{ READ_REVERSE,                    1080  },
	{ RECOVER_BUFFERED_DATA,           60    },
	{ REPORT_DENSITY_SUPPORT,          30    },
	{ SET_CAPACITY,                    -1    },
	{ STRING_SEARCH,                   -1    },
	{ WRITE_DYNAMIC_RUNTIME_ATTRIBUTE, 30    },
	{-1, -1}
};

static struct _timeout_tape timeout_1140[] = {
	{ XCOPY,                           -1    },
	{ ERASE,                           36900 },
	{ FORMAT_MEDIUM,                   3000  },
	{ LOAD_UNLOAD,                     720   },
	{ LOCATE10,                        2000  },
	{ LOCATE16,                        2000  },
	{ READ,                            2100  },
	{ READ_BUFFER,                     300   },
	{ REWIND,                          480   },
	{ SEND_DIAGNOSTIC,                 2100  },
	{ SPACE6,                          2000  },
	{ SPACE16,                         2000  },
	{ VERIFY,                          38100 },
	{ WRITE,                           1200  },
	{ WRITE_BUFFER,                    540   },
	{ WRITE_FILEMARKS6,                1100  },
	{-1, -1}
};

static struct _timeout_tape timeout_1150[] = {
	{ XCOPY,                           18000 },
	{ ERASE,                           45800 },
	{ FORMAT_MEDIUM,                   3100  },
	{ LOAD_UNLOAD,                     900   },
	{ LOCATE10,                        2300  },
	{ LOCATE16,                        2300  },
	{ READ,                            2400  },
	{ READ_BUFFER,                     480   },
	{ REWIND,                          560   },
	{ SEND_DIAGNOSTIC,                 2100  },
	{ SPACE6,                          2300  },
	{ SPACE16,                         2300  },
	{ VERIFY,                          46700 },
	{ WRITE,                           1500  },
	{ WRITE_BUFFER,                    540   },
	{ WRITE_FILEMARKS6,                1400  },
	{-1, -1}
};

static struct _timeout_tape timeout_1155[] = {
	{ XCOPY,                           68900 },
	{ ERASE,                           68000 },
	{ FORMAT_MEDIUM,                   3100  },
	{ LOAD_UNLOAD,                     900   },
	{ LOCATE10,                        2300  },
	{ LOCATE16,                        2300  },
	{ READ,                            2400  },
	{ READ_BUFFER,                     480   },
	{ REWIND,                          560   },
	{ SEND_DIAGNOSTIC,                 2100  },
	{ SPACE6,                          2300  },
	{ SPACE16,                         2300  },
	{ VERIFY,                          68900 },
	{ WRITE,                           1500  },
	{ WRITE_BUFFER,                    540   },
	{ WRITE_FILEMARKS6,                1400  },
	{-1, -1}
};

static struct _timeout_tape timeout_1160[] = {
	{ XCOPY,                           68900 },
	{ ERASE,                           64860 },
	{ FORMAT_MEDIUM,                   3060  },
	{ LOAD_UNLOAD,                     900   },
	{ LOCATE10,                        2280  },
	{ LOCATE16,                        2280  },
	{ READ,                            2340  },
	{ READ_BUFFER,                     480   },
	{ REWIND,                          600   },
	{ SEND_DIAGNOSTIC,                 2100  },
	{ SPACE6,                          2380  },
	{ SPACE16,                         2380  },
	{ VERIFY,                          65820 },
	{ WRITE,                           1440  },
	{ WRITE_BUFFER,                    530   },
	{ WRITE_FILEMARKS6,                1380  },
	{-1, -1}
};

static int _create_table_tape(struct timeout_tape **result,
							  struct _timeout_tape* base,
							  struct _timeout_tape* override)
{
	struct _timeout_tape* cur;
	struct timeout_tape* entry;

	entry = malloc(sizeof(struct timeout_tape));

	entry->op_code  = base->op_code;
	entry->timeout = base->timeout;
	HASH_ADD_INT(*result, op_code, entry);
	if (! *result) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return -LTFS_NO_MEMORY;
	}

	for ( cur = base + 1; cur->op_code != -1; ++cur) {
		entry = malloc(sizeof(struct timeout_tape));
		entry->op_code  = cur->op_code;
		entry->timeout = cur->timeout;
		HASH_ADD_INT(*result, op_code, entry);
	}

	for ( cur = override; cur->op_code != -1; ++cur) {
		entry = malloc(sizeof(struct timeout_tape));
		entry->op_code  = cur->op_code;
		entry->timeout = cur->timeout;
		HASH_ADD_INT(*result, op_code, entry);
	}

	return 0;
}

int ibm_tape_init_timeout(struct timeout_tape** table, int type)
{
	int ret = 0;

	/* Clear the table if it is already created */
	HASH_CLEAR(hh, *table);

	switch (type) {
		case DRIVE_LTO5:
			ret = _create_table_tape(table, timeout_lto, timeout_lto5);
			break;
		case DRIVE_LTO5_HH:
			ret = _create_table_tape(table, timeout_lto, timeout_lto5_hh);
			break;
		case DRIVE_LTO6:
			ret = _create_table_tape(table, timeout_lto, timeout_lto6);
			break;
		case DRIVE_LTO6_HH:
			ret = _create_table_tape(table, timeout_lto, timeout_lto6_hh);
			break;
		case DRIVE_LTO7:
			ret = _create_table_tape(table, timeout_lto, timeout_lto7);
			break;
		case DRIVE_LTO7_HH:
			ret = _create_table_tape(table, timeout_lto, timeout_lto7_hh);
			break;
		case DRIVE_LTO8:
			ret = _create_table_tape(table, timeout_lto, timeout_lto8);
			break;
		case DRIVE_LTO8_HH:
			ret = _create_table_tape(table, timeout_lto, timeout_lto8_hh);
			break;
		case DRIVE_TS1140:
			ret = _create_table_tape(table, timeout_11x0, timeout_1140);
			break;
		case DRIVE_TS1150:
			ret = _create_table_tape(table, timeout_11x0, timeout_1150);
			break;
		case DRIVE_TS1155:
			ret = _create_table_tape(table, timeout_11x0, timeout_1155);
			break;
		case DRIVE_TS1160:
			ret = _create_table_tape(table, timeout_11x0, timeout_1160);
			break;
		default:
			ret = _create_table_tape(table, timeout_lto, timeout_lto7_hh);
			break;
	}

	if (ret) {
		HASH_CLEAR(hh, *table);
	}

	return ret;
}

void ibm_tape_destroy_timeout(struct timeout_tape** table)
{
	struct timeout_tape *entry, *tmp;

	HASH_ITER(hh, *table, entry, tmp) {
		HASH_DEL(*table, entry);
		free(entry);
	}
}

int ibm_tape_get_timeout(struct timeout_tape* table, int op_code)
{
	struct timeout_tape *out = NULL;

	if (!table) {
		ltfsmsg(LTFS_WARN, 39802W, op_code);
		return DEFAULT_TIMEOUT;
	}

	HASH_FIND_INT(table, &op_code, out);

	if (out) {
		if (out->timeout == -1) {
			ltfsmsg(LTFS_WARN, 39800W, op_code);
			return -1;
		} else {
			ltfsmsg(LTFS_DEBUG3, 39801D, op_code, out->timeout);
			return out->timeout;
		}
	} else {
		ltfsmsg(LTFS_WARN, 39805W, op_code);
		return DEFAULT_TIMEOUT;
	}
}

static inline unsigned char _assume_cartridge_type(char product, char btype)
{
	unsigned char ctype = 0;

	if (product == 'J') {
		switch (btype) {
			case 'B':
				ctype = TC_MP_JB;
				break;
			case 'C':
				ctype = TC_MP_JC;
				break;
			case 'K':
				ctype = TC_MP_JK;
				break;
			case 'Y':
				ctype = TC_MP_JY;
				break;
			case 'D':
				ctype = TC_MP_JD;
				break;
			case 'L':
				ctype = TC_MP_JL;
				break;
			case 'Z':
				ctype = TC_MP_JZ;
				break;
			case 'E':
				ctype = TC_MP_JE;
				break;
			case 'V':
				ctype = TC_MP_JV;
				break;
			case 'M':
				ctype = TC_MP_JM;
				break;
			default:
				break;
		}
	} else if (product == 'L') {
		switch (btype) {
			case '5':
				ctype = TC_MP_LTO5D_CART;
				break;
			case '6':
				ctype = TC_MP_LTO6D_CART;
				break;
			case '7':
				ctype = TC_MP_LTO7D_CART;
				break;
			case '8':
				ctype = TC_MP_LTO8D_CART;
				break;
			default:
				break;
		}
	} else if (product == 'M') {
		switch (btype) {
			case '8':
				ctype = TC_MP_LTO7D_CART;
				break;
			default:
				break;
		}
	}

	return ctype;
}

unsigned char ibm_tape_assume_cart_type(const char* type_name)
{
	unsigned char c_type = 0;

	if (strlen(type_name) < 2)
		return TC_MP_LTO5D_CART;

	c_type = _assume_cartridge_type(type_name[0], type_name[1]);
	if (!c_type)
		c_type = TC_MP_LTO5D_CART;

	return c_type;
}

char* ibm_tape_assume_cart_name(unsigned char type)
{
	char *name = NULL;

	switch (type) {
		case TC_MP_LTO5D_CART:
			name = "L5";
			break;
		case TC_MP_LTO6D_CART:
			name = "L6";
			break;
		case TC_MP_LTO7D_CART:
			name = "L7";
			break;
		case TC_MP_LTO8D_CART:
			name = "L8";
			break;
		case TC_MP_JB:
			name = "JB";
			break;
		case TC_MP_JX:
			name = "JX";
			break;
		case TC_MP_JC:
			name = "JC";
			break;
		case TC_MP_JK:
			name = "JK";
			break;
		case TC_MP_JY:
			name = "JY";
			break;
		case TC_MP_JD:
			name = "JD";
			break;
		case TC_MP_JL:
			name = "JL";
			break;
		case TC_MP_JZ:
			name = "JZ";
			break;
		case TC_MP_JE:
			name = "JE";
			break;
		case TC_MP_JV:
			name = "JV";
			break;
		case TC_MP_JM:
			name = "JM";
			break;
		default:
			name = "L5";
			break;
	}

	return name;
}

static inline int _is_mountable(const int drive_type,
								const char *barcode,
								const unsigned char cart_type,
								const unsigned char density_code,
								const bool strict)
{
	int drive_generation = DRIVE_FAMILY_GEN(drive_type);
	char product = 0x00;                              /* Product type on barcode */
	char btype = 0x00;                                /* Cartridge type on barcode */
	unsigned char ctype = cart_type;                  /* Cartridge type in CM */
	unsigned char dcode = density_code & MASK_CRYPTO; /* Density code in CM */
	DRIVE_DENSITY_SUPPORT_MAP *table = NULL;
	int num_table = 0, i;
	int ret = MEDIUM_CANNOT_ACCESS;

	if (barcode) {
		product = barcode[6];
		btype = barcode[7];
	}

	if (IS_LTO(drive_type)) {
		if (product == 'L' || product == 'M' || product == 0x00) {
			if (strict) {
				table = lto_drive_density_strict;
				num_table = num_lto_drive_density_strict;
			} else {
				table = lto_drive_density;
				num_table = num_lto_drive_density;
			}
		} else {
			ltfsmsg(LTFS_INFO, 39808I, barcode);
			return MEDIUM_CANNOT_ACCESS;
		}
	} else {
		if (product == 'J' || product == 0x00) {
			if (strict) {
				table = jaguar_drive_density_strict;
				num_table = num_jaguar_drive_density_strict;
			} else {
				table = jaguar_drive_density;
				num_table = num_jaguar_drive_density;
			}
		} else {
			ltfsmsg(LTFS_INFO, 39808I, barcode);
			return MEDIUM_CANNOT_ACCESS;
		}
	}

	/* Assume cartridge type from barcode */
	if (ctype == 0)
		ctype = _assume_cartridge_type(product, btype);

	/* Special case, assume M8 as TC_DC_M8 when density code is not fetched yet */
	if (density_code == 0x00) {
		if (product == 'M' && btype == '8')
			dcode = TC_DC_LTOM8;
	}

	for (i = 0; i < num_table; i++) {
		if ( (table->drive_generation == drive_generation) &&
			 (table->cartridge_type == ctype) &&
			 (table->density_code == dcode) )
		{
			ret = table->access;
			break;
		}
		table++;
	}

	return ret;
}

int ibm_tape_is_mountable(const int drive_type,
						 const char *barcode,
						 const unsigned char cart_type,
						 const unsigned char density_code,
						 const bool strict)
{
	int ret = MEDIUM_CANNOT_ACCESS;

	if (barcode) {
		int bc_len = strlen(barcode);

		/* Check bar code length */
		switch (bc_len) {
			case 6:
				/* Always supported */
				ltfsmsg(LTFS_DEBUG, 39806D, barcode);
				return MEDIUM_WRITABLE;
			case 8:
				break;
			default:
				// invalid bar code length
				ltfsmsg(LTFS_ERR, 39807E, barcode);
				return MEDIUM_CANNOT_ACCESS;
				break;
		}
	}

	ret = _is_mountable(drive_type, barcode, cart_type, density_code, strict);

	return ret;
}

int ibm_tape_is_supported_tape(unsigned char type, unsigned char density, bool *is_worm)
{
	int ret = -LTFS_UNSUPPORTED_MEDIUM, i;

	for (i = 0; i < num_supported_cart; ++i) {
		if(type == supported_cart[i]) {
			if(IS_WORM_MEDIUM(type)) {
				/* Detect WORM cartridge */
				ltfsmsg(LTFS_DEBUG, 39809D);
				*is_worm = true;
			}
			ret = 0;
			break;
		}
	}

	if (!ret) {
		ret = -LTFS_UNSUPPORTED_MEDIUM;
		for (i = 0; i < num_supported_density; ++i) {
			if(density == supported_density[i]) {
				ret = 0;
				break;
			}
		}
	}

	return ret;
}

/**
 *  Generate a key for persistent reservation
 */
int ibm_tape_genkey(unsigned char *key)
{
#ifdef mingw_PLATFORM
	memset(key, 0x00, KEYLEN);
	*key = KEY_PREFIX_HOST;
	strncpy(key + 1, "WINLTFS", KEYLEN - 1);
#else
	unsigned char host[KEYLEN];

	struct ifaddrs *ifaddr, *ifa;
	int family, n;

	struct sockaddr_in *addr4;
	struct sockaddr_in6 *addr6;
	unsigned char key4[KEYLEN];
	unsigned char key6[KEYLEN];
	bool a4_found = false;
	bool a6_found = false;

	/* Capture host name. In the failure case, x1000000000000000 will be used */
	memset(host, 0x00, KEYLEN);
	gethostname((char *)host, KEYLEN);

	if (!getifaddrs(&ifaddr)) {
		for (ifa = ifaddr, n = 0; ifa != NULL; ifa = ifa->ifa_next, n++) {
			if (ifa->ifa_addr == NULL)
				continue;

			if (!strncmp(ifa->ifa_name, LOOP_BACK_DEVICE, 2))
				continue;

			family = ifa->ifa_addr->sa_family;

			switch (family) {
				case AF_INET:
					if (!a4_found) {
						addr4 = (struct sockaddr_in *)ifa->ifa_addr;
						memset(key4, 0x00, KEYLEN);
						key4[0] = KEY_PREFIX_IPV4;
						memcpy(key4 + 4, &addr4->sin_addr.s_addr, 4);
						a4_found = true;
					}
					break;
				case AF_INET6:
					if (!a6_found) {
						addr6 = (struct sockaddr_in6 *)ifa->ifa_addr;
						memset(key6, 0x00, KEYLEN);
						key6[0] = KEY_PREFIX_IPV6;
						memcpy(key6 + 1, addr6->sin6_addr.s6_addr + 9, 7);
						a6_found = true;
					}
					break;
				default:
					break;
			}
		}
		freeifaddrs(ifaddr);

		if (a4_found) {
			memcpy(key, key4, KEYLEN);
			return 0;
		}

		if (a6_found) {
			memcpy(key, key6, KEYLEN);
			return 0;
		}

		ltfsmsg(LTFS_WARN, 39810W);
	} else
		ltfsmsg(LTFS_WARN, 39811W, errno);

	/* Return host name based key */
	*key = KEY_PREFIX_HOST;
	memcpy(key + 1, host, KEYLEN -1);
#endif

	return 0;
}

int ibm_tape_parsekey(unsigned char *key, struct reservation_info *r)
{
	r->key_type = key[0];
	switch (r->key_type) {
		case KEY_PREFIX_IPV6:
			snprintf(r->hint, sizeof(r->hint),
					 "IPv6 (last 7 bytes): xx%02x:%02x%02x:%02x%02x:%02x%02x",
					 key[1], key[2], key[3], key[4], key[5], key[6], key[7]);
			break;
		case KEY_PREFIX_HOST:
			snprintf(r->hint, sizeof(r->hint),
					 "HOSTNAME (first 7 bytes): %c%c%c%c%c%c%c",
					 key[1], key[2], key[3], key[4], key[5], key[6], key[7]);
			break;
		case KEY_PREFIX_IPV4:
			if (!key[1] && !key[2] && !key[3]) {
				snprintf(r->hint, sizeof(r->hint),
						 "IPv4: %d.%d.%d.%d",
						 key[4], key[5], key[6], key[7]);
				break;
			} // else fall through
		default:
			snprintf(r->hint, sizeof(r->hint),
					 "KEY: x%02x%02x%02x%02x%02x%02x%02x%02x",
					 key[0], key[1], key[2], key[3], key[4], key[5], key[6], key[7]);
	}

	memcpy(r->wwid, key + 32, sizeof(r->wwid));

	return 0;
}

bool ibm_tape_is_supported_firmware(int drive_type, const unsigned char * const revision)
{
	bool supported = true;
	const uint32_t rev = ltfs_betou32(revision);

	switch (drive_type) {
	case DRIVE_LTO5:
	case DRIVE_LTO5_HH:
		if (rev < ltfs_betou32(base_firmware_level_lto5)) {
			ltfsmsg(LTFS_WARN, 39812W, base_firmware_level_lto5);
			ltfsmsg(LTFS_WARN, 39813W);
			supported = false;
		}
		break;
	case DRIVE_LTO8:
	case DRIVE_LTO8_HH:
		if (rev < ltfs_betou32(base_firmware_level_lto8)) {
			ltfsmsg(LTFS_WARN, 39812W, base_firmware_level_lto8);
			supported = false;
		}
		break;
	case DRIVE_TS1140:
		if (rev < ltfs_betou32(base_firmware_level_ts1140)) {
			ltfsmsg(LTFS_WARN, 39812W, base_firmware_level_ts1140);
			supported = false;
		}
		break;
	case DRIVE_LTO6:
	case DRIVE_LTO6_HH:
	case DRIVE_LTO7:
	case DRIVE_LTO7_HH:
	case DRIVE_TS1150:
	case DRIVE_TS1160:
	default:
		break;
	}

	return supported;
}
