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
** FILE NAME:       libltfs/dcache.h
**
** DESCRIPTION:     Dentry Cache API
**
** AUTHOR:          Lucas C. Villa Real
**                  IBM Brazil Research Lab
**                  lucasvr@br.ibm.com
**
*************************************************************************************
*/
#ifndef __dcache_h
#define __dcache_h

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _XOPEN_SOURCE
#undef _XOPEN_SOURCE
#endif
#define _XOPEN_SOURCE 500

#ifdef mingw_PLATFORM
#include "arch/win/win_util.h"
#include <time.h>
#include <sys/utime.h>
#endif

#ifndef mingw_PLATFORM
#include <ftw.h>
#include <sys/time.h>
#include <utime.h>
#endif
#include <sys/types.h>



#include "plugin.h"
#include "dcache_ops.h"

/* Initialization, deinitialization and management */
int  dcache_init(struct libltfs_plugin *plugin, const struct dcache_options *options,
		struct ltfs_volume *vol);
int  dcache_destroy(struct ltfs_volume *vol);
int  dcache_parse_options(const char **options, struct dcache_options **out);
void dcache_free_options(struct dcache_options **options);
bool dcache_initialized(struct ltfs_volume *vol);
int  dcache_mkcache(const char *name, struct ltfs_volume *vol);
int  dcache_rmcache(const char *name, struct ltfs_volume *vol);
int  dcache_cache_exists(const char *name, bool *exists, struct ltfs_volume *vol);
int  dcache_set_workdir(const char *workdir, bool clean, struct ltfs_volume *vol);
int  dcache_get_workdir(char **workdir, struct ltfs_volume *vol);
int  dcache_assign_name(const char *name, struct ltfs_volume *vol);
int  dcache_unassign_name(struct ltfs_volume *vol);
int  dcache_wipe_dentry_tree(struct ltfs_volume *vol);
int  dcache_get_vol_uuid(const char *work_dir, const char *barcode, char **uuid, struct ltfs_volume *vol);
int  dcache_set_vol_uuid(char *uuid, struct ltfs_volume *vol);
int  dcache_get_generation(const char *work_dir, const char *barcode, unsigned int *gen, struct ltfs_volume *vol);
int  dcache_set_generation(unsigned int gen, struct ltfs_volume *vol);
int  dcache_get_dirty(const char *work_dir, const char *barcode, bool *dirty, struct ltfs_volume *vol);
int  dcache_set_dirty(bool dirty, struct ltfs_volume *vol);

/* Disk image management */
int dcache_diskimage_create(struct ltfs_volume *vol);
int dcache_diskimage_remove(struct ltfs_volume *vol);
int dcache_diskimage_mount(struct ltfs_volume *vol);
int dcache_diskimage_unmount(struct ltfs_volume *vol);
bool dcache_diskimage_is_full(struct ltfs_volume *vol);

/* Advisory lock operations */
int dcache_get_advisory_lock(const char *name, struct ltfs_volume *vol);
int dcache_put_advisory_lock(const char *name, struct ltfs_volume *vol);

/* File system operations */
int  dcache_open(const char *path, struct dentry **d, struct ltfs_volume *vol);
int  dcache_openat(const char *parent_path, struct dentry *parent, const char *name,
		struct dentry **result, struct ltfs_volume *vol);
int  dcache_close(struct dentry *d, bool lock_meta, bool descend, struct ltfs_volume *vol);
int  dcache_create(const char *path, struct dentry *d, struct ltfs_volume *vol);
int  dcache_unlink(const char *path, struct dentry *d, struct ltfs_volume *vol);
int  dcache_rename(const char *oldpath, const char *newpath, struct dentry **old_dentry,
		struct ltfs_volume *vol);
int  dcache_flush(struct dentry *d, enum dcache_flush_flags flags, struct ltfs_volume *vol);
int  dcache_readdir(struct dentry *d, bool dentries, void ***result, struct ltfs_volume *vol);
int dcache_read_direntry(struct dentry *d, struct ltfs_direntry *dirent, unsigned long index,  struct ltfs_volume *vol);
int  dcache_setxattr(const char *path, struct dentry *d, const char *xattr, const char *value,
		size_t size, int flags, struct ltfs_volume *vol);
int  dcache_removexattr(const char *path, struct dentry *d, const char *xattr,
		struct ltfs_volume *vol);
int  dcache_listxattr(const char *path, struct dentry *d, char *list, size_t size,
		struct ltfs_volume *vol);
int  dcache_getxattr(const char *path, struct dentry *d, const char *name,
		void *value, size_t size, struct ltfs_volume *vol);

/* Helper operations */
int  dcache_get_dentry(struct dentry *d, struct ltfs_volume *vol);
int  dcache_put_dentry(struct dentry *d, struct ltfs_volume *vol);

#ifdef __cplusplus
}
#endif


#endif /* __dcache_h */
