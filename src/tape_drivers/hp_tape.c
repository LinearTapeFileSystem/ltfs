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
** FILE NAME:       tape_drivers/hp_tape.c
**
** DESCRIPTION:     General handling of HPE tape devices
**
** AUTHOR:          Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/
#ifndef mingw_PLATFORM
#if defined (__FreeBSD__) || defined(__NetBSD__)
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif /* __FreeBSD__ */
#include <netdb.h>
#include <ifaddrs.h>
#include <unistd.h>

#define LOOP_BACK_DEVICE "lo"
#endif

#include "tape_drivers/hp_tape.h"
#include "libltfs/ltfs_endian.h"

struct supported_device *hp_supported_drives[] = {
		TAPEDRIVE( HP_VENDOR_ID,  "Ultrium 5-SCSI",  DRIVE_LTO5,    "[Ultrium 5-SCSI]" ),  /* HP Ultrium Gen 5  */
		TAPEDRIVE( HP_VENDOR_ID,  "Ultrium 6-SCSI",  DRIVE_LTO6,    "[Ultrium 6-SCSI]" ),  /* HP Ultrium Gen 6  */
		TAPEDRIVE( HP_VENDOR_ID,  "Ultrium 7-SCSI",  DRIVE_LTO7,    "[Ultrium 7-SCSI]" ),  /* HP Ultrium Gen 7  */
		TAPEDRIVE( HPE_VENDOR_ID, "Ultrium 8-SCSI",  DRIVE_LTO8,    "[Ultrium 8-SCSI]" ),  /* HPE Ultrium Gen 8 */
		/* End of supported_devices */
		NULL
};

/* HP/HPE LTO tape drive vendor unique sense table */
struct error_table hp_tape_errors[] = {
	/* Sense Key 0 (No Sense) */
	{0x008282, -EDEV_CLEANING_REQUIRED,         "HPE LTO - Cleaning Required"},
	{0x008283, -EDEV_HARDWARE_ERROR,            "HPE LTO - Bad microcode detected"},
	/* END MARK*/
	{0xFFFFFF, -EDEV_UNKNOWN,                   "Unknown Error code"},
};

#define DEFAULT_TIMEOUT (60)

struct _timeout_tape{
	int  op_code;     /**< SCSI op code */
	int  timeout;     /**< SCSI timeout */
};

/* Base timeout value for LTO  */
static struct _timeout_tape timeout_lto[] = {
	{ CHANGE_DEFINITION,               -1    },
	{ XCOPY,                           -1    },
	{ INQUIRY,                         60    },
	{ LOG_SELECT,                      60    },
	{ LOG_SENSE,                       60    },
	{ MODE_SELECT6,                    60    },
	{ MODE_SELECT10,                   60    },
	{ MODE_SENSE6,                     60    },
	{ MODE_SENSE10,                    60    },
	{ PERSISTENT_RESERVE_IN,           60    },
	{ PERSISTENT_RESERVE_OUT,          60    },
	{ READ_ATTRIBUTE,                  60    },
	{ RECEIVE_DIAGNOSTIC_RESULTS,      60    },
	{ RELEASE_UNIT6,                   60    },
	{ RELEASE_UNIT10,                  60    },
	{ REPORT_LUNS,                     60    },
	{ REQUEST_SENSE,                   60    },
	{ RESERVE_UNIT6,                   60    },
	{ RESERVE_UNIT10,                  60    },
	{ SPIN,                            60    },
	{ SPOUT,                           60    },
	{ TEST_UNIT_READY,                 60    },
	{ WRITE_ATTRIBUTE,                 60    },
	{ ALLOW_OVERWRITE,                 60    },
	{ DISPLAY_MESSAGE,                 -1    },
	{ PREVENT_ALLOW_MEDIUM_REMOVAL,    60    },
	{ READ_BLOCK_LIMITS,               60    },
	{ READ_DYNAMIC_RUNTIME_ATTRIBUTE,  60    },
	{ READ_POSITION,                   60    },
	{ READ_REVERSE,                    -1    },
	{ RECOVER_BUFFERED_DATA,           -1    },
	{ REPORT_DENSITY_SUPPORT,          60    },
	{ STRING_SEARCH,                   -1    },
	{ WRITE_DYNAMIC_RUNTIME_ATTRIBUTE, 60    },
	{-1, -1}
};

