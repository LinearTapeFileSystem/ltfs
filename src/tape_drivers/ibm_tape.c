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
	/* TS1170 */
	{ DRIVE_GEN_JAG7,  TC_MP_JF, TC_DC_JAG7,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG7,  TC_MP_JF, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },

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
	/* TS1170 */
	{ DRIVE_GEN_JAG7,  TC_MP_JF, TC_DC_JAG7,    MEDIUM_PERFECT_MATCH },
	{ DRIVE_GEN_JAG7,  TC_MP_JF, TC_DC_UNKNOWN, MEDIUM_PROBABLY_WRITABLE },

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
	/* LTO9 */
	{ DRIVE_GEN_LTO9, TC_MP_LTO9D_CART, TC_DC_LTO9,    MEDIUM_PERFECT_MATCH},
	{ DRIVE_GEN_LTO9, TC_MP_LTO9D_CART, TC_DC_UNKNOWN, MEDIUM_PERFECT_MATCH},
	{ DRIVE_GEN_LTO9, TC_MP_LTO8D_CART, TC_DC_LTO8,    MEDIUM_WRITABLE},
	{ DRIVE_GEN_LTO9, TC_MP_LTO8D_CART, TC_DC_UNKNOWN, MEDIUM_WRITABLE},

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
	/* LTO9 */
	{ DRIVE_GEN_LTO9, TC_MP_LTO9D_CART, TC_DC_LTO9,    MEDIUM_PERFECT_MATCH},
	{ DRIVE_GEN_LTO9, TC_MP_LTO9D_CART, TC_DC_UNKNOWN, MEDIUM_PERFECT_MATCH},

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
	TC_MP_LTO9D_CART,
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
	TC_MP_JF,
};

