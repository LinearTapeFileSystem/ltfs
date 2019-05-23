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
*/

/*************************************************************************************
 ** FILE NAME:       ltfs_endian.h
 **
 ** DESCRIPTION:     Implements macros for endian conversions.
 **
 ** AUTHORS:         Brian Biskeborn
 **                  IBM Almaden Research Center
 **                  bbiskebo@us.ibm.com
 **
 *************************************************************************************
 */

#ifndef __LTFS_ENDIAN_H__
#define __LTFS_ENDIAN_H__

/* TODO: verify that this is correct for mingw */
#ifdef mingw_PLATFORM
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif /* mingw_PLATFORM */

/**
 * Convert a uint64_t value (src) to big endian and store it in the 8-byte buffer
 * pointed to by dest.
 */
#define ltfs_u64tobe(dest, src) \
	do { \
		uint32_t *tmp = (uint32_t *)(dest); \
		uint64_t stmp = (src); \
		tmp[0] = htonl((stmp >> 32) & 0xffffffff); \
		tmp[1] = htonl(stmp & 0xffffffff); \
	} while (0)

/**
 * Convert a uint32_t value (src) to big endian and store it in the 4-byte buffer
 * pointed to by dest.
 */
#define ltfs_u32tobe(dest, src) \
	do { \
		*((uint32_t *)(dest)) = htonl((src)); \
	} while (0)

/**
 * Convert a uint16_t value (src) to big endian and store it in the 2-byte buffer
 * pointed to by dest.
 */
#define ltfs_u16tobe(dest, src) \
	do { \
		*((uint16_t *)(dest)) = htons((src)); \
	} while (0)

/**
 * Convert a big endian 64-bit unsigned integer (pointed to by buf)
 * to a uint64_t in local byte order.
 */
#define ltfs_betou64(buf) \
	(((uint64_t)ntohl(*((uint32_t *)(buf)))) << 32) + (uint64_t)ntohl(*(((uint32_t *)(buf))+1))

/**
 * Convert a big endian 32-bit unsigned integer (pointed to by buf)
 * to a uint32_t in local byte order.
 */
#define ltfs_betou32(buf) ntohl(*((uint32_t *)(buf)))

/**
 * Convert a big endian 16-bit unsigned integer (pointed to by buf)
 * to a uint16_t in local byte order.
 */
#define ltfs_betou16(buf) ntohs(*((uint16_t *)(buf)))

#endif /* __LTFS_ENDIAN_H__ */

