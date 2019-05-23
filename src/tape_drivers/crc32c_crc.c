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
** FILE NAME:       crc32c_crc.c
**
** DESCRIPTION:     Implements CRC32C CRC usd by LTO based drives
**                  The algorithm is defined by the equation
**                  x32+x28+x27+x26+x25+x23+x23+x22++x20+x19+x18+x14+x13+x11+x10+x9+x8+x6+1
**                  polynomial 0x11EDC6F41
**                  refer http://www.intel.com/content/www/us/en/processors/architectures-software-developer-manuals.html
**                  document is http://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-manual-325462.pdf
**
** AUTHORS:         Mitsuhiro Nishida
**                  IBM Tokyo Lab., Japan
**                  mini@jp.ibm.com
**
**                  Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/

#define __crc32c_crc_c

#include <inttypes.h>
#include <sys/types.h>
#include <string.h>

#if defined(__SSE42__) && (defined(__i386__) || defined(__x86_64__))
#include <cpuid.h>
#include <nmmintrin.h>
#ifdef __i386__
#define CALC_SIZE (4)
#else
#define CALC_SIZE (8)
#endif
#endif

#include "libltfs/ltfslogging.h"


static bool is_sse4_2_supported(void)
{
#if defined(__SSE42__) && (defined(__i386__) || defined(__x86_64__))
	unsigned int eax, ebx, ecx, edx;
#ifdef __APPLE__
	__get_cpuid(1, &eax, &ebx, &ecx, &edx);
#else
	__cpuid(1, eax, ebx, ecx, edx);
#endif
	return ecx & 0x00080000; // SSE4.2
#else
	return false;
#endif
}

