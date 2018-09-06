/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2018 IBM Corp. All rights reserved.
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
** FILE NAME:       tape_ops.h
**
** DESCRIPTION:     Definitions for the LTFS tape drive backend interface.
**
** AUTHOR:          Atsushi Abe
**                  IBM Yamato, Japan
**                  PISTE@jp.ibm.com
**
*************************************************************************************
*/

/**
 * @file
 * Definitions for the LTFS tape drive backend interface.
 *
 * This file defines the interface which must be implemented by tape device backend plugins
 * for libltfs. The primary interface is the set of functions specified in struct tape_ops below.
 * To be used with libltfs, the backend library must implement all functions in that structure,
 * as well as the tape_dev_get_ops() and tape_dev_get_message_bundle_name() function.
 */

#ifndef __tape_ops_h
#define __tape_ops_h

#include <stdint.h>
#include <stdbool.h>
#include "ltfs_types.h"

#define VENDOR_ID_LENGTH           (8)
#define PRODUCT_ID_LENGTH          (16)
#define PRODUCT_REV_LENGTH         (4)
#define PRODUCT_NAME_LENGTH        (PRODUCT_ID_LENGTH + 3) /* " [PRODUCT_ID]" */
#define PRODUCT_NAME_REPORT_LENGTH (15)

#define UNIT_SERIAL_LENGTH         (255)

#define TAPE_MODEL_NAME_LEN_MAX     (16)
#define TAPE_VENDOR_NAME_LEN_MAX    (8)
#define TAPE_REVISION_CODE_LEN_MAX  (4)
#define TAPE_VENDORUNQ_DATA_LEN_MAX (20)
#define TAPE_DEVNAME_LEN_MAX        (1023)
#define TAPE_SERIAL_LEN_MAX         (32)

struct tc_drive_info {
	char name[TAPE_DEVNAME_LEN_MAX + 1];           /* Device name like "/dev/IBMtape0" */
	char vendor[TAPE_VENDOR_NAME_LEN_MAX + 1];     /* Vendor code "IBM" */
	char model[TAPE_MODEL_NAME_LEN_MAX + 1];       /* Device identifier */
	char serial_number[TAPE_SERIAL_LEN_MAX + 1];   /* Serial number of the drvice */
	char product_name[PRODUCT_NAME_LENGTH + 1];    /* Product name like " [ULTRIUM-TD5]" */
};

typedef uint64_t tape_filemarks_t;

typedef struct tc_position {
	tape_block_t     block;
	tape_filemarks_t filemarks;
	tape_partition_t partition;
	bool early_warning;
	bool programmable_early_warning;
} tape_position;

#define TAPE_BLOCK_MAX (0xFFFFFFFFFFFFFFFFLL)

struct tc_inq {
	unsigned int  devicetype;
	bool          cmdque;
	unsigned char vid[8 + 1];
	unsigned char pid[16 + 1];
	unsigned char revision[4 + 1];
	unsigned char vendor[20 + 1];
};

struct tc_inq_page {
	unsigned char page_code;
	unsigned char data[255];
};

#define TC_INQ_PAGE_DRVSERIAL (0x80)

struct tc_current_param {
	/* Parameters for tape drive */
	unsigned int  max_blksize;           /* Maximum block size */

	/* Parameters for current loaded tape */
	unsigned char cart_type;             /* Cartridge type in CM like TC_MP_JB */
	unsigned char density;               /* Current density code */
	unsigned int  write_protected;       /* Write protect status of the tape (use bit field of volumelock_status) */
	/* TODO: Following field shall be handled by backend but currently they are not implemented yet */
	//bool          is_encrypted;          /* Is encrypted tape ? */
	//bool          is_worm;               /* Is worm tape? */
};

/* Changed to uint64_t while fixing cp problems on OS X. This appears to make a difference for unknown reasons. */
struct tc_remaining_cap {
	uint64_t remaining_p0;   /* Remaining capacity of partition 0 */
	uint64_t remaining_p1;   /* Remaining capacity of partition 1 */
	uint64_t max_p0;         /* Maxmum capacity of partition 0 */
	uint64_t max_p1;         /* Maxmum capacity of partition 1 */
};

/* Density codes */
enum {
	TC_DC_UNKNOWN = 0x00,
	TC_DC_LTO1    = 0x40,
	TC_DC_LTO2    = 0x42,
	TC_DC_LTO3    = 0x44,
	TC_DC_LTO4    = 0x46,
	TC_DC_LTO5    = 0x58,
	TC_DC_LTO6    = 0x5A,
	TC_DC_LTO7    = 0x5C,
	TC_DC_LTOM8   = 0x5D,
	TC_DC_LTO8    = 0x5E,
	TC_DC_JAG1    = 0x51,
	TC_DC_JAG2    = 0x52,
	TC_DC_JAG3    = 0x53,
	TC_DC_JAG4    = 0x54,
	TC_DC_JAG5    = 0x55,
	TC_DC_JAG5A   = 0x56,
	TC_DC_JAG1E   = 0x71,
	TC_DC_JAG2E   = 0x72,
	TC_DC_JAG3E   = 0x73,
	TC_DC_JAG4E   = 0x74,
	TC_DC_JAG5E   = 0x75,
	TC_DC_JAG5AE  = 0x76,
};

#define TEST_CRYPTO (0x20)
#define MASK_CRYPTO (~0x20)

typedef enum {
	TC_SPACE_EOD,   /* Space EOD          */
	TC_SPACE_FM_F,  /* Space FM Forward   */
	TC_SPACE_FM_B,  /* Space FM Backword  */
	TC_SPACE_F,     /* Space Rec Forward  */
	TC_SPACE_B,     /* Space Rec Backword */
} TC_SPACE_TYPE;    /* Space command operations */

