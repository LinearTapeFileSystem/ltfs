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
** FILE NAME:       kmi/key_format_ltfs.h
**
** DESCRIPTION:     Header file for the LTFS specific format manager.
**
** AUTHOR:          Yutaka Oishi
**                  IBM Yamato, Japan
**                  oishi@jp.ibm.com
**
*************************************************************************************
*/

#ifndef __key_format_ltfs_h
#define __key_format_ltfs_h

#define DK_LENGTH 32
#define DKI_LENGTH 12
#define DKI_ASCII_LENGTH 3
#define	SEPARATOR_LENGTH 1 /* length of ':' and '/' */

struct key {
	unsigned char dk[DK_LENGTH];   /**< Data Key */
	unsigned char dki[DKI_LENGTH]; /**< Data Key Identifier */
};

struct key_format_ltfs {
	int num_of_keys;                  /**< Number of DK and DKi pairs */
	struct key *dk_list;              /**< DK and DKi pairs' list */
};

void *key_format_ltfs_init(struct ltfs_volume *vol);
int key_format_ltfs_destroy(void * const kmi_handle);
int key_format_ltfs_get_key(unsigned char **keyalias, unsigned char **key, void * const kmi_handle,
	unsigned char * const dk_list, unsigned char * const dki_for_format);

#endif /* __key_format_ltfs_h */
