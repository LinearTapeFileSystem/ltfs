/************************************************************************************
**
**  Hewlett Packard LTFS backend for LTO and DAT tape drives
**
** FILE:            ltotape_diag.c
**
** CONTENTS:        Definitions and further header files for LTO diagnostic routines
**
** (C) Copyright 2015, 2016 Hewlett Packard Enterprise Development LP
** (c) Copyright 2010, 2011 Quantum Corporation
**
** This program is free software; you can redistribute it and/or modify it
**  under the terms of version 2.1 of the GNU Lesser General Public License
**  as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful, but 
**  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
**  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
**  License for more details.
**
** You should have received a copy of the GNU General Public License along
**  with this program; if not, write to:
**    Free Software Foundation, Inc.
**    51 Franklin Street, Fifth Floor
**    Boston, MA 02110-1301, USA.
**
**   26 April 2010
**
*************************************************************************************
*/

#ifndef __ltotape_diag_h
#define __ltotape_diag_h

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "../../../libltfs/tape_ops.h"
#ifdef __APPLE__
#include "../../linux/ibmtape/IBM_tape.h"
#elif __NetBSD__
#include "../../freebsd/cam/IBM_tape.h"
#else
#include "../ibmtape/IBM_tape.h" /* for some common definitions */
#endif
#include "ltotape_timeout.h"

/*
 *  Definitions
 */
#ifdef HPE_BUILD
# define HPLTFS_COPYRIGHT   "Portions (C) Copyright 2015, 2016 Hewlett Packard Enterprise Development LP"
#elif defined QUANTUM_BUILD
# define QTMLTFS_COPYRIGHT   "Portions copyright (c) 2010-2011 Quantum Corporation"
#elif defined GENERIC_OEM_BUILD 
# define GENERICLTFS_COPYRIGHT   "Portions copyright (c) 2010-2011 Generic LTFS"
#endif

#define KB   (1024)
#define MB   (KB * 1024)
#define GB   (MB * 1024)

#ifdef __NetBSD__
#define SNAPSHOT_LENGTH        MAXPHYS  /* Max log size we'll handle                      */
#else
#define SNAPSHOT_LENGTH        (256 * KB)  /* Max log size we'll handle                      */
#endif
#define MAX_SNAPSHOT_RETRIES   10 /* wait up to 10s for the snapshot to become available -   */
                                  /*  see ltotape_read_snapshot() in ltotape_diag.c          */
#define MAX_RETAINED_SNAPSHOTS 10 /* Keep up to ten snapshots (older files will be deleted)  */

#define LINUX_LOGFILE_DIR      "/var/log"
#define MACOS_LOGFILE_DIR      "/var/tmp/ltfs"	/* This is used for dumping support tickets */

#define LTOTAPE_TIMESTAMP_TYPE_OFFSET    10
#define LTOTAPE_TIMESTAMP_OFFSET         12
#define LTOTAPE_LIBSN_OFFSET             52
#define LTOTAPE_LIBSN_LENGTH             32

/*
 * Function prototype for (public) function:
 */
int   ltotape_log_snapshot (void *device, int minidump);
char* ltotape_get_default_snapshotdir (void);
char* ltotape_set_snapshotdir (char* newdir);

#endif // __ltotape_diag_h
