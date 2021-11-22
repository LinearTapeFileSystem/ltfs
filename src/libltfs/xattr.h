/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2021 IBM Corp. All rights reserved.
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
** FILE NAME:       xattr.h
**
** DESCRIPTION:     Header for extended attribute routines.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
**                  Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
**                  Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/

#ifndef __xattr_h__
#define __xattr_h__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __FreeBSD__
#include "libltfs/arch/freebsd/xattr.h"
#endif

#include "ltfs.h"

#define LTFS_PRIVATE_PREFIX "ltfs."

int xattr_set(struct dentry *d, const char *name, const char *value, size_t size, int flags,
	struct ltfs_volume *vol);
int xattr_get(struct dentry *d, const char *name, char *value, size_t size,
	struct ltfs_volume *vol);
int xattr_list(struct dentry *d, char *list, size_t size, struct ltfs_volume *vol);
int xattr_remove(struct dentry *d, const char *name, struct ltfs_volume *vol);

/** For internal use only */
int xattr_do_set(struct dentry *d, const char *name, const char *value, size_t size,
	struct xattr_info *xattr);
int xattr_do_remove(struct dentry *d, const char *name, bool force, struct ltfs_volume *vol);
const char *xattr_strip_name(const char *name);
int xattr_set_mountpoint_length(struct dentry *d, const char* value, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* __xattr_h__ */
