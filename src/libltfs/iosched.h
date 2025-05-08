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
** FILE NAME:       iosched.h
**
** DESCRIPTION:     Header for the interface with the pluggable I/O schedulers.
**
** AUTHOR:          Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
*************************************************************************************
*/
#ifndef __iosched_h
#define __iosched_h

#ifdef __cplusplus
extern "C" {
#endif

#include "plugin.h"
#include "iosched_ops.h"

int iosched_init(struct libltfs_plugin *plugin, struct ltfs_volume *vol);
int iosched_destroy(struct ltfs_volume *vol);
int iosched_open(const char *path, bool open_write, struct dentry **dentry,
	struct ltfs_volume *vol);
int iosched_close(struct dentry *d, bool flush, struct ltfs_volume *vol);
bool iosched_initialized(struct ltfs_volume *vol);
ssize_t iosched_read(struct dentry *d, char *buf, size_t size, off_t offset,
		struct ltfs_volume *vol);
ssize_t iosched_write(struct dentry *d, const char *buf, size_t size, off_t offset,
	bool isupdatetime, struct ltfs_volume *vol);
int iosched_flush(struct dentry *d, bool closeflag, struct ltfs_volume *vol);
int iosched_truncate(struct dentry *d, off_t length, struct ltfs_volume *vol);
uint64_t iosched_get_filesize(struct dentry *d, struct ltfs_volume *vol);
int iosched_update_data_placement(struct dentry *d, struct ltfs_volume *vol);
int iosched_set_profiler(char *work_dir, bool enable, struct ltfs_volume *vol);

#ifdef __cplusplus
}
#endif


#endif /* __iosched_h */