static struct _timeout_tape timeout_lto5[] = {
	{ ERASE,                           18000 },
	{ FORMAT_MEDIUM,                   1560  },
	{ LOAD_UNLOAD,                     600   },
	{ LOCATE10,                        1200  },
	{ LOCATE16,                        1200  },
	{ READ,                            1200  },
	{ READ_BUFFER,                     60    },
	{ REWIND,                          600   },
	{ SEND_DIAGNOSTIC,                 600   },
	{ SET_CAPACITY,                    780   },
	{ SPACE6,                          1200  },
	{ SPACE16,                         1200  },
	{ VERIFY,                          18000 },
	{ WRITE,                           300   },
	{ WRITE_BUFFER,                    60    },
	{ WRITE_FILEMARKS6,                300   },
	{-1, -1}
};

static struct _timeout_tape timeout_lto6[] = {
	{ ERASE,                           18000 },
	{ FORMAT_MEDIUM,                   1560  },
	{ LOAD_UNLOAD,                     600   },
	{ LOCATE10,                        1200  },
	{ LOCATE16,                        1200  },
	{ READ,                            1200  },
	{ READ_BUFFER,                     60    },
	{ REWIND,                          600   },
	{ SEND_DIAGNOSTIC,                 600   },
	{ SET_CAPACITY,                    780   },
	{ SPACE6,                          1200  },
	{ SPACE16,                         1200  },
	{ VERIFY,                          18000 },
	{ WRITE,                           300   },
	{ WRITE_BUFFER,                    60    },
	{ WRITE_FILEMARKS6,                300   },
	{-1, -1}
};

static struct _timeout_tape timeout_lto7[] = {
	{ ERASE,                           29400 },
	{ FORMAT_MEDIUM,                   3000  },
	{ LOAD_UNLOAD,                     1020  },
	{ LOCATE10,                        2700  },
	{ LOCATE16,                        2700  },
	{ READ,                            1920  },
	{ READ_BUFFER,                     480   },
	{ REWIND,                          780   },
	{ SEND_DIAGNOSTIC,                 1980  },
	{ SET_CAPACITY,                    780   },
	{ SPACE6,                          2700  },
	{ SPACE16,                         2700  },
	{ VERIFY,                          28860 },
	{ WRITE,                           1920  },
	{ WRITE_BUFFER,                    540   },
	{ WRITE_FILEMARKS6,                1920  },
	{-1, -1}
};

static struct _timeout_tape timeout_lto8[] = {
	{ ERASE,                           53040 },
	{ FORMAT_MEDIUM,                   3000  },
	{ LOAD_UNLOAD,                     840   },
	{ LOCATE10,                        2940  },
	{ LOCATE16,                        2940  },
	{ READ,                            2340  },
	{ READ_BUFFER,                     480   },
	{ REWIND,                          660   },
	{ SEND_DIAGNOSTIC,                 1980  },
	{ SET_CAPACITY,                    780   },
	{ SPACE6,                          2940  },
	{ SPACE16,                         2940  },
	{ VERIFY,                          53040 },
	{ WRITE,                           1680  },
	{ WRITE_BUFFER,                    540   },
	{ WRITE_FILEMARKS6,                1680  },
	{-1, -1}
};

static struct _timeout_tape timeout_lto9[] = {
	{ ERASE,                           53040 },
	{ FORMAT_MEDIUM,                   3000  },
	{ LOAD_UNLOAD,                     840   },
	{ LOCATE10,                        2940  },
	{ LOCATE16,                        2940  },
	{ READ,                            2340  },
	{ READ_BUFFER,                     480   },
	{ REWIND,                          660   },
	{ SEND_DIAGNOSTIC,                 1980  },
	{ SET_CAPACITY,                    780   },
	{ SPACE6,                          2940  },
	{ SPACE16,                         2940  },
	{ VERIFY,                          53040 },
	{ WRITE,                           1680  },
	{ WRITE_BUFFER,                    540   },
	{ WRITE_FILEMARKS6,                1680  },
	{-1, -1}
};