typedef enum {
	TC_FORMAT_DEFAULT   = 0x00,   /* Make 1 partition medium */
	TC_FORMAT_PARTITION = 0x01,   /* Make 2 partition medium */
	TC_FORMAT_DEST_PART = 0x02,   /* Destroy all data and make 2 partition medium */
	TC_FORMAT_MAX       = 0x03
} TC_FORMAT_TYPE;    /* Space command operations */

typedef enum {
	TC_MP_PC_CURRENT    = 0x00,    /* Get current value           */
	TC_MP_PC_CHANGEABLE = 0x40,    /* Get changeable bitmap       */
	TC_MP_PC_DEFAULT    = 0x80,    /* Get default(power-on) value */
	TC_MP_PC_SAVED      = 0xC0,    /* Get saved value             */
} TC_MP_PC_TYPE;    /* Page control (PC) value for ModePage */

#define TC_MP_DEV_CONFIG_EXT        (0x10) // ModePage 0x10 (Device Configuration Extension Page)
#define TC_MP_SUB_DEV_CONFIG_EXT    (0x01) // ModePage SubPage 0x01 (Device Configuration Extension Page)
#define TC_MP_DEV_CONFIG_EXT_SIZE   (48)

#define TC_MP_CTRL                  (0x0A) // ModePage 0x0A (Control Page)
#define TC_MP_SUB_DP_CTRL           (0xF0) // ModePage Subpage 0xF0 (Control Data Protection Page)
#define TC_MP_SUB_DP_CTRL_SIZE      (48)

#define TC_MP_COMPRESSION           (0x0F) // ModePage 0x0F (Data Compression Page)
#define TC_MP_COMPRESSION_SIZE      (32)

#define TC_MP_MEDIUM_PARTITION      (0x11) // ModePage 0x11 (Medium Partiton Page)
#define TC_MP_MEDIUM_PARTITION_SIZE (28)

#define TC_MP_MEDIUM_SENSE          (0x23) // ModePage 0x23 (Medium Sense Page)
#define TC_MP_MEDIUM_SENSE_SIZE     (76)

#define TC_MP_INIT_EXT              (0x24) // ModePage 0x24 (Initator-Specific Extentions)
#define TC_MP_INIT_EXT_SIZE         (40)

#define TC_MP_READ_WRITE_CTRL       (0x25) // ModePage 0x25 (Read/Write Control Page)
#define TC_MP_READ_WRITE_CTRL_SIZE  (48)

#define TC_MP_SUPPORTEDPAGE         (0x3F) // ModePage 0x3F (Supported Page Info)
#define TC_MP_SUPPORTEDPAGE_SIZE    (0xFF)

#define TC_MAM_PAGE_HEADER_SIZE    (0x5)
#define TC_MAM_PAGE_VCR            (0x0009) /* Page code of Volume Change Reference */
#define TC_MAM_PAGE_VCR_SIZE       (0x4)    /* Size of Volume Change Reference */
#define TC_MAM_PAGE_COHERENCY      (0x080C)
#define TC_MAM_PAGE_COHERENCY_SIZE (0x46)

#define TC_MAM_APP_VENDER          (0x0800)
#define TC_MAM_APP_VENDER_SIZE     (0x8)
#define TC_MAM_APP_NAME  (0x0801)
#define TC_MAM_APP_NAME_SIZE (0x20)
#define TC_MAM_APP_VERSION (0x0802)
#define TC_MAM_APP_VERSION_SIZE (0x8)
#define TC_MAM_USER_MEDIUM_LABEL (0x0803)
#define TC_MAM_USER_MEDIUM_LABEL_SIZE (0xA0)
#define TC_MAM_TEXT_LOCALIZATION_IDENTIFIER (0x0805)
#define TC_MAM_TEXT_LOCALIZATION_IDENTIFIER_SIZE (0x1)
#define TC_MAM_BARCODE (0x0806)
#define TC_MAM_BARCODE_SIZE (0x20)
#define TC_MAM_MEDIA_POOL (0x0808)
#define TC_MAM_MEDIA_POOL_SIZE (0xA0)
#define TC_MAM_APP_FORMAT_VERSION (0x080B)
#define TC_MAM_APP_FORMAT_VERSION_SIZE (0x10)
#define TC_MAM_VOLUME_LOCKED (0x1623)
#define TC_MAM_VOLUME_LOCKED_SIZE (0x1)

#define BINARY_FORMAT (0x0)
#define ASCII_FORMAT (0x1)
#define TEXT_FORMAT (0x2)

#define TEXT_LOCALIZATION_IDENTIFIER_ASCII (0x0)
#define TEXT_LOCALIZATION_IDENTIFIER_UTF8 (0x81)

enum eod_status {
	EOD_GOOD        = 0x00,
	EOD_MISSING     = 0x01,
	EOD_UNKNOWN     = 0x02
};

enum {
	MEDIUM_UNKNOWN = 0,
	MEDIUM_PERFECT_MATCH,
	MEDIUM_WRITABLE,
	MEDIUM_PROBABLY_WRITABLE,
	MEDIUM_READONLY,
	MEDIUM_CANNOT_ACCESS
};

/* Structure of tape operations */
struct tape_ops {
	/**
	 * Open a device.
	 * @param devname Name of the device to open. The format of this string is
	 *                implementation-dependent. For example, the ibmtape backend requires
	 *                the path to an IBM tape device, e.g. /dev/IBMtape0.
	 * @param[out] handle Stores the handle of the device on a successful call to this function.
	 *             The device handle is implementation-defined and treated as opaque by libltfs.
	 * @return 0 on success or a negative value on error.
	 */
	int (*open)(const char *devname, void **handle);

