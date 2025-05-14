/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2025 IBM Corp. All rights reserved.
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
** FILE NAME:       reed_solomon_crc.c
**
** DESCRIPTION:     Implements Reed-Solomon GF(256) CRC usd by LTO based drives
**
** AUTHOR:          Atsushi Abe
**                  IBM Yamato, Japan
**                  PISTE@jp.ibm.com
**
*************************************************************************************
*/

#define __reed_solomon_crc_c

#include <inttypes.h>
#include <sys/types.h>

#include "libltfs/ltfslogging.h"
#include "libltfs/ltfs_endian.h"

static const uint32_t rs_gf256_table[256] =
{
	0x00000000,0x38CF3801,0x70837002,0x484C4803,0xE01BE004,0xD8D4D805,
	0x90989006,0xA857A807,0xDD36DD08,0xE5F9E509,0xADB5AD0A,0x957A950B,
	0x3D2D3D0C,0x05E2050D,0x4DAE4D0E,0x7561750F,0xA76CA710,0x9FA39F11,
	0xD7EFD712,0xEF20EF13,0x47774714,0x7FB87F15,0x37F43716,0x0F3B0F17,
	0x7A5A7A18,0x42954219,0x0AD90A1A,0x3216321B,0x9A419A1C,0xA28EA21D,
	0xEAC2EA1E,0xD20DD21F,0x53D85320,0x6B176B21,0x235B2322,0x1B941B23,
	0xB3C3B324,0x8B0C8B25,0xC340C326,0xFB8FFB27,0x8EEE8E28,0xB621B629,
	0xFE6DFE2A,0xC6A2C62B,0x6EF56E2C,0x563A562D,0x1E761E2E,0x26B9262F,
	0xF4B4F430,0xCC7BCC31,0x84378432,0xBCF8BC33,0x14AF1434,0x2C602C35,
	0x642C6436,0x5CE35C37,0x29822938,0x114D1139,0x5901593A,0x61CE613B,
	0xC999C93C,0xF156F13D,0xB91AB93E,0x81D5813F,0xA6ADA640,0x9E629E41,
	0xD62ED642,0xEEE1EE43,0x46B64644,0x7E797E45,0x36353646,0x0EFA0E47,
	0x7B9B7B48,0x43544349,0x0B180B4A,0x33D7334B,0x9B809B4C,0xA34FA34D,
	0xEB03EB4E,0xD3CCD34F,0x01C10150,0x390E3951,0x71427152,0x498D4953,
	0xE1DAE154,0xD915D955,0x91599156,0xA996A957,0xDCF7DC58,0xE438E459,
	0xAC74AC5A,0x94BB945B,0x3CEC3C5C,0x0423045D,0x4C6F4C5E,0x74A0745F,
	0xF575F560,0xCDBACD61,0x85F68562,0xBD39BD63,0x156E1564,0x2DA12D65,
	0x65ED6566,0x5D225D67,0x28432868,0x108C1069,0x58C0586A,0x600F606B,
	0xC858C86C,0xF097F06D,0xB8DBB86E,0x8014806F,0x52195270,0x6AD66A71,
	0x229A2272,0x1A551A73,0xB202B274,0x8ACD8A75,0xC281C276,0xFA4EFA77,
	0x8F2F8F78,0xB7E0B779,0xFFACFF7A,0xC763C77B,0x6F346F7C,0x57FB577D,
	0x1FB71F7E,0x2778277F,0x51475180,0x69886981,0x21C42182,0x190B1983,
	0xB15CB184,0x89938985,0xC1DFC186,0xF910F987,0x8C718C88,0xB4BEB489,
	0xFCF2FC8A,0xC43DC48B,0x6C6A6C8C,0x54A5548D,0x1CE91C8E,0x2426248F,
	0xF62BF690,0xCEE4CE91,0x86A88692,0xBE67BE93,0x16301694,0x2EFF2E95,
	0x66B36696,0x5E7C5E97,0x2B1D2B98,0x13D21399,0x5B9E5B9A,0x6351639B,
	0xCB06CB9C,0xF3C9F39D,0xBB85BB9E,0x834A839F,0x029F02A0,0x3A503AA1,
	0x721C72A2,0x4AD34AA3,0xE284E2A4,0xDA4BDAA5,0x920792A6,0xAAC8AAA7,
	0xDFA9DFA8,0xE766E7A9,0xAF2AAFAA,0x97E597AB,0x3FB23FAC,0x077D07AD,
	0x4F314FAE,0x77FE77AF,0xA5F3A5B0,0x9D3C9DB1,0xD570D5B2,0xEDBFEDB3,
	0x45E845B4,0x7D277DB5,0x356B35B6,0x0DA40DB7,0x78C578B8,0x400A40B9,
	0x084608BA,0x308930BB,0x98DE98BC,0xA011A0BD,0xE85DE8BE,0xD092D0BF,
	0xF7EAF7C0,0xCF25CFC1,0x876987C2,0xBFA6BFC3,0x17F117C4,0x2F3E2FC5,
	0x677267C6,0x5FBD5FC7,0x2ADC2AC8,0x121312C9,0x5A5F5ACA,0x629062CB,
	0xCAC7CACC,0xF208F2CD,0xBA44BACE,0x828B82CF,0x508650D0,0x684968D1,
	0x200520D2,0x18CA18D3,0xB09DB0D4,0x885288D5,0xC01EC0D6,0xF8D1F8D7,
	0x8DB08DD8,0xB57FB5D9,0xFD33FDDA,0xC5FCC5DB,0x6DAB6DDC,0x556455DD,
	0x1D281DDE,0x25E725DF,0xA432A4E0,0x9CFD9CE1,0xD4B1D4E2,0xEC7EECE3,
	0x442944E4,0x7CE67CE5,0x34AA34E6,0x0C650CE7,0x790479E8,0x41CB41E9,
	0x098709EA,0x314831EB,0x991F99EC,0xA1D0A1ED,0xE99CE9EE,0xD153D1EF,
	0x035E03F0,0x3B913BF1,0x73DD73F2,0x4B124BF3,0xE345E3F4,0xDB8ADBF5,
	0x93C693F6,0xAB09ABF7,0xDE68DEF8,0xE6A7E6F9,0xAEEBAEFA,0x962496FB,
	0x3E733EFC,0x06BC06FD,0x4EF04EFE,0x763F76FF
};