static const uint32_t crc32c_table[256] =
{
	0x00000000,0xF26B8303,0xE13B70F7,0x1350F3F4,0xC79A971F,0x35F1141C,
	0x26A1E7E8,0xD4CA64EB,0x8AD958CF,0x78B2DBCC,0x6BE22838,0x9989AB3B,
	0x4D43CFD0,0xBF284CD3,0xAC78BF27,0x5E133C24,0x105EC76F,0xE235446C,
	0xF165B798,0x030E349B,0xD7C45070,0x25AFD373,0x36FF2087,0xC494A384,
	0x9A879FA0,0x68EC1CA3,0x7BBCEF57,0x89D76C54,0x5D1D08BF,0xAF768BBC,
	0xBC267848,0x4E4DFB4B,0x20BD8EDE,0xD2D60DDD,0xC186FE29,0x33ED7D2A,
	0xE72719C1,0x154C9AC2,0x061C6936,0xF477EA35,0xAA64D611,0x580F5512,
	0x4B5FA6E6,0xB93425E5,0x6DFE410E,0x9F95C20D,0x8CC531F9,0x7EAEB2FA,
	0x30E349B1,0xC288CAB2,0xD1D83946,0x23B3BA45,0xF779DEAE,0x05125DAD,
	0x1642AE59,0xE4292D5A,0xBA3A117E,0x4851927D,0x5B016189,0xA96AE28A,
	0x7DA08661,0x8FCB0562,0x9C9BF696,0x6EF07595,0x417B1DBC,0xB3109EBF,
	0xA0406D4B,0x522BEE48,0x86E18AA3,0x748A09A0,0x67DAFA54,0x95B17957,
	0xCBA24573,0x39C9C670,0x2A993584,0xD8F2B687,0x0C38D26C,0xFE53516F,
	0xED03A29B,0x1F682198,0x5125DAD3,0xA34E59D0,0xB01EAA24,0x42752927,
	0x96BF4DCC,0x64D4CECF,0x77843D3B,0x85EFBE38,0xDBFC821C,0x2997011F,
	0x3AC7F2EB,0xC8AC71E8,0x1C661503,0xEE0D9600,0xFD5D65F4,0x0F36E6F7,
	0x61C69362,0x93AD1061,0x80FDE395,0x72966096,0xA65C047D,0x5437877E,
	0x4767748A,0xB50CF789,0xEB1FCBAD,0x197448AE,0x0A24BB5A,0xF84F3859,
	0x2C855CB2,0xDEEEDFB1,0xCDBE2C45,0x3FD5AF46,0x7198540D,0x83F3D70E,
	0x90A324FA,0x62C8A7F9,0xB602C312,0x44694011,0x5739B3E5,0xA55230E6,
	0xFB410CC2,0x092A8FC1,0x1A7A7C35,0xE811FF36,0x3CDB9BDD,0xCEB018DE,
	0xDDE0EB2A,0x2F8B6829,0x82F63B78,0x709DB87B,0x63CD4B8F,0x91A6C88C,
	0x456CAC67,0xB7072F64,0xA457DC90,0x563C5F93,0x082F63B7,0xFA44E0B4,
	0xE9141340,0x1B7F9043,0xCFB5F4A8,0x3DDE77AB,0x2E8E845F,0xDCE5075C,
	0x92A8FC17,0x60C37F14,0x73938CE0,0x81F80FE3,0x55326B08,0xA759E80B,
	0xB4091BFF,0x466298FC,0x1871A4D8,0xEA1A27DB,0xF94AD42F,0x0B21572C,
	0xDFEB33C7,0x2D80B0C4,0x3ED04330,0xCCBBC033,0xA24BB5A6,0x502036A5,
	0x4370C551,0xB11B4652,0x65D122B9,0x97BAA1BA,0x84EA524E,0x7681D14D,
	0x2892ED69,0xDAF96E6A,0xC9A99D9E,0x3BC21E9D,0xEF087A76,0x1D63F975,
	0x0E330A81,0xFC588982,0xB21572C9,0x407EF1CA,0x532E023E,0xA145813D,
	0x758FE5D6,0x87E466D5,0x94B49521,0x66DF1622,0x38CC2A06,0xCAA7A905,
	0xD9F75AF1,0x2B9CD9F2,0xFF56BD19,0x0D3D3E1A,0x1E6DCDEE,0xEC064EED,
	0xC38D26C4,0x31E6A5C7,0x22B65633,0xD0DDD530,0x0417B1DB,0xF67C32D8,
	0xE52CC12C,0x1747422F,0x49547E0B,0xBB3FFD08,0xA86F0EFC,0x5A048DFF,
	0x8ECEE914,0x7CA56A17,0x6FF599E3,0x9D9E1AE0,0xD3D3E1AB,0x21B862A8,
	0x32E8915C,0xC083125F,0x144976B4,0xE622F5B7,0xF5720643,0x07198540,
	0x590AB964,0xAB613A67,0xB831C993,0x4A5A4A90,0x9E902E7B,0x6CFBAD78,
	0x7FAB5E8C,0x8DC0DD8F,0xE330A81A,0x115B2B19,0x020BD8ED,0xF0605BEE,
	0x24AA3F05,0xD6C1BC06,0xC5914FF2,0x37FACCF1,0x69E9F0D5,0x9B8273D6,
	0x88D28022,0x7AB90321,0xAE7367CA,0x5C18E4C9,0x4F48173D,0xBD23943E,
	0xF36E6F75,0x0105EC76,0x12551F82,0xE03E9C81,0x34F4F86A,0xC69F7B69,
	0xD5CF889D,0x27A40B9E,0x79B737BA,0x8BDCB4B9,0x988C474D,0x6AE7C44E,
	0xBE2DA0A5,0x4C4623A6,0x5F16D052,0xAD7D5351
};


static inline void crc32c_calc(const unsigned char in, uint32_t *reg)
{
	*reg = (*reg >> 8) ^ crc32c_table[in ^ (*reg & 0xff)];
}

static uint32_t memcpy_crc32c(void *dest, const void *src, size_t n)
{
	unsigned char *dest_cur = (unsigned char *)dest, *src_cur = (unsigned char *)src;
	unsigned int reg = 0xffffffff;
	size_t i;

	if (is_sse4_2_supported()) {
#if defined(__SSE42__) && (defined(__i386__) || defined(__x86_64__))
#ifdef __i386__
		unsigned int reg_native;
#else
		unsigned long long reg_native;
#endif

		memcpy(dest_cur, src_cur, n);
		i = 0;

		for(i = 0; (i < n) && ((size_t)src_cur % CALC_SIZE > 0); i++){
			/* Calculate CRC */
			reg = _mm_crc32_u8(reg, *src_cur);

			/* Increment pointers */
			src_cur++;
		}

		reg_native = reg;

		for(; i + CALC_SIZE - 1 < n; i+=CALC_SIZE){
			/* Calculate CRC */
#ifdef __i386__
			reg_native = _mm_crc32_u32(reg_native, *(unsigned int *)src_cur);
#else
			reg_native = _mm_crc32_u64(reg_native, *(unsigned long long *)src_cur);
#endif

			/* Increment pointers */
			src_cur += CALC_SIZE;
		}

		reg = reg_native;

		for(; i < n; i++){
			/* Calculate CRC */
			reg = _mm_crc32_u8(reg, *src_cur);

			/* Increment pointers */
			src_cur++;
		}
#endif
	} else {
		for(i = 0; i < n; i++){
			/* Inject CRC value to the end of the destination buffer */
			*dest_cur = *src_cur;

			/* Calculate CRC */
			crc32c_calc(*src_cur, &reg);

			/* Increment pointers */
			dest_cur++;
			src_cur++;
		}
	}

	return ~reg;
}

