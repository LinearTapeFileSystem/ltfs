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
** FILE NAME:       tape_drivers/quantum_tape.c
**
** DESCRIPTION:     General handling of Quantum tape devices
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

#include "tape_drivers/quantum_tape.h"
#include "libltfs/ltfs_endian.h"

struct supported_device *quantum_supported_drives[] = {
	TAPEDRIVE( QUANTUM_VENDOR_ID, "ULTRIUM-HH5",  DRIVE_LTO5_HH, "[ULTRIUM-HH5]" ),  /* QUANTUM Ultrium Gen 5 Half-High */
	TAPEDRIVE( QUANTUM_VENDOR_ID, "ULTRIUM-HH6",  DRIVE_LTO6_HH, "[ULTRIUM-HH6]" ),  /* QUANTUM Ultrium Gen 6 Half-High */
	TAPEDRIVE( QUANTUM_VENDOR_ID, "ULTRIUM-HH7",  DRIVE_LTO7_HH, "[ULTRIUM-HH7]" ),  /* QUANTUM Ultrium Gen 7 Half-High */
	TAPEDRIVE( QUANTUM_VENDOR_ID, "ULTRIUM-HH8",  DRIVE_LTO8_HH, "[ULTRIUM-HH8]" ),  /* QUANTUM Ultrium Gen 8 Half-High */
	TAPEDRIVE( QUANTUM_VENDOR_ID, "ULTRIUM 5",    DRIVE_LTO5_HH, "[ULTRIUM-5]" ),    /* Another QUANTUM Ultrium Gen 5 Half-High */
	TAPEDRIVE( QUANTUM_VENDOR_ID, "ULTRIUM 6",    DRIVE_LTO6_HH, "[ULTRIUM-6]" ),    /* Another QUANTUM Ultrium Gen 6 Half-High */
	/* End of supported_devices */
	NULL
};

/* Quantum LTO tape drive vendor unique sense table */
struct error_table quantum_tape_errors[] = {
	/* Sense Key 0 (No Sense) */
	{0x008282, -EDEV_CLEANING_REQUIRED,         "QUANTUM LTO - Cleaning Required"},
	/* Sense Key 1 (Recoverd Error) */
	{0x018252, -EDEV_DEGRADED_MEDIA,            "QUANTUM LTO - Degraded Media"},
	{0x018383, -EDEV_RECOVERED_ERROR,           "Drive Has Been Cleaned"},
	{0x018500, -EDEV_RECOVERED_ERROR,           "Search Match List Limit (warning)"},
	{0x018501, -EDEV_RECOVERED_ERROR,           "Search Snoop Match Found"},
	/* Sense Key 3 (Medium Error) */
	{0x038500, -EDEV_DATA_PROTECT,              "Write Protected Because of Tape or Drive Failure"},
	{0x038501, -EDEV_DATA_PROTECT,              "Write Protected Because of Tape Failure"},
	{0x038502, -EDEV_DATA_PROTECT,              "Write Protected Because of Drive Failure"},
	/* Sense Key 5 (Illegal Request) */
	{0x058000, -EDEV_ILLEGAL_REQUEST,           "CU Mode, Vendor-Unique"},
	{0x058283, -EDEV_ILLEGAL_REQUEST,           "Bad Microcode Detected"},
	{0x058503, -EDEV_ILLEGAL_REQUEST,           "Write Protected Because of Current Tape Position"},
	{0x05A301, -EDEV_ILLEGAL_REQUEST,           "OEM Vendor-Specific"},
	/* Sense Key 6 (Unit Attention) */
	{0x065DFF, -EDEV_UNIT_ATTENTION,            "Failure Prediction False"},
	{0x068283, -EDEV_UNIT_ATTENTION,            "Drive Has Been Cleaned (older versions of microcode)"},
	{0x068500, -EDEV_UNIT_ATTENTION,            "Search Match List Limit (alert)"},
	/* Crypto Related Sense Code */
	{0x044780, -EDEV_HARDWARE_ERROR,            "QUANTUM LTO - Read Internal CRC Error"},
	{0x044781, -EDEV_HARDWARE_ERROR,            "QUANTUM LTO - Write Internal CRC Error"},
	/* END MARK*/
	{0xFFFFFF, -EDEV_UNKNOWN,                   "Unknown Error code"},
};

#define DEFAULT_TIMEOUT (60)

struct _timeout_tape{
	int  op_code;     /**< SCSI op code */
	int  timeout;     /**< SCSI timeout */
};

/* Base timeout value for LTO */
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