	/**
	 * Reopen a device. If reopen is not needed, do nothing in this call. (ie. ibmtape backend)
	 * @param devname Name of the device to open. The format of this string is
	 *                implementation-dependent. For example, the ibmtape backend requires
	 *                the path to an IBM tape device, e.g. /dev/IBMtape0.
	 * @param device Device handle returned by the backend's open().
	 * @return 0 on success or a negative value on error.
	 */
	int (*reopen)(const char *devname, void *handle);

	/**
	 * Close a previously opened device.
	 * @param device Device handle returned by the backend's open(). The handle is invalidated
	 *               and will not be reused after this function is called.
	 * @return 0 on success or a negative value on error.
	 */
	int   (*close)(void *device);

	/**
	 * Close only file descriptor
	 * @param device a pointer to the ibmtape backend
	 * @return 0 on success or a negative value on error
	 */
	int   (*close_raw)(void *device);

	/**
	 * Verify if a tape device is connected to the host.
	 * @param devname Name of the device to check. The format of this string is the same one
	 *                used in the open() operation.
	 * @return 0 to indicate that the tape device is connected and a negative value otherwise.
	 */
	int   (*is_connected)(const char *devname);

	/**
	 * Retrieve inquiry data from a device.
	 * This function is not currently used by libltfs. Backends not implementing it should
	 * zero out the inq parameter and return 0.
	 * @param device Device handle returned by the backend's open().
	 * @param inq Pointer to a tc_inq structure. On success, this structure's fields will be filled
	 *            using data from the device. Any fields which do not make sense for the device
	 *            are zero-filled.
	 * @return 0 on success or a negative value on error.
	 */
	int   (*inquiry)(void *device, struct tc_inq *inq);

	/**
	 * Retrieve inquiry data from a specific page.
	 * @param device Device handle returned by the backend's open().
	 * @param page Page to inquiry data from
	 * @param inq Pointer to a tc_inq_page structure. On success, this structure's fields will
	 *            be filled using data from the device.
	 * @return 0 on success or negative value on error
	 */
	int   (*inquiry_page)(void *device, unsigned char page, struct tc_inq_page *inq);

	/**
	 * Check whether a device is ready to accept commands.
	 * Some devices may indicate their readiness but still fail certain commands if a load() is
	 * not performed. Therefore, load() will be issued before any calls to this function.
	 * @param device Device handle returned by the backend's open().
	 * @return 0 if the device is ready, or a negative value otherwise.
	 */
	int   (*test_unit_ready)(void *device);

	/**
	 * Read exactly one block from a device, of at most the specified size.
	 * libltfs will break badly if this function reads more or less than one logical block.
	 * @param device Device handle returned by the backend's open().
	 * @param buf Buffer to receive data read from the device.
	 * @param count Buffer size (maximum block size to read).
	 * @param pos Pointer to a tc_position structure. The backend must fill this structure with
	 *            the final logical block position of the device, even on error.
	 *            libltfs expects the block position to increment by 1 on success; violating this
	 *            assumption may harm performance.
	 * @param unusual_size True if libltfs expects the actual block size to be smaller than
	 *                     the requested count. This is purely a hint: the backend must always
	 *                     correctly handle any block size up to the value of the count argument,
	 *                     regardless of the value of this flag.
	 * @return Number of bytes read on success, or a negative value on error. If a file mark is
	 *         encountered during reading, this function must return 0 and position the device
	 *         immediately after the file mark.
	 */
	int   (*read)(void *device, char *buf, size_t count, struct tc_position *pos, const bool unusual_size);

	/**
	 * Write the given bytes to a device in exactly one logical block.
	 * libltfs will break badly if this function writes only some of the given bytes, or if it
	 * splits them across multiple logical blocks.
	 * @param device Device handle returned by the backend's open().
	 * @param buf Buffer containing data to write to the device.
	 * @param count Buffer size (number of bytes to write).
	 * @param pos Pointer to a tc_position structure. The backend must fill this structure with
	 *            the final logical block position of the device, even on error.
	 *            libltfs expects the block position to increment by 1 on success; violating
	 *            this assumption may cause data corruption.
	 *
	 *            On success, libltfs also inspects the early_warning and
	 *            programmable_early_warning flags. These flags must be set by the backend when
	 *            low space (programmable_early_warning) or very low space (early_warning)
	 *            conditions are encountered. These flags are used by LTFS to decide when it should
	 *            stop accepting user data writes (programmable_early_warning) and when free space
	 *            is low enough to start panicking (early_warning).
	 *
	 *            Implementation of the early_warning flag is optional. If the backend can only
	 *            support a single low space warning, it should set programmable_early_warning
	 *            when it reaches that condition. The amount of space between
	 *            programmable_early_warning and the next "low space" state (either early_warning
	 *            or out of space) must be sufficient to write an index, preferably two indexes.
	 *            At least 10 GB is recommended, but values as low as 0.5 GB are safe for many
	 *            common use cases.
	 *
	 *            If a backend does not (or cannot) implement a low space warning, it must set
	 *            early_warning and programmable_early_warning to false (0). Note, however, that
	 *            data loss may occur with such backends when the medium runs out of space.
	 *            Therefore, any backend which is targeted at end users must support
	 *            the programmable_early_warning flag in this function if at all possible.
	 *            Support for programmable_early_warning in the writefm(), locate() and space()
	 *            functions is desirable, but not absolutely required.
	 * @return 0 on success or a negative value on error.
	 */
	int (*write)(void *device, const char *buf, size_t count, struct tc_position *pos);

