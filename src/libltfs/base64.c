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
** FILE NAME:       base64.c
**
** DESCRIPTION:     RFC-4648 compliant base64 decoder.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
*************************************************************************************
*/

#include <string.h>
#include <limits.h>
#include "libltfs/ltfslogging.h"
#include "base64.h"

static const char *base64_enc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * A base64 decoder, RFC-4648 compliant except that it ignores whitespace.
 * Returns 0 if parsing or memory allocation fails.
 * Parsing may fail if:
 *   - Non-base64 characters (except '\r', '\n', '\t', ' ') are found in the string
 *   - An '=' is found out of place
 *   - The length, not counting whitespace, is not a multiple of 4
 *   - The length is 0
 * @param enc the encoded string
 * @param nbytes_in length of the encoded string
 * @param dec an output buffer, points to newly allocated memory on success
 * @return the number of bytes in the output buffer, or 0 on error
 */
size_t base64_decode(const unsigned char *enc, size_t nbytes_in, unsigned char **dec)
{
	unsigned char base64_dec[256];
	unsigned char cur_quad[4];
	size_t i, out_pos;
	size_t nbytes_real, nequal, nout;
	int quad_count;

	*dec = 0;

	/* check for empty input */
	if (nbytes_in == 0) {
		ltfsmsg(LTFS_ERR, 11111E);
		return 0;
	}

	/* prepare the decode map */
	memset(base64_dec,UCHAR_MAX,256);
	for (i=0; i<64; ++i)
		base64_dec[(size_t)base64_enc[i]] = i;

	/* check for out-of-place '=' and invalid characters */
	nequal = 0;
	nbytes_real = nbytes_in;
	for (i=0; i<nbytes_in; ++i) {
		if (i == nbytes_in-2 && enc[i] == '=' && enc[i+1] == '=') {
			nequal = 2;
		} else if (i == nbytes_in-1 && enc[i] == '=') {
			if (enc[i-1] != '=')
				nequal = 1;
		} else if (enc[i] == '\r' || enc[i] == '\n' || enc[i] == ' ' || enc[i] == '\t') {
			--nbytes_real;
		} else if (base64_dec[enc[i]] == UCHAR_MAX) {
			ltfsmsg(LTFS_ERR, 11112E);
			return 0;
		}
	}

	/* check for bad input length */
	if (nbytes_real % 4) {
		ltfsmsg(LTFS_ERR, 11113E);
		return 0;
	}

	/* allocate output buffer */
	nout = 3 * (nbytes_real / 4) - nequal;
	*dec = (unsigned char *)malloc(nout);
	if (!(*dec)) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return 0;
	}

	/* generate output */
	quad_count = 0;
	out_pos = 0;
	for (i=0; i<nbytes_in; ++i) {
		if (enc[i] == '\n' || enc[i] == '\r' || enc[i] == '\t' || enc[i] == ' ')
			continue;
		cur_quad[quad_count++] = base64_dec[enc[i]];
		if (quad_count == 4) {
			quad_count = 0;
			(*dec)[out_pos] = cur_quad[0] << 2;
			(*dec)[out_pos] |= (cur_quad[1] & 0x30) >> 4;
			if (cur_quad[2] != UCHAR_MAX) {
				(*dec)[out_pos+1] = (cur_quad[1] & 0xF) << 4;
				(*dec)[out_pos+1] |= (cur_quad[2] & 0x3C) >> 2;
				if (cur_quad[3] != UCHAR_MAX) {
					(*dec)[out_pos+2] = (cur_quad[2] & 0x3) << 6;
					(*dec)[out_pos+2] |= cur_quad[3];
				}
			}
			out_pos += 3;
		}
	}

	return nout;
}

