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
** FILE NAME:       ltfs_types.h
**
** DESCRIPTION:     Type declarations used by several modules.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
*************************************************************************************
*/

#ifndef __ltfs_types_h__
#define __ltfs_types_h__

#include "arch/time_internal.h"

typedef uint32_t tape_partition_t;
typedef uint64_t tape_block_t;

struct tc_coherency {
	uint64_t      volume_change_ref; /**< VWJ from the drive */
	uint64_t      count;             /**< Generation of Index */
	uint64_t      set_id;            /**< Position of Index   */
	char          uuid[37];          /**< Volume UUID */
	unsigned char version;           /**< Version field */
};

/* Structure of cartridge health */
#define UNSUPPORTED_CARTRIDGE_HEALTH ((int64_t)(-1))

typedef struct tc_cartridge_health {
	int64_t  mounts;           /* Total number of mount in the volume lifetime */
	uint64_t written_ds;       /* Total number of data sets written in the volume lifetime */
	int64_t  write_temps;      /* Total number of recoverd write error in the volume lifetime */
	int64_t  write_perms;      /* Total number of unrecoverd write error in the volume lifetime */
	uint64_t read_ds;          /* Total number of data sets read in the volume lifetime */
	int64_t  read_temps;       /* Total number of recoverd read error in the volume lifetime */
	int64_t  read_perms;       /* Total number of unrecoverd read error in the volume lifetime */
	int64_t  write_perms_prev; /* Unrecoverd write errors in previous mount */
	int64_t  read_perms_prev;  /* Unrecoverd read errors in previous mount */
	uint64_t written_mbytes;   /* Total number of mega bytes written in the volume lifetime */
	uint64_t read_mbytes;      /* Total number of mega bytes read in the volume lifetime */
	int64_t  passes_begin;     /* Count of the total number of times the beginning of medium position has passed */
	int64_t  passes_middle;    /* Count of the total number of times the middle of medium position has passed */
	int64_t  tape_efficiency;  /* Tape efficiency (0-255) */
} cartridge_health_info;

struct dentry_attr {
	uint64_t size;
	uint64_t alloc_size;
	uint64_t blocksize;
	uint64_t uid;
	uint32_t nlink;
	struct ltfs_timespec create_time;
	struct ltfs_timespec access_time;
	struct ltfs_timespec modify_time;
	struct ltfs_timespec change_time;
	struct ltfs_timespec backup_time;
	bool readonly;
	bool isdir;
	bool isslink;
};

#endif /* __ltfs_types_h__ */