	/**
	 * Write one or more file marks to a device.
	 * @param device Device handle returned by the backend's open().
	 * @param count Number of file marks to write. This function will not be called with a zero
	 *              argument. Currently libltfs only writes 1 file mark at a time, but this
	 *              function must correctly handle larger values.
	 * @param pos Pointer to a tc_position structure. The backend must fill this structure with
	 *            the final logical block position of the device, even on error.
	 *
	 *            On success, the programmable_early_warning and early_warning flags should be
	 *            set appropriately. These flags are optional for this function; libltfs will
	 *            function correctly if the backend always sets them to false (0).
	 *            See the documentation for write() for more information on the early
	 *            warning flags.
	 * @param immed Set immediate bit on
	 * @return 0 on success or a negative value on error.
	 */
	int (*writefm)(void *device, size_t count, struct tc_position *pos, bool immed);

	/**
	 * Rewind a device.
	 * Ideally the backend should position the device at partition 0, block 0. But libltfs
	 * does not depend on this behavior; for example, the file backend sets the position to
	 * block 0 of the current partition.
	 * This function is called immediately before unload().
	 * @param device Device handle returned by the backend's open().
	 * @param pos Pointer to a tc_position structure. The backend must fill this structure with
	 *            the final logical block position of the device, even on error.
	 * @return 0 on success or a negative value on error.
	 */
	int   (*rewind)(void *device, struct tc_position *pos);

	/**
	 * Seek to the specified position on a device.
	 * @param device Device handle returned by the backend's open().
	 * @param dest Destination position, specified as a partition and logical block. The filemarks
	 *             field must be ignored by the backend.
	 * @param pos Pointer to a tc_position structure. The backend must fill this structure with
	 *            the final logical block position of the device, even on error.
	 *            The backend should ensure that on success, pos matches dest. libltfs considers
	 *            pos != dest as an error, even if this function returns 0.
	 *
	 *            On success, the programmable_early_warning and early_warning flags should be
	 *            set appropriately. These flags are optional for this function; libltfs will
	 *            function correctly if the backend always sets them to false (0).
	 *            See the documentation for write() for more information on the early
	 *            warning flags.
	 * @return 0 on success or a negative value on error.
	 */
	int (*locate)(void *device, struct tc_position dest, struct tc_position *pos);

	/**
	 * Issue a space command to a device.
	 * @param device Device handle returned by the backend's open().
	 * @param count Number of items to space by.
	 * @param type Space type. Must be one of the following.
	 *             TC_SPACE_EOD: space to end of data on the current partition.
	 *             TC_SPACE_FM_F: space forward by file marks.
	 *             TC_SPACE_FM_B: space backward by file marks.
	 *             TC_SPACE_F: space forward by records.
	 *             TC_SPACE_B: space backward by records.
	 *             Currently only TC_SPACE_FM_F and TC_SPACE_FM_B are used by libltfs.
	 *             If TC_SPACE_FM_F is specified, the backend must skip the specified number of
	 *             file marks and position the device immediately after the last skipped file mark.
	 *             If TC_SPACE_FM_B is specified, the backend must skip the specified number of
	 *             file marks and position the device immediately before the last skipped file mark.
	 * @param pos Pointer to a tc_position structure. The backend must fill this structure with
	 *            the final logical block position of the device, even on error.
	 *
	 *            On success, the programmable_early_warning and early_warning flags should be
	 *            set appropriately. These flags are optional for this function; libltfs will
	 *            function correctly if the backend always sets them to false (0).
	 *            See the documentation for write() for more information on the early
	 *            warning flags.
	 * @return 0 on success or a negative value on error.
	 *
	 *         The backend should return an error if the requested operation causes the
	 *         device to cross the beginning of the current partition or the end of data
	 *         on the current partition, as these conditions will not occur in a valid LTFS volume.
	 */
	int (*space)(void *device, size_t count, TC_SPACE_TYPE type, struct tc_position *pos);

	/**
	 * Erase medium starting at the current position.
	 * This function is currently unused by libltfs.
	 * @param device Device handle returned by the backend's open().
	 * @param pos Pointer to a tc_position structure. The backend must fill this structure with
	 *            the final logical block position of the device, even on error.
	 * @param long_erase   Set long bit and immed bit ON
	 * @return 0 on success or a negative value on error.
	 */
	int   (*erase)(void *device, struct tc_position *pos, bool long_erase);

	/**
	 * Load medium into a device.
	 * libltfs calls this function after open() and reserve_unit(), but before any
	 * other backend calls.
	 * @param device Device handle returned by the backend's open().
	 * @param pos Pointer to a tc_position structure. The backend must fill this structure with
	 *            the final logical block position of the device, even on error.
	 *            libltfs does not depend on any particular position being set here.
	 * @return 0 on success or a negative value on error.
	 *         If no medium is present in the device, the backend must return -EDEV_NO_MEDIUM.
	 *         If the medium is unsupported (for example, does not support two partitions),
	 *         the backend should return -LTFS_UNSUPPORTED_MEDIUM.
	 */
	int   (*load)(void *device, struct tc_position *pos);

	/**
	 * Eject medium from a device.
	 * @param device Device handle returned by the backend's open().
	 * @param pos Pointer to a tc_position structure. The backend should zero out this structure
	 *            on success. On error, it must fill this structure with the final logical
	 *            block position of the device.
	 * @return 0 on success or a negative value on error.
	 */
	int   (*unload)(void *device, struct tc_position *pos);