static struct _timeout_tape timeout_lto10[] =  {
	{ ERASE,                           16320  },
	{ FORMAT_MEDIUM,                   3180   },
	{ LOAD_UNLOAD,                     780    },
	{ LOCATE10,                        2940   },
	{ LOCATE16,                        2940   },
	{ READ,                            2340   },
	{ READ_BUFFER,                     480    },
	{ REWIND,                          600    },
	{ SEND_DIAGNOSTIC,                 1980   },
	{ SET_CAPACITY,                    780    },
	{ SPACE6,                          2940   },
	{ SPACE16,                         2940   },
	{ VERIFY,                          104880 },
	{ WRITE,                           1500   },
	{ WRITE_BUFFER,                    540    },
	{ WRITE_FILEMARKS6,                1620   },
	{-1, -1}
};

static struct _timeout_tape timeout_lto5_hh[] = {
	{ ERASE,                           18000 },
	{ FORMAT_MEDIUM,                   1560  },
	{ LOAD_UNLOAD,                     600   },
	{ LOCATE10,                        1200  },
	{ LOCATE16,                        1200  },
	{ READ,                            1200  },
	{ READ_BUFFER,                     60    },
	{ REWIND,                          600   },
	{ SEND_DIAGNOSTIC,                 600   },
	{ SET_CAPACITY,                    780   },
	{ SPACE6,                          1200  },
	{ SPACE16,                         1200  },
	{ VERIFY,                          18000 },
	{ WRITE,                           300   },
	{ WRITE_BUFFER,                    60    },
	{ WRITE_FILEMARKS6,                300   },
	{-1, -1}
};

static struct _timeout_tape timeout_lto6_hh[] = {
	{ ERASE,                           18000 },
	{ FORMAT_MEDIUM,                   1560  },
	{ LOAD_UNLOAD,                     600   },
	{ LOCATE10,                        1200  },
	{ LOCATE16,                        1200  },
	{ READ,                            1200  },
	{ READ_BUFFER,                     60    },
	{ REWIND,                          600   },
	{ SEND_DIAGNOSTIC,                 600   },
	{ SET_CAPACITY,                    780   },
	{ SPACE6,                          1200  },
	{ SPACE16,                         1200  },
	{ VERIFY,                          18000 },
	{ WRITE,                           300   },
	{ WRITE_BUFFER,                    60    },
	{ WRITE_FILEMARKS6,                300   },
	{-1, -1}
};

static struct _timeout_tape timeout_lto7_hh[] = {
	{ ERASE,                           29400 },
	{ FORMAT_MEDIUM,                   3000  },
	{ LOAD_UNLOAD,                     1020  },
	{ LOCATE10,                        2700  },
	{ LOCATE16,                        2700  },
	{ READ,                            1920  },
	{ READ_BUFFER,                     480   },
	{ REWIND,                          780   },
	{ SEND_DIAGNOSTIC,                 1980  },
	{ SET_CAPACITY,                    780   },
	{ SPACE6,                          2700  },
	{ SPACE16,                         2700  },
	{ VERIFY,                          28860 },
	{ WRITE,                           1920  },
	{ WRITE_BUFFER,                    540   },
	{ WRITE_FILEMARKS6,                1920  },
	{-1, -1}
};

static struct _timeout_tape timeout_lto8_hh[] = {
	{ ERASE,                           53040 },
	{ FORMAT_MEDIUM,                   3000  },
	{ LOAD_UNLOAD,                     840   },
	{ LOCATE10,                        2940  },
	{ LOCATE16,                        2940  },
	{ READ,                            2340  },
	{ READ_BUFFER,                     480   },
	{ REWIND,                          660   },
	{ SEND_DIAGNOSTIC,                 1980  },
	{ SET_CAPACITY,                    780   },
	{ SPACE6,                          2940  },
	{ SPACE16,                         2940  },
	{ VERIFY,                          53040 },
	{ WRITE,                           1680  },
	{ WRITE_BUFFER,                    540   },
	{ WRITE_FILEMARKS6,                1680  },
	{-1, -1}
};

static struct _timeout_tape timeout_lto9_hh[] = {
	{ ERASE,                           53040 },
	{ FORMAT_MEDIUM,                   3000  },
	{ LOAD_UNLOAD,                     840   },
	{ LOCATE10,                        2940  },
	{ LOCATE16,                        2940  },
	{ READ,                            2340  },
	{ READ_BUFFER,                     480   },
	{ REWIND,                          660   },
	{ SEND_DIAGNOSTIC,                 1980  },
	{ SET_CAPACITY,                    780   },
	{ SPACE6,                          2940  },
	{ SPACE16,                         2940  },
	{ VERIFY,                          53040 },
	{ WRITE,                           1680  },
	{ WRITE_BUFFER,                    540   },
	{ WRITE_FILEMARKS6,                1680  },
	{-1, -1}
};