static uint32_t crc32c(void *buf, size_t n)
{
	unsigned char *buf_cur = (unsigned char *)buf;
	unsigned int reg = 0xffffffff;
	size_t i;

	if (is_sse4_2_supported()) {
#if defined(__SSE42__) && (defined(__i386__) || defined(__x86_64__))
#ifdef __i386__
		unsigned int reg_native;
#else
		unsigned long long reg_native;
#endif

		for(i = 0; (i < n) && ((size_t)buf_cur % CALC_SIZE > 0); i++){
			/* Calculate CRC */
			reg = _mm_crc32_u8(reg, *buf_cur);
			buf_cur++;
		}

		reg_native = reg;

		for(; i + CALC_SIZE - 1 < n; i+=CALC_SIZE){
			/* Calculate CRC */
#ifdef __i386__
			reg_native = _mm_crc32_u32(reg_native, *(unsigned int *)buf_cur);
#else
			reg_native = _mm_crc32_u64(reg_native, *(unsigned long long *)buf_cur);
#endif
			buf_cur += CALC_SIZE;
		}

		reg = reg_native;

		for(; i < n; i++){
			/* Calculate CRC */
			reg = _mm_crc32_u8(reg, *buf_cur);
			buf_cur++;
		}
#endif
	} else {
		for(i = 0; i < n; i++){
			/* Calculate CRC */
			crc32c_calc(*buf_cur, &reg);
			buf_cur++;
		}
	}

	return ~reg;
}

void *memcpy_crc32c_enc(void *dest, const void *src, size_t n)
{
	unsigned char *dest_cur = (unsigned char *)dest + n;
	uint32_t reg;

	reg = memcpy_crc32c(dest, src, n);

	/* Inject CRC value to the end of the destination buffer */
	*dest_cur++ = reg & 0xff;
	*dest_cur++ = (reg >> 8) & 0xff;
	*dest_cur++ = (reg >> 16) & 0xff;
	*dest_cur++ = (reg >> 24) & 0xff;
	ltfsmsg(LTFS_DEBUG, 39804D, "encode", (int)n, reg);

	return dest;
}

void crc32c_enc(void *buf, size_t n)
{
	uint32_t reg;
	unsigned char *reg_cur = (unsigned char *)buf + n;

	reg = crc32c(buf, n);

	/* Inject CRC value to the end of the destination buffer */
	*reg_cur++ = reg & 0xff;
	*reg_cur++ = (reg >> 8) & 0xff;
	*reg_cur++ = (reg >> 16) & 0xff;
	*reg_cur++ = (reg >> 24) & 0xff;
	ltfsmsg(LTFS_DEBUG, 39804D, "encode", (int)n, reg);

	return;
}

int memcpy_crc32c_check(void *dest, const void *src, size_t n)
{
	unsigned char *src_cur = (unsigned char *)src + n;
	uint32_t reg, crc;

	reg = memcpy_crc32c(dest, src, n);

	crc = *src_cur++;
	crc |= *src_cur++ << 8;
	crc |= *src_cur++ << 16;
	crc |= *src_cur++ << 24;

	if (crc != reg) {
		ltfsmsg(LTFS_ERR, 39803E, (int)n, reg, crc);
		return -1;
	}
	ltfsmsg(LTFS_DEBUG, 39804D, "check", (int)n, crc);

	return n;
}

int crc32c_check(void *buf, size_t n)
{
	unsigned char *buf_cur = (unsigned char *)buf + n;
	uint32_t reg, crc;

	reg = crc32c(buf, n);

	crc = *buf_cur++;
	crc |= *buf_cur++ << 8;
	crc |= *buf_cur++ << 16;
	crc |= *buf_cur++ << 24;

	if (crc != reg) {
		ltfsmsg(LTFS_ERR, 39803E, (int)n, reg, crc);
		return -1;
	}
	ltfsmsg(LTFS_DEBUG, 39804D, "check", (int)n, crc);

	return n;
}
