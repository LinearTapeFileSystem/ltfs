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
** FILE NAME:       pathname.h
**
** DESCRIPTION:     Header file for Unicode text analysis and processing routines.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
*************************************************************************************
*/

/** \file
 * Functions for converting and manipulating file, directory, and extended attribute names.
 */

#ifndef __PATHNAME_H__
#define __PATHNAME_H__

#ifdef __cplusplus
extern "C" {
#endif


#ifdef mingw_PLATFORM
	#include <uchar.h>
#include <minwindef.h>
#else
	#ifdef __APPLE_MAKEFILE__
		#include <ICU/unicode/utypes.h>
	#else
		#include <unicode/utypes.h>
	#endif
#endif
#include <stdlib.h>
#include <stdbool.h>

#include <unicode/ustring.h>

int pathname_format(const char *name, char **new_name, bool validate, bool path);
int pathname_unformat(const char *name, char **new_name);
int pathname_caseless_match(const char *name1, const char *name2, int *result);
int pathname_prepare_caseless(const char *name, UChar **new_name, bool use_nfc);
int pathname_normalize(const char *name, char **new_name);
int pathname_validate_file(const char *name);
int pathname_validate_target(const char *name);
int pathname_validate_xattr_name(const char *name);
int pathname_validate_xattr_value(const char *name, size_t size);
int pathname_strlen(const char *name);
int pathname_truncate(char *name, size_t size);
int pathname_nfd_normalize(const char *name, char **new_name);
#ifdef __cplusplus
}
#endif

#endif /* __PATHNAME_H__ */