const unsigned char supported_density[] = {
	TC_DC_JAG7E,
	TC_DC_JAG6E,
	TC_DC_JAG5AE,
	TC_DC_JAG5E,
	TC_DC_JAG4E,
	TC_DC_JAG7,
	TC_DC_JAG6,
	TC_DC_JAG5A,
	TC_DC_JAG5,
	TC_DC_JAG4,
	TC_DC_LTO9,
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
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-TD5",  VENDOR_IBM, DRIVE_LTO5,    "[ULTRIUM-TD5]" ),  /* IBM Ultrium Gen 5 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-TD5",  VENDOR_IBM, DRIVE_LTO5,    "[ULT3580-TD5]" ),  /* IBM Ultrium Gen 5 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-HH5",  VENDOR_IBM, DRIVE_LTO5_HH, "[ULTRIUM-HH5]" ),  /* IBM Ultrium Gen 5 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-HH5",  VENDOR_IBM, DRIVE_LTO5_HH, "[ULT3580-HH5]" ),  /* IBM Ultrium Gen 5 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "HH LTO Gen 5", VENDOR_IBM, DRIVE_LTO5_HH, "[HH LTO Gen 5]" ), /* IBM Ultrium Gen 5 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-TD6",  VENDOR_IBM, DRIVE_LTO6,    "[ULTRIUM-TD6]" ),  /* IBM Ultrium Gen 6 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-TD6",  VENDOR_IBM, DRIVE_LTO6,    "[ULT3580-TD6]" ),  /* IBM Ultrium Gen 6 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-HH6",  VENDOR_IBM, DRIVE_LTO6_HH, "[ULTRIUM-HH6]" ),  /* IBM Ultrium Gen 6 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-HH6",  VENDOR_IBM, DRIVE_LTO6_HH, "[ULT3580-HH6]" ),  /* IBM Ultrium Gen 6 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "HH LTO Gen 6", VENDOR_IBM, DRIVE_LTO6_HH, "[HH LTO Gen 6]" ), /* IBM Ultrium Gen 6 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-TD7",  VENDOR_IBM, DRIVE_LTO7,    "[ULTRIUM-TD7]" ),  /* IBM Ultrium Gen 7 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-TD7",  VENDOR_IBM, DRIVE_LTO7,    "[ULT3580-TD7]" ),  /* IBM Ultrium Gen 7 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-HH7",  VENDOR_IBM, DRIVE_LTO7_HH, "[ULTRIUM-HH7]" ),  /* IBM Ultrium Gen 7 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-HH7",  VENDOR_IBM, DRIVE_LTO7_HH, "[ULT3580-HH7]" ),  /* IBM Ultrium Gen 7 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "HH LTO Gen 7", VENDOR_IBM, DRIVE_LTO7_HH, "[HH LTO Gen 7]" ), /* IBM Ultrium Gen 7 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-TD8",  VENDOR_IBM, DRIVE_LTO8,    "[ULTRIUM-TD8]" ),  /* IBM Ultrium Gen 8 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-TD8",  VENDOR_IBM, DRIVE_LTO8,    "[ULT3580-TD8]" ),  /* IBM Ultrium Gen 8 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-HH8",  VENDOR_IBM, DRIVE_LTO8_HH, "[ULTRIUM-HH8]" ),  /* IBM Ultrium Gen 8 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-HH8",  VENDOR_IBM, DRIVE_LTO8_HH, "[ULT3580-HH8]" ),  /* IBM Ultrium Gen 8 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "HH LTO Gen 8", VENDOR_IBM, DRIVE_LTO8_HH, "[HH LTO Gen 8]" ), /* IBM Ultrium Gen 8 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-TD9",  VENDOR_IBM, DRIVE_LTO9,    "[ULTRIUM-TD9]" ),  /* IBM Ultrium Gen 9 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-TD9",  VENDOR_IBM, DRIVE_LTO9,    "[ULT3580-TD9]" ),  /* IBM Ultrium Gen 9 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-HH9",  VENDOR_IBM, DRIVE_LTO9_HH, "[ULTRIUM-HH9]" ),  /* IBM Ultrium Gen 9 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-HH9",  VENDOR_IBM, DRIVE_LTO9_HH, "[ULT3580-HH9]" ),  /* IBM Ultrium Gen 9 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "HH LTO Gen 9", VENDOR_IBM, DRIVE_LTO9_HH, "[HH LTO Gen 9]" ), /* IBM Ultrium Gen 9 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "03592E07",     VENDOR_IBM, DRIVE_TS1140,  "[03592E07]" ),     /* IBM TS1140 */
		TAPEDRIVE( IBM_VENDOR_ID, "03592E08",     VENDOR_IBM, DRIVE_TS1150,  "[03592E08]" ),     /* IBM TS1150 */
		TAPEDRIVE( IBM_VENDOR_ID, "0359255F",     VENDOR_IBM, DRIVE_TS1155,  "[0359255F]" ),     /* IBM TS1155 */
		TAPEDRIVE( IBM_VENDOR_ID, "0359255E",     VENDOR_IBM, DRIVE_TS1155,  "[0359255E]" ),     /* IBM TS1155 */
		TAPEDRIVE( IBM_VENDOR_ID, "0359260F",     VENDOR_IBM, DRIVE_TS1160,  "[0359260F]" ),     /* IBM TS1160 */
		TAPEDRIVE( IBM_VENDOR_ID, "0359260E",     VENDOR_IBM, DRIVE_TS1160,  "[0359260E]" ),     /* IBM TS1160 */
		TAPEDRIVE( IBM_VENDOR_ID, "0359260S",     VENDOR_IBM, DRIVE_TS1160,  "[0359260S]" ),     /* IBM TS1160 */
		TAPEDRIVE( IBM_VENDOR_ID, "0359270F",     VENDOR_IBM, DRIVE_TS1170,  "[0359270F]" ),     /* IBM TS1170 */
		TAPEDRIVE( IBM_VENDOR_ID, "0359270S",     VENDOR_IBM, DRIVE_TS1170,  "[0359270S]" ),     /* IBM TS1170 */
		/* End of supported_devices */
		NULL
};