static inline void enc4(const unsigned char in, uint32_t *reg)
{
	*reg = (*reg << 8) ^ rs_gf256_table[in ^ (*reg >> 24)];
}

void *memcpy_rs_gf256_enc(void *dest, const void *src, size_t n)
{
	unsigned char *dest_cur = (unsigned char *)dest, *src_cur = (unsigned char *)src;
	uint32_t reg = 0;
	size_t i;

	for(i = 0; i < n; i++){
		/* Inject CRC value to the end of the destination buffer */
		*dest_cur = *src_cur;

		/* Calculate CRC */
		enc4(*src_cur, &reg);

		/* Increment pointers */
		dest_cur++;
		src_cur++;
	}

	/* Inject CRC value to the end of the destination buffer */
	ltfs_u32tobe(dest_cur, reg);
	ltfsmsg(LTFS_DEBUG, 39804D, "encode", (int)n, reg);

	return dest;
}

void rs_gf256_enc(void *buf, size_t n)
{
	unsigned char *buf_cur = (unsigned char *)buf;
	unsigned char *reg_cur = (unsigned char *)buf + n;
	uint32_t reg = 0;
	size_t i;

	for(i = 0; i < n; i++){
		/* Calculate CRC */
		enc4(*buf_cur, &reg);
		buf_cur++;
	}

	/* Inject CRC value */
	ltfs_u32tobe(reg_cur, reg);
	ltfsmsg(LTFS_DEBUG, 39804D, "encode", (int)n, reg);

	return;
}

int memcpy_rs_gf256_check(void *dest, const void *src, size_t n)
{
	unsigned char *dest_cur = (unsigned char *)dest, *src_cur = (unsigned char *)src;
	uint32_t reg = 0, crc;
	size_t i;

	for(i = 0; i < n; i++){
		/* Inject CRC value to the end of the destination buffer */
		*dest_cur = *src_cur;

		/* Calculate CRC */
		enc4(*src_cur, &reg);

		/* Increment pointers */
		dest_cur++;
		src_cur++;
	}

	/* Check CRC value in the end of the source buffer */
	crc = ltfs_betou32(src_cur);
	if(crc != reg) {
		ltfsmsg(LTFS_ERR, 39803E, (int)n, reg, crc);
		return -1;
	}
	ltfsmsg(LTFS_DEBUG, 39804D, "check", (int)n, crc);

	return n;
}

int rs_gf256_check(void *buf, size_t n)
{
	unsigned char *buf_cur = (unsigned char *)buf;
	uint32_t reg = 0, crc;
	size_t i;

	for(i = 0; i < n; i++){
		/* Calculate CRC */
		enc4(*buf_cur, &reg);
		buf_cur++;
	}

	/* Check CRC value in the end of the buffer */
	crc = ltfs_betou32(buf_cur);
	if(crc != reg) {
		ltfsmsg(LTFS_ERR, 39803E, (int)n, reg, crc);
		return -1;
	}
	ltfsmsg(LTFS_DEBUG, 39804D, "check", (int)n, crc);

	return n;
}