	/**
	 * Read logical position (partition and logical block) from a device.
	 * @param device Device handle returned by the backend's open().
	 * @param pos Pointer to a tc_position structure. On success, the backend must fill this
	 *            structure with the current logical block position of the device. On error,
	 *            its contents must be unchanged.
	 * @return 0 on success or a negative value on error.
	 */
	int   (*readpos)(void *device, struct tc_position *pos);

	/**
	 * Set the capacity proportion of the medium.
	 * This function is always preceded by a locate request for partition 0, block 0.
	 * @param device Device handle returned by the backend's open().
	 * @param proportion Number to specify the proportion from 0 to 0xFFFF. 0xFFFF is for full capacity.
	 * @return 0 on success or a negative value on error.
	 */
	int   (*setcap)(void *device, uint16_t proportion);

	/**
	 * Format a device.
	 * This function is always preceded by a locate request for partition 0, block 0.
	 * @param device Device handle returned by the backend's open().
	 * @param format Type of format to perform. Currently libltfs uses the following values.
	 *               TC_FORMAT_DEFAULT: create a single partition on the medium.
	 *               TC_FORMAT_DEST_PART: create two partitions on the medium.
	 * @return 0 on success or a negative value on error.
	 */
	int   (*format)(void *device, TC_FORMAT_TYPE format);

	/**
	 * Get capacity data from a device.
	 * @param device Device handle returned by the backend's open().
	 * @param cap On success, the backend must fill this structure with the total and remaining
	 *            capacity values of the two partitions on the medium, in units of 1048576 bytes.
	 * @return 0 on success or a negative value on error.
	 */
	int   (*remaining_capacity)(void *device, struct tc_remaining_cap *cap);

	/**
	 * Send a SCSI Log Sense command to a device.
	 * libltfs does not currently use this function, but it may be useful internally (for example,
	 * ibmtape uses it from its remaining_capacity() function).
	 * @param device Device handle returned by the backend's open().
	 * @param page Log page to query.
	 * @param buf On success, the backend must fill this buffer with the log page's value.
	 * @param size Buffer size.
	 * @return 0 on success or a negative value on error. Backends for which Log Sense is
	 *         meaningless should return -1.
	 */
	int   (*logsense)(void *device, const uint8_t page, unsigned char *buf, const size_t size);

	/**
	 * Send a SCSI Mode Sense(10) command to a device.
	 * This is used by libltfs to query the device's current partition size settings.
	 * @param device Device handle returned by the backend's open().
	 * @param page Mode page to query.
	 * @param pc Page control value for the command. Currently libltfs only uses
	 *  		 TC_MP_PC_CURRENT (to request the current value of the mode page).
	 * @param subpage Subpage of the specified mode page.
	 * @param buf On success, the backend must fill this buffer with the mode page's value.
	 * @param size Buffer size.
	 * @return 0 on success or a negative value on error. Backends for which Mode Sense is
	 *         meaningless should zero out the buffer and return 0.
	 */
	int   (*modesense)(void *device, const uint8_t page, const TC_MP_PC_TYPE pc, const uint8_t subpage, unsigned char *buf, const size_t size);

	/**
	 * Send a SCSI Mode Select(10) command to a device.
	 * This is used by libltfs to update the partition size settings of the device.
	 * @param device Device handle returned by the backend's open().
	 * @param buf Buffer containing the new mode page value to set.
	 * @param size Buffer size.
	 * @return 0 on success or a negative value on error. Backends for which Mode Select is
	 *         meaningless should return 0.
	 */
	int   (*modeselect)(void *device, unsigned char *buf, const size_t size);

	/**
	 * Send a SCSI Reserve Unit command to a device.
	 * libltfs calls this function immediately after opening the device to prevent contention
	 * with other initiators.
	 * @param device Device handle returned by the backend's open().
	 * @return 0 on success or a negative value on error.
	 */
	int   (*reserve_unit)(void *device);

	/**
	 * Send a SCSI Release Unit command to a device.
	 * libltfs calls this function immediately before closing the device to allow access by
	 * other initiators.
	 * @param device Device handle returned by the backend's open().
	 * @return 0 on success or a negative value on error.
	 */
	int   (*release_unit)(void *device);

	/**
	 * Lock the medium in a device, preventing manual removal.
	 * libltfs calls this function immediately after load().
	 * @param device Device handle returned by the backend's open().
	 * @return 0 on success or a negative value on error.
	 */
	int   (*prevent_medium_removal)(void *device);

	/**
	 * Unlock the medium in a device, allowing manual removal.
	 * @param device Device handle returned by the backend's open().
	 * @return 0 on success or a negative value on error.
	 */
	int   (*allow_medium_removal)(void *device);

	/**
	 * Read a MAM parameter from a device.
	 * For performance reasons, it is recommended that all backends implement MAM parameter support.
	 * However, this support is technically optional.
	 * @param device Device handle returned by the backend's open().
	 * @param part Partition to read the parameter from.
	 * @param id Attribute ID to read. libltfs uses TC_MAM_PAGE_VCR and TC_MAM_PAGE_COHERENCY.
	 * @param buf On success, the backend must place the MAM parameter in this buffer.
	 *            Otherwise, the backend should zero out the buffer.
	 * @param size Buffer size. The backend behavior is implementation-defined if the buffer
	 *             is too small to receive the MAM parameter.
	 * @return 0 on success or a negative value on error. A backend which does not implement
	 *         MAM parameters must zero the output buffer and return a negative value.
	 */
	int   (*read_attribute)(void *device, const tape_partition_t part, const uint16_t id, unsigned char *buf, const size_t size);

