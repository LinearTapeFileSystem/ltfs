/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2022 IBM Corp. All rights reserved.
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
** FILE NAME:       ltfs.h
**
** DESCRIPTION:     Defines data structures used by the LTFS FUSE operations.
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

#ifndef __ltfs_fuse_h__
#define __ltfs_fuse_h__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef mingw_PLATFORM
#include "arch/win/win_util.h"
	typedef DWORD pid_t;   // Process ID
	typedef DWORD uid_t;   // User ID
	typedef DWORD gid_t;   // Group ID
#endif

#include "libltfs/ltfs_fuse_version.h"
//#include <fuse.h>
#include "libltfs/ltfs.h"
#include "libltfs/plugin.h"
#include "libltfs/uthash.h"
#include <sys/types.h> 
struct ltfs_fuse_data {
	bool first_parsing_pass;       /**< Just looking for a config file? If so, don't print help */

	struct statvfs fs_stats;       /**< Filesystem stats */

	pid_t pid_orig;                /**< Process ID of LTFS at launched (before background exec) */

	bool perm_override;            /**< Did the user ask for any permissions override? */
	uid_t mount_uid;               /**< Real UID of the mounting user */
	gid_t mount_gid;               /**< Real GID of the mounting user */
	mode_t file_mode;              /**< All files are assigned this mode, maybe minus write bits */
	mode_t dir_mode;               /**< All directories are assigned this mode */

	/* overrides for the permission setup */
	char *force_uid;               /**< Override for the uid */
	char *force_gid;               /**< Override for the gid */
	char *force_umask;             /**< Override for the umask */
	char *force_fmask;             /**< Override for the file umask */
	char *force_dmask;             /**< Override for the directory umask */

	char *sync_type_str;           /**< Sync type fetched by option (time, close or none)*/
	ltfs_sync_type_t sync_type;    /**< Sync type (time, close or none)*/
	long sync_time;                /**< Sync time*/

	bool snmp_enabled;             /**< Indicates if the snmp service is enabled */
	char *snmp_deffile;            /**< SNMP definition file */

	const char *devname;              /**< Device where tape resides */
	const char *tape_backend_name;    /**< Name of tape backend library or path to library */
	const char *iosched_backend_name; /**< Name or path to the I/O scheduler backend library */
	const char *dcache_backend_name;  /**< Name or path to the dentry cache library */
	const char *kmi_backend_name;     /**< Name or path to the key manager interface backend library */

	const char *config_file;       /**< Path to ltfs.conf */
	const char *work_directory;    /**< The directory where LTFS will put its temporary files */

	char *force_min_pool;          /**< Override for min pool size */
	char *force_max_pool;          /**< Override for the max pool size */
	size_t min_pool_size;          /**< Minimum write cache pool size in MiB */
	size_t max_pool_size;          /**< Maximum write cache pool size in MiB */
	char *index_rules;             /**< Index rules (overrides the ones specified at format time) */

	struct ltfs_volume *data;            /**< LTFS data */

	struct config_file *config;            /**< Plugin configuration data */
	struct libltfs_plugin tape_plugin;     /**< Tape drive plugin */
	struct libltfs_plugin iosched_plugin;  /**< I/O scheduler plugin */
	struct libltfs_plugin dcache_plugin;   /**< Dentry cache plugin */
	struct libltfs_plugin kmi_plugin;      /**< Key manager interface plugin */

	int atime;                     /**< Update the XML schema on access */
	int verbose;                   /**< Logging level (1=quiet, 2=normal, 3=trace) */
	int eject;                     /**< Eject cartridge after unmount? */
	int skip_eod_check;            /**< Skip EOD check? */
	int device_list;               /**< List available tape devices */
	char *rollback_str;            /**< Target generation to roll back mount (string) */
	unsigned int rollback_gen;     /**< Target generation to roll back mount */
	int release_device;            /**< Release device? */
	int allow_other;               /**< Allow all users to access the volume? */
	char *capture_dir;             /**< Directory to capture index information  */
	char *symlink_str;             /**< Symbolic Link type fetched by option (live or posix)*/
	char *str_append_only_mode;    /**< option sting of scsi_append_only_mode */
	int append_only_mode;          /**< Use append-only mode */

	bool advanced_help;            /**< Include standard FUSE options on --help? */

	ltfs_mutex_t file_table_lock; /**< Controls access to 'open_files' */
	struct file_info *file_table;    /**< Hash table of open file handles */
};

#ifdef __cplusplus
}
#endif

/**
 * This structure holds information about individual file handles.
 * There is one file_handle structure for each file handle (note that
 * a file may have many open handles).
 */
struct ltfs_file_handle {
	struct file_info *file_info; /**< open_file data associated with this file handle */
	bool dirty;                  /**< True if this handle has been written but not synced */
	ltfs_mutex_t lock;
};

/**
 * Holds information about open files.
 * There is one file_info structure for each file with open handles.
 */
struct file_info {
	char *path;           /**< Path originally used to open this file */
	void *dentry_handle;  /**< File data */
	bool write_index;     /**< True if an index should be written once this file is closed */
	uint32_t open_count;  /**< Reference counter */
	ltfs_mutex_t lock;
	UT_hash_handle hh;
};

/**
 * Request type definisions for LTFS request trace
 */
#define REQ_MOUNT       0000
#define REQ_UNMOUNT     0001
#define REQ_GETATTR     0002
#define REQ_FGETATTR    0003
#define REQ_ACCESS      0004
#define REQ_STATFS      0005
#define REQ_OPEN        0006
#define REQ_RELEASE     0007
#define REQ_FSYNC       0008
#define REQ_FLUSH       0009
#define REQ_UTIMENS     000a
#define REQ_CHMOD       000b
#define REQ_CHOWN       000c
#define REQ_CREATE      000d
#define REQ_TRUNCATE    000e
#define REQ_FTRUNCATE   000f
#define REQ_UNLINK      0010
#define REQ_RENAME      0011
#define REQ_MKDIR       0012
#define REQ_RMDIR       0013
#define REQ_OPENDIR     0014
#define REQ_READDIR     0015
#define REQ_RELEASEDIR  0016
#define REQ_FSYNCDIR    0017
#define REQ_WRITE       0018
#define REQ_READ        0019
#define REQ_SETXATTR    001a
#define REQ_GETXATTR    001b
#define REQ_LISTXATTR   001c
#define REQ_REMOVEXATTR 001d
#define REQ_SYMLINK     001e
#define REQ_READLINK    001f
/* Following definition is reserved.
 * Actually used in libltfs for handling periodic sync
#define REQ_SYNC        fffe
*/

#endif /* __ltfs_fuse_h__ */