struct supported_device *usb_supported_drives[] = {
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-TD5",  VENDOR_IBM, DRIVE_LTO5,    "[ULT3580-TD5]" ), /* IBM Ultrium Gen 5 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-HH5",  VENDOR_IBM, DRIVE_LTO5_HH, "[ULTRIUM-HH5]" ), /* IBM Ultrium Gen 5 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-HH5",  VENDOR_IBM, DRIVE_LTO5_HH, "[ULT3580-HH5]" ), /* IBM Ultrium Gen 5 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-TD6",  VENDOR_IBM, DRIVE_LTO6,    "[ULT3580-TD6]" ), /* IBM Ultrium Gen 6 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-HH6",  VENDOR_IBM, DRIVE_LTO6_HH, "[ULTRIUM-HH6]" ), /* IBM Ultrium Gen 6 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-HH6",  VENDOR_IBM, DRIVE_LTO6_HH, "[ULT3580-HH6]" ), /* IBM Ultrium Gen 6 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-TD7",  VENDOR_IBM, DRIVE_LTO7,    "[ULT3580-TD7]" ), /* IBM Ultrium Gen 7 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-HH7",  VENDOR_IBM, DRIVE_LTO7_HH, "[ULTRIUM-HH7]" ), /* IBM Ultrium Gen 7 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-HH7",  VENDOR_IBM, DRIVE_LTO7_HH, "[ULT3580-HH7]" ), /* IBM Ultrium Gen 7 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-TD8",  VENDOR_IBM, DRIVE_LTO8,    "[ULT3580-TD8]" ), /* IBM Ultrium Gen 8 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-HH8",  VENDOR_IBM, DRIVE_LTO8_HH, "[ULTRIUM-HH8]" ), /* IBM Ultrium Gen 8 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-HH8",  VENDOR_IBM, DRIVE_LTO8_HH, "[ULT3580-HH8]" ), /* IBM Ultrium Gen 8 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-TD9",  VENDOR_IBM, DRIVE_LTO9,    "[ULT3580-TD9]" ), /* IBM Ultrium Gen 9 */
		TAPEDRIVE( IBM_VENDOR_ID, "ULTRIUM-HH9",  VENDOR_IBM, DRIVE_LTO9_HH, "[ULTRIUM-HH9]" ), /* IBM Ultrium Gen 9 Half-High */
		TAPEDRIVE( IBM_VENDOR_ID, "ULT3580-HH9",  VENDOR_IBM, DRIVE_LTO9_HH, "[ULT3580-HH9]" ), /* IBM Ultrium Gen 9 Half-High */
		/* End of supported_devices */
		NULL
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
	{ LOCATE10,                        2940  },
	{ LOCATE16,                        2940  },
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
	{ LOAD_UNLOAD,                     960   },
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
	{ ERASE,                           54896 },
	{ FORMAT_MEDIUM,                   3000  },
	{ LOAD_UNLOAD,                     960   },
	{ LOCATE10,                        2880  },
	{ LOCATE16,                        2880  },
	{ READ,                            2280  },
	{ READ_BUFFER,                     480   },
	{ REWIND,                          600   },
	{ SEND_DIAGNOSTIC,                 1980  },
	{ SET_CAPACITY,                    780   },
	{ SPACE6,                          2880  },
	{ SPACE16,                         2880  },
	{ VERIFY,                          47700 },
	{ WRITE,                           1500  },
	{ WRITE_BUFFER,                    540   },
	{ WRITE_FILEMARKS6,                1620  },
	{-1, -1}
};

static struct _timeout_tape timeout_lto9[] = {
	{ ERASE,                           74341 },
	{ FORMAT_MEDIUM,                   3000  },
	{ LOAD_UNLOAD,                     960   },
	{ LOCATE10,                        2940  },
	{ LOCATE16,                        2940  },
	{ READ,                            2340  },
	{ READ_BUFFER,                     480   },
	{ REWIND,                          600   },
	{ SEND_DIAGNOSTIC,                 1980  },
	{ SET_CAPACITY,                    780   },
	{ SPACE6,                          2940  },
	{ SPACE16,                         2940  },
	{ VERIFY,                          63300 },
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
	{ LOAD_UNLOAD,                     960   },
	{ LOCATE10,                        2940  },
	{ LOCATE16,                        2940  },
	{ READ,                            2340  },
	{ READ_BUFFER,                     480   },
	{ REWIND,                          600   },
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
	{ ERASE,                           121448 },
	{ FORMAT_MEDIUM,                   3240   },
	{ LOAD_UNLOAD,                     960    },
	{ LOCATE10,                        2940   },
	{ LOCATE16,                        2940   },
	{ READ,                            2340   },
	{ READ_BUFFER,                     480    },
	{ REWIND,                          600    },
	{ SEND_DIAGNOSTIC,                 2040   },
	{ SET_CAPACITY,                    960    },
	{ SPACE6,                          2940   },
	{ SPACE16,                         2940   },
	{ VERIFY,                          54360  },
	{ WRITE,                           1560   },
	{ WRITE_BUFFER,                    540    },
	{ WRITE_FILEMARKS6,                1680   },
	{-1, -1}
};