static struct _timeout_tape timeout_lto10_hh[] =  {
	{ ERASE,                           166370 },
	{ FORMAT_MEDIUM,                   3240   },
	{ LOAD_UNLOAD,                     960    },
	{ LOCATE10,                        3940   },
	{ LOCATE16,                        3940   },
	{ READ,                            2340   },
	{ READ_BUFFER,                     480    },
	{ REWIND,                          600    },
	{ SEND_DIAGNOSTIC,                 2040   },
	{ SET_CAPACITY,                    960    },
	{ SPACE6,                          3940   },
	{ SPACE16,                         3940   },
	{ VERIFY,                          63300  },
	{ WRITE,                           1560   },
	{ WRITE_BUFFER,                    540    },
	{ WRITE_FILEMARKS6,                1680   },
	{-1, -1}
};

static int _create_table_tape(struct timeout_tape **result,
							  struct _timeout_tape* base,
							  struct _timeout_tape* override)
{
	struct _timeout_tape* cur;
	struct timeout_tape* entry;
	struct timeout_tape *out = NULL;

	entry = malloc(sizeof(struct timeout_tape));
	entry->op_code  = override->op_code;
	entry->timeout = override->timeout;
	HASH_ADD_INT(*result, op_code, entry);
	if (! *result) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return -LTFS_NO_MEMORY;
	}

	for ( cur = override; cur->op_code != -1; ++cur) {
		entry = malloc(sizeof(struct timeout_tape));
		entry->op_code  = cur->op_code;
		entry->timeout = cur->timeout;
		HASH_ADD_INT(*result, op_code, entry);
	}

	for ( cur = base; cur->op_code != -1; ++cur) {
		out = NULL;
		HASH_FIND_INT(*result, &cur->op_code, out);
		if (!out) {
			entry = malloc(sizeof(struct timeout_tape));
			entry->op_code  = cur->op_code;
			entry->timeout = cur->timeout;
			HASH_ADD_INT(*result, op_code, entry);
		}
	}

	return 0;
}

int hp_tape_init_timeout(struct timeout_tape** table, int type)
{
	int ret = 0;

	/* Clear the table if it is already created */
	HASH_CLEAR(hh, *table);

	switch (type) {
		case DRIVE_LTO5:
			ret = _create_table_tape(table, timeout_lto, timeout_lto5);
			break;
		case DRIVE_LTO5_HH:
			ret = _create_table_tape(table, timeout_lto, timeout_lto5_hh);
			break;
		case DRIVE_LTO6:
			ret = _create_table_tape(table, timeout_lto, timeout_lto6);
			break;
		case DRIVE_LTO6_HH:
			ret = _create_table_tape(table, timeout_lto, timeout_lto6_hh);
			break;
		case DRIVE_LTO7:
			ret = _create_table_tape(table, timeout_lto, timeout_lto7);
			break;
		case DRIVE_LTO7_HH:
			ret = _create_table_tape(table, timeout_lto, timeout_lto7_hh);
			break;
		case DRIVE_LTO8:
			ret = _create_table_tape(table, timeout_lto, timeout_lto8);
			break;
		case DRIVE_LTO8_HH:
			ret = _create_table_tape(table, timeout_lto, timeout_lto8_hh);
			break;
		case DRIVE_LTO9:
			ret = _create_table_tape(table, timeout_lto, timeout_lto9);
			break;
		case DRIVE_LTO9_HH:
			ret = _create_table_tape(table, timeout_lto, timeout_lto9_hh);
			break;
		case DRIVE_LTO10:
			ret = _create_table_tape(table, timeout_lto, timeout_lto10);
			break;
		case DRIVE_LTO10_HH:
			ret = _create_table_tape(table, timeout_lto, timeout_lto10_hh);
			break;
		default:
			ret = _create_table_tape(table, timeout_lto, timeout_lto7_hh);
			break;
	}

	if (ret) {
		HASH_CLEAR(hh, *table);
	}

	return ret;
}