static struct _timeout_tape timeout_lto5_hh[] = {
	{ ERASE,                           19200 },
	{ FORMAT_MEDIUM,                   1980  },
	{ LOAD_UNLOAD,                     1020  },
	{ LOCATE10,                        2700  },
	{ LOCATE16,                        2700  },
	{ READ,                            1920  },
	{ READ_BUFFER,                     660   },
	{ REWIND,                          780   },
	{ SEND_DIAGNOSTIC,                 3120  },
	{ SET_CAPACITY,                    960   },
	{ SPACE6,                          2700  },
	{ SPACE16,                         2700  },
	{ VERIFY,                          19980 },
	{ WRITE,                           1920  },
	{ WRITE_BUFFER,                    720   },
	{ WRITE_FILEMARKS6,                1740  },
	{-1, -1}
};

static struct _timeout_tape timeout_lto6_hh[] = {
	{ ERASE,                           29400 },
	{ FORMAT_MEDIUM,                   3840  },
	{ LOAD_UNLOAD,                     1020  },
	{ LOCATE10,                        2700  },
	{ LOCATE16,                        2700  },
	{ READ,                            1920  },
	{ READ_BUFFER,                     660   },
	{ REWIND,                          780   },
	{ SEND_DIAGNOSTIC,                 3120  },
	{ SET_CAPACITY,                    960   },
	{ SPACE6,                          2700  },
	{ SPACE16,                         2700  },
	{ VERIFY,                          30000 },
	{ WRITE,                           1920  },
	{ WRITE_BUFFER,                    720   },
	{ WRITE_FILEMARKS6,                1740  },
	{-1, -1}
};

static struct _timeout_tape timeout_lto7_hh[] = {
	{ ERASE,                           27540 },
	{ FORMAT_MEDIUM,                   3240  },
	{ LOAD_UNLOAD,                     840   },
	{ LOCATE10,                        2940  },
	{ LOCATE16,                        2940  },
	{ READ,                            2340  },
	{ READ_BUFFER,                     480   },
	{ REWIND,                          660   },
	{ SEND_DIAGNOSTIC,                 2040  },
	{ SET_CAPACITY,                    960   },
	{ SPACE6,                          2940  },
	{ SPACE16,                         2940  },
	{ VERIFY,                          28860 },
	{ WRITE,                           1560  },
	{ WRITE_BUFFER,                    540   },
	{ WRITE_FILEMARKS6,                1680  },
	{-1, -1}
};

static struct _timeout_tape timeout_lto8_hh[] = {
	{ ERASE,                           46380 },
	{ FORMAT_MEDIUM,                   3240  },
	{ LOAD_UNLOAD,                     840   },
	{ LOCATE10,                        2940  },
	{ LOCATE16,                        2940  },
	{ READ,                            2340  },
	{ READ_BUFFER,                     480   },
	{ REWIND,                          660   },
	{ SEND_DIAGNOSTIC,                 2040  },
	{ SET_CAPACITY,                    960   },
	{ SPACE6,                          2940  },
	{ SPACE16,                         2940  },
	{ VERIFY,                          47700 },
	{ WRITE,                           1560  },
	{ WRITE_BUFFER,                    540   },
	{ WRITE_FILEMARKS6,                1680  },
	{-1, -1}
};

static struct _timeout_tape timeout_lto9_hh[] = {
	{ ERASE,                           46380 },
	{ FORMAT_MEDIUM,                   3240  },
	{ LOAD_UNLOAD,                     840   },
	{ LOCATE10,                        2940  },
	{ LOCATE16,                        2940  },
	{ READ,                            2340  },
	{ READ_BUFFER,                     480   },
	{ REWIND,                          660   },
	{ SEND_DIAGNOSTIC,                 2040  },
	{ SET_CAPACITY,                    960   },
	{ SPACE6,                          2940  },
	{ SPACE16,                         2940  },
	{ VERIFY,                          47700 },
	{ WRITE,                           1560  },
	{ WRITE_BUFFER,                    540   },
	{ WRITE_FILEMARKS6,                1680  },
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

int quantum_tape_init_timeout(struct timeout_tape** table, int type)
{
	int ret = 0;

	/* Clear the table if it is already created */
	HASH_CLEAR(hh, *table);

	switch (type) {
		case DRIVE_LTO5_HH:
			ret = _create_table_tape(table, timeout_lto, timeout_lto5_hh);
			break;
		case DRIVE_LTO6_HH:
			ret = _create_table_tape(table, timeout_lto, timeout_lto6_hh);
			break;
		case DRIVE_LTO7_HH:
			ret = _create_table_tape(table, timeout_lto, timeout_lto7_hh);
			break;
		case DRIVE_LTO8_HH:
			ret = _create_table_tape(table, timeout_lto, timeout_lto8_hh);
			break;
		case DRIVE_LTO9_HH:
			ret = _create_table_tape(table, timeout_lto, timeout_lto9_hh);
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