static struct _timeout_tape timeout_lto9_hh[] = {
	{ ERASE,                           166370 },
	{ FORMAT_MEDIUM,                   3240   },
	{ LOAD_UNLOAD,                     960    },
	{ LOCATE10,                        2940   },
	{ LOCATE16,                        2940   },
	{ READ,                            2340   },
	{ READ_BUFFER,                     480    },
	{ REWIND,                          600    },
	{ SEND_DIAGNOSTIC,                 2040   },
	{ SET_CAPACITY,                    960    },
	{ SPACE6,                          2940   },
	{ SPACE16,                         2940   },
	{ VERIFY,                          63300  },
	{ WRITE,                           1560   },
	{ WRITE_BUFFER,                    540    },
	{ WRITE_FILEMARKS6,                1680   },
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

static struct _timeout_tape timeout_1170[] = {
	{ XCOPY,                           176820 },
	{ ERASE,                           175900 },
	{ FORMAT_MEDIUM,                   3120   },
	{ LOAD_UNLOAD,                     900    },
	{ LOCATE10,                        2280   },
	{ LOCATE16,                        2240   },
	{ READ,                            2340   },
	{ READ_BUFFER,                     480    },
	{ REWIND,                          600    },
	{ SEND_DIAGNOSTIC,                 2280   },
	{ SPACE6,                          2280   },
	{ SPACE16,                         2240   },
	{ VERIFY,                          176820 },
	{ WRITE,                           1440   },
	{ WRITE_BUFFER,                    540    },
	{ WRITE_FILEMARKS6,                1380   },
	{-1, -1}
};

static int _create_table_tape(struct timeout_tape **result,
							  struct _timeout_tape* base,
							  struct _timeout_tape* override)
{
	struct _timeout_tape* cur;
	struct timeout_tape* entry;
	struct timeout_tape *out = NULL;

	entry = malloc(sizeof(struct timeout_tape));
	entry->op_code  = override->op_code;
	entry->timeout = override->timeout;
	HASH_ADD_INT(*result, op_code, entry);
	if (! *result) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return -LTFS_NO_MEMORY;
	}

	for ( cur = override; cur->op_code != -1; ++cur) {
		entry = malloc(sizeof(struct timeout_tape));
		entry->op_code  = cur->op_code;
		entry->timeout = cur->timeout;
		HASH_ADD_INT(*result, op_code, entry);
	}

	for ( cur = base; cur->op_code != -1; ++cur) {
		out = NULL;
		HASH_FIND_INT(*result, &cur->op_code, out);
		if (!out) {
			entry = malloc(sizeof(struct timeout_tape));
			entry->op_code  = cur->op_code;
			entry->timeout = cur->timeout;
			HASH_ADD_INT(*result, op_code, entry);
		}
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
		case DRIVE_LTO9:
			ret = _create_table_tape(table, timeout_lto, timeout_lto9);
			break;
		case DRIVE_LTO9_HH:
			ret = _create_table_tape(table, timeout_lto, timeout_lto9_hh);
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
		case DRIVE_TS1170:
			ret = _create_table_tape(table, timeout_11x0, timeout_1170);
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
			case 'F':
				ctype = TC_MP_JF;
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
			case '9':
				ctype = TC_MP_LTO9D_CART;
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
		case TC_MP_LTO9D_CART:
			name = "L9";
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
		case TC_MP_JF:
			name = "JF";
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
	char product = 0x00;             /* Product type on barcode */
	char btype = 0x00;               /* Cartridge type on barcode */
	unsigned char ctype = cart_type; /* Cartridge type in CM */
	unsigned char dcode = 0;         /* Density code in CM */
	DRIVE_DENSITY_SUPPORT_MAP *table = NULL;
	int num_table = 0, i;
	int ret = MEDIUM_CANNOT_ACCESS;

	if (barcode) {
		product = barcode[6];
		btype = barcode[7];
	}

	if (IS_LTO(drive_type)) {
		dcode = density_code;
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
		dcode = density_code & MASK_CRYPTO;
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
	case DRIVE_TS1170:
	default:
		break;
	}

	return supported;
}