	/**
	 * Write a MAM parameter to a device.
	 * For performance reasons, it is recommended that all backends implement MAM parameter support.
	 * However, this support is technically optional.
	 * libltfs calls this to set cartridge coherency data from tape_set_cartridge_coherency().
	 * @param device Device handle returned by the backend's open().
	 * @param part Partition to write the parameter to.
	 * @param buf Parameter to write. It is formatted for copying directly into the CDB, so
	 *            it contains a header with the attribute ID and size.
	 * @param size Buffer size.
	 * @return 0 on success or a negative value on error. A backend which does not implement
	 *         MAM parameters should return a negative value.
	 */
	int   (*write_attribute)(void *device, const tape_partition_t part, const unsigned char *buf, const size_t size);

	/**
	 * Set append point to the device.
	 * The device will accept write commmand only on specified position or EOD, if the dvice
	 * supports this feature.
	 * @param device Device handle returned by the backend's open().
	 * @param pos position to accept write command
	 * @return 0 on success or a negative value on error.
	 */
	int   (*allow_overwrite)(void *device, const struct tc_position pos);

	/**
	 * Enable or disable compression on a device.
	 * @param device Device handle returned by the backend's open().
	 * @param enable_compression If true, turn on compression. Otherwise, turn it off.
	 * @param pos Pointer to a tc_position structure. The backend must fill this structure with
	 *            the final logical block position of the device, even on error.
	 * @return 0 on success or a negative value on error. If the underlying device does not
	 *         support transparent compression, the backend should always return 0.
	 */
	int   (*set_compression)(void *device, const bool enable_compression, struct tc_position *pos);

	/**
	 * Set up any required default parameters for a device.
	 * The effect of this function is implementation-defined. For example, the file backend
	 * does nothing, while the ibmtape backend sets the device blocksize to variable and disables
	 * the IBM tape driver's read past file mark option.
	 * @param device Device handle returned by the backend's open().
	 * @return 0 on success or a negative value on error.
	 */
	int   (*set_default)(void *device);

	/**
	 * Get cartridge health data from the drive
	 * @param device Device handle returned by the backend's open().
	 * @param cart_health On success, the backend must fill this structure with the cartridge health
	 *                    "-1" shows the unsupported value except tape alert.
	 * @return 0 on success or a negative value on error.
	 */
	int   (*get_cartridge_health)(void *device, struct tc_cartridge_health *cart_health);

	/**
	 * Get tape alert from the drive this value shall be latched by backends and shall be cleard by
	 * clear_tape_alert() on write clear method
	 * @param device Device handle returned by the backend's open().
	 * @param tape alert On success, the backend must fill this value with the tape alert
	 *                    "-1" shows the unsupported value except tape alert.
	 * @return 0 on success or a negative value on error.
	 */
	int   (*get_tape_alert)(void *device, uint64_t *tape_alert);

	/**
	 * clear latched tape alert from the drive
	 * @param device Device handle returned by the backend's open().
	 * @param tape_alert value to clear tape alert. Backend shall be clear the specicied bits in this value.
	 * @return 0 on success or a negative value on error.
	 */
	int   (*clear_tape_alert)(void *device, uint64_t tape_alert);

	/**
	 * Get vendor unique backend xattr
	 * @param device Device handle returned by the backend's open().
	 * @param name   Name of xattr
	 * @param buf    On success, the backend must fill this value with the pointer of data buffer for xattr
	 * @return 0 on success or a negative value on error.
	 */
	int   (*get_xattr)(void *device, const char *name, char **buf);

	/**
	 * Get vendor unique backend xattr
	 * @param device Device handle returned by the backend's open().
	 * @param name   Name of xattr
	 * @param buf    Data buffer to set the value
	 * @param size   Length of data buffer
	 * @return 0 on success or a negative value on error.
	 */
	int   (*set_xattr)(void *device, const char *name, const char *buf, size_t size);

	/**
	 * Get operational parameters of a device. These parameters include such things as the
	 * maximum supported blocksize and medium write protect state.
	 * @param device Device handle returned by the backend's open().
	 * @param params On success, the backend must fill this structure with the device
	 *                    parameters.
	 * @return 0 on success or a negative value on error.
	 */
	int   (*get_parameters)(void *device, struct tc_current_param *params);

	/**
	 * Get EOD status of a partition.
	 * @param device Device handle returned by the backend's open().
	 * @param part Partition to read the parameter from.
	 * @param pos Pointer to a tc_position structure. The backend must fill this structure with
	 *            the final logical block position of the device, even on error.
	 * @return enum eod_status or UNSUPPORTED_FUNCTION if not supported.
	 */
	int   (*get_eod_status)(void *device, int part);


	/**
	 * Get a list of available tape devices for LTFS found in the host. The caller is
	 * responsible from allocating the buffer to contain the tape drive information
	 * by get_device_count() call.
	 * When buf is NULL, this function just returns an available tape device count.
	 * @param[out] buf Pointer to tc_drive_info structure array.
	 *             The backend must fill this structure when this paramater is not NULL.
	 * @param count size of array in buf.
	 * @return on success, available device count on this system or a negative value on error.
	 */
	int   (*get_device_list)(struct tc_drive_info *buf, int count);

	/**
	 * Print a help message for the backend.
	 * This function should print options specific to the backend. For example, the IBM
	 * backends print their default device names.
	 */
	void  (*help_message)(void);

	/**
	 * Parse backend-specific options.
	 * For example: the file backend takes an argument to write protect its
	 * simulated tape cartridge.
	 * @param device Device handle returned by the backend's open().
	 * @param opt_args Pointer to a FUSE argument structure, suitable for passing to
	 *                 fuse_opt_parse(). See the file backend for an example of argument parsing.
	 * @return 0 on success or a negative value on error.
	 */
	int   (*parse_opts)(void *device, void *opt_args);

	/**
	 * Get the default device name for the backend.
	 * @return A pointer to the default device name string. This pointer is not freed on exit
	 *         by the IBM LTFS utilities. It may be NULL if the backend has no default device.
	 */
	const char *(*default_device_name)(void);

	/**
	 * Set the data key for application-managed encryption.
	 * @param device Device handle returned by the backend's open().
	 * @param keyalias A pointer to Data Key Identifier (DKi).
	 *                 DKi compounded from 3 bytes ASCII characters and 9 bytes binary data.
	 * @param key A pointer to Data Key (DK). DK is 32 bytes binary data.
	 * @return 0 on success or a negative value on error.
	 */
	int   (*set_key)(void *device, const unsigned char *keyalias, const unsigned char *key);

	/**
	 * Get the key alias of the next block for application-managed encryption.
	 * @param device Device handle returned by the backend's open().
	 * @param[out] keyalias A pointer to Data Key Identifier (DKi).
	 *                      DKi compounded from 3 bytes ASCII characters and 9 bytes binary data.
	 * @return 0 on success or a negative value on error.
	 */
	int   (*get_keyalias)(void *device, unsigned char **keyalias);

	/**
	 * Take a dump from the tape drive.
	 * @param device Device handle returned by the backend's open().
	 * @return 0 on success or a negative value on error.
	 */
	int   (*takedump_drive)(void *device, bool capture_unforced);

	/**
	 * Check if the tape drive can mount the medium.
	 * @param device Device handle returned by the backend's open().
	 * @param barcode Bar code of the medium
	 * @param cart_type Cartridge type of the medium (in the CM on LTO)
	 *                  0x00 when this tape never be loaded to a drive
	 * @param density Density code of the medium
	 *                  0x00 when this tape never be loaded to a drive
	 * @return MEDIUM_PERFECT_MATCH when this drive support the tape naively,
	 *         MEDIUM_WRITABLE when this drive can read/write the medium,
	 *         MEDIUM_READONLY when this drive can only read the medium,
	 *         MEDIUM_CANNOT_ACCESS when this drive cannot read/write the medium,
	 *         MEDIUM_PROBABLY_WRITABLE when this drive may read/write the medium like
	 *         JC cartridge which never be loaded yet (no density_code information) and
	 *         the drive is TS1140
	 */
	int   (*is_mountable)(void *device,
						  const char *barcode,
						  const unsigned char cart_type,
						  const unsigned char density);

	/**
	 * Check if the loaded carridge is WORM.
	 * @param device Device handle returned by the backend's open().
	 * @param is_worm Pointer to worm status.
	 * @return 0 on success or a negative value on error.
	 */
	int   (*get_worm_status)(void *device, bool *is_worm);

	/**
	 * Get the tape device's serial number
	 * @param device a pointer to the tape device
	 * @param[out] result On success, contains the serial number of the changer device.
	 *                    The memory is allocated on demand and must be freed by the user
	 *                    once it's been used.
	 * @return 0 on success or a negative value on error
	 */
	int   (*get_serialnumber)(void *device, char **result);

	/**
	 * Enable profiler function
	 * @param device a pointer to the tape device
	 * @param work_dir work directory to store profiler data
	 * @paran enable enable or disable profiler function of this backend
	 * @return 0 on success or a negative value on error
	 */
	int   (*set_profiler)(void *device, char *work_dir, bool enable);

	/**
	 * Get block number stored in the drive buffer
	 * @param device A pointer to the tape device
	 * @param block Number of blocks stored in the drive buffer
	 * @return 0 on success or a negative value on error
	 */
	int   (*get_block_in_buffer)(void *device, unsigned int *block);

	/**
	 * Check if the generation of tape drive and the current loaded cartridge is read-only combination
	 * @param device Device handle returned by the backend's open().
	 */
	bool   (*is_readonly)(void *device);
};

/**
 * Get the operations structure for a backend. Every backend must implement this function,
 * as the plugin architecture relies on this to find pointers to the backend's operations.
 * @return Pointer to a tape_ops structure containing the backend's operations.
 */
struct tape_ops *tape_dev_get_ops(void);

/**
 * Get the message bundle name for a backend.
 * A backend may provide its own statically compiled ICU message bundle
 * whose messages will be used by libltfs.
 * See the LTFS build system and the IBM backends for examples of creating and
 * using an ICU message bundle.
 * Every backend must implement this function.
 * @param message_data If the backend contains a statically compiled message bundle, it must
 *                     return a pointer to the message bundle data in this parameter.
 * @return The name of the ICU message bundle for this backend, or NULL if the backend does not
 *         contain a message bundle.
 */
const char *tape_dev_get_message_bundle_name(void **message_data);

/***********************************************************************************
* NOTE:                                                                            *
*    int (*read_attribute)(void *device,                                           *
*                          const tc_partition part, const int id,                  *
*                          char *buf, const size_t size);                          *
*                                                                                  *
*    int (*write_attribute)(void *device,                                          *
*                           const tc_partition part,                               *
*                           const char *buf, const size_t size);                   *
*                                                                                  *
************************************************************************************
*                                        **                                        *
*   Expected data of argument "buf" in   **  Each attribute #(1-y) data is defined *
*   WriteAttribute() and ReadAttribute() **  as below                              *
*   method                               **                                        *
*                                        **                                        *
* +---+-------------------------------+  **  +---+-------------------------------+ *
* |   |          BIT                  |  **  |   |          BIT                  | *
* +BY +---+---+---+---+---+---+---+---+  **  +BY +---+---+---+---+---+---+---+---+ *
* | TE| 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |  **  | TE| 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 | *
* +---+---+---+---+---+---+---+---+---+  **  +---+---+---+---+---+---+---+---+---+ *
* | 4 |                               |  **  | 0 |                               | *
* +---+                               |  **  +---+    Attribute Identifier #1    | *
* |...|    Attribute #1               |  **  | 1 |                               | *
* +---+                               |  **  +---+---+---+---+---+---+---+---+---+ *
* | x |                               |  **  | 2 | RO|     Reserved      | Fromat| *
* +---+---+---+---+---+---+---+---+---+  **  +---+---+---+---+---+---+---+---+---+ *
* | m |                               |  **  | 3 |                               | *
* +---+                               |  **  +---+    Attribute Length (n-4)     | *
* |...|    Attribute #y               |  **  | 4 |                               | *
* +---+                               |  **  +---+---+---+---+---+---+---+---+---+ *
* | n |                               |  **  | 5 |                               | *
* +---+---+---+---+---+---+---+---+---+  **  +---+                               | *
*                                        **  |...|    Attribute Value            | *
*                                        **  +---+                               | *
*                                        **  | n |                               | *
*                                        **  +---+---+---+---+---+---+---+---+---+ *
************************************************************************************/

/**
 * Request type definisions for LTFS request profile
 */

#define REQ_TC_OPEN        0000	/**< open: Unused */
#define REQ_TC_REOPEN      0001	/**< reopen: Unused */
#define REQ_TC_CLOSE       0002	/**< close */
#define REQ_TC_CLOSERAW    0003	/**< close_raw */
#define REQ_TC_ISCONNECTED 0004	/**< is_connected: Unused*/
#define REQ_TC_INQUIRY     0005	/**< inquiry */
#define REQ_TC_INQUIRYPAGE 0006	/**< inquiry_page */
#define REQ_TC_TUR         0007	/**< test_unit_ready */
#define REQ_TC_READ        0008	/**< read */
#define REQ_TC_WRITE       0009	/**< write */
#define REQ_TC_WRITEFM     000a	/**< writefm */
#define REQ_TC_REWIND      000b	/**< rewind */
#define REQ_TC_LOCATE      000c	/**< locate */
#define REQ_TC_SPACE       000d	/**< space */
#define REQ_TC_ERASE       000e	/**< erase */
#define REQ_TC_LOAD        000f	/**< load */
#define REQ_TC_UNLOAD      0010	/**< unload */
#define REQ_TC_READPOS     0011	/**< readpos */
#define REQ_TC_SETCAP      0012	/**< setcap*/
#define REQ_TC_FORMAT      0013	/**< format */
#define REQ_TC_REMAINCAP   0014	/**< remaining_capacity */
#define REQ_TC_LOGSENSE    0015	/**< logsense */
#define REQ_TC_MODESENSE   0016	/**< modesense */
#define REQ_TC_MODESELECT  0017	/**< modeselect */
#define REQ_TC_RESERVEUNIT 0018	/**< reserve_unit */
#define REQ_TC_RELEASEUNIT 0019	/**< release_unit */
#define REQ_TC_PREVENTM    001a	/**< prevent_medium_removal */
#define REQ_TC_ALLOWMREM   001b	/**< allow_medium_removal */
#define REQ_TC_READATTR    001c	/**< read_attribute */
#define REQ_TC_WRITEATTR   001d	/**< write_attribute */
#define REQ_TC_ALLOWOVERW  001e	/**< allow_overwrite */
#define REQ_TC_REPDENSITY  001f	/**< report_density */
#define REQ_TC_SETCOMPRS   0020	/**< set_compression */
#define REQ_TC_SETDEFAULT  0021	/**< set_default */
#define REQ_TC_GETCARTHLTH 0022	/**< get_cartridge_health */
#define REQ_TC_GETTAPEALT  0023	/**< get_tape_alert */
#define REQ_TC_CLRTAPEALT  0024	/**< clear_tape_alert */
#define REQ_TC_GETXATTR    0025	/**< getxattr */
#define REQ_TC_SETXATTR    0026	/**< setxattr */
#define REQ_TC_GETPARAM    0027	/**< get_parameters */
#define REQ_TC_GETEODSTAT  0028	/**< get_eod_status */
#define REQ_TC_GETDLIST    0029	/**< get_device_list: Unused */
#define REQ_TC_HELPMSG     002a	/**< help_message: Unused */
#define REQ_TC_PARSEOPTS   002b	/**< parse_opts: Unused */
#define REQ_TC_DEFDEVNAME  002c	/**< default_device_name: Unused */
#define REQ_TC_SETKEY      002d	/**< set_key */
#define REQ_TC_GETKEYALIAS 002e	/**< get_keyalias */
#define REQ_TC_TAKEDUMPDRV 002f	/**< takedump_drive */
#define REQ_TC_ISMOUNTABLE 0030	/**< is_mountable */
#define REQ_TC_GETWORMSTAT 0031	/**< get_worm_status */

#define REQ_TC_GETSLOTS    0032	/**< getslots */
#define REQ_TC_INVENTORY   0033	/**< inventory */
#define REQ_TC_MOVEMEDIA   0034	/**< movemedia */
#define REQ_TC_GETDMAP     0035	/**< get_devmap */
#define REQ_TC_GETSER      0036	/**< get_serialnumber */
#define REQ_TC_SETSUPCHG   0037	/**< set_supported_changers: Unused */

#endif /* __tape_ops_h */
