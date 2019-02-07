/************************************************************************************
**
** LTFS backend for LTO and DAT tape drives
**
** FILE:            ltotape.h
**
** CONTENTS:        Definitions, typedefs etc for the ltotape LTFS backend
**
** (C) Copyright 2015 - 2017 Hewlett Packard Enterprise Development LP
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
**
**  (C) Copyright 2015 - 2017 Hewlett Packard Enterprise Development LP
**  10/13/17 Added support for LTO8 media
**
*************************************************************************************
**
** Copyright (C) 2012 OSR Open Systems Resources, Inc.
** 
************************************************************************************* 
*/

#ifndef __ltotape_h
#define __ltotape_h

#ifdef __APPLE__
# include <CoreFoundation/CoreFoundation.h>
# include <IOKit/IOTypes.h>
# include <IOKit/IOKitLib.h>
# include <IOKit/scsi/SCSITaskLib.h>
#endif

/*
 * An enumerated type specifying data flow direction during command execution:
 */
typedef enum {
  HOST_WRITE,
  HOST_READ,
  NO_TRANSFER,
  UNKNOWN_DIRECTION
} direction;

/*
 * An enumerated type to distinguish drive families:
 */
typedef enum {
   drivefamily_lto,
   drivefamily_dat,
   drivefamily_unknown
} drivefamily_type;

/*
 * An enumerated type to distinguish drive type:
 */
typedef enum {
   drive_unsupported,
   drive_lto8,
   drive_lto7,
   drive_lto6,
   drive_lto5,
   drive_lto4,
   drive_dat,
   drive_unknown
} drive_family;


#ifdef QUANTUM_BUILD
/*
 * An enumerated type to distinguish T10 Vendor Identification:
 */
typedef enum {
   drivevendor_unknown,
   drivevendor_hp,
   drivevendor_quantum 
} drivevendor_type;
#endif

/*
 * An enumerated type used for handling of early warning:
 */
typedef enum {
   before_eweom,
   after_eweom,
   report_eweom
} ltotape_eweomstate_type;

/*
 * The underlying structure for drive communications, used internally:
 */
typedef struct {

#ifdef HPE_mingw_BUILD
   /* We need this to be pointer precision */
   void            *fd;
#else
   int			   fd;
#endif
   unsigned char	   cdb[16];
   int			   cdb_length;
   unsigned char	  *data;
   int			   data_length;
   direction		   data_direction;
   int			   actual_data_length;
   unsigned char	   sensedata[128];
   int			   sense_length;
   int			   timeout_ms;
   drivefamily_type	   family;
   drive_family	       type;
   char			   serialno[32];
   ltotape_eweomstate_type eweomstate;
   char*                   logdir;
   int                     unlimited_blocksize;
#ifdef QUANTUM_BUILD
   drivevendor_type	   drive_vendor_id;
#endif
/*
 * Platform-specific members:
 */
#ifdef __APPLE__
   IOCFPlugInInterface     **plugInInterface;
   SCSITaskDeviceInterface **interface;
   SCSITaskInterface       **task;
#endif

} ltotape_scsi_io_type;


typedef struct {
    const char* product_id;
    drivefamily_type product_family;
    const char* description;
    drive_family drive_type;
} supported_device_type;

/*
 * SCSI Command opcodes:
 */
#define CMDtest_unit_ready	  0x00
#define	CMDrewind		  0x01
#define CMDrequest_block_addr     0x02
#define CMDrequest_sense	  0x03
#define CMDformat                 0x04
#define CMDread_block_limits	  0x05
#define CMDread			  0x08
#define CMDwrite		  0x0A
#define CMDset_capacity		  0x0B
#define CMDseek_block		  0x0C
#define CMDwrite_filemarks	  0x10
#define	CMDspace		  0x11
#define	CMDinquiry		  0x12
#define CMDverify		  0x13
#define CMDmode_select		  0x15
#define CMDreserve		  0x16
#define CMDrelease		  0x17
#define CMDerase		  0x19
#define CMDmode_sense		  0x1A
#define CMDload			  0x1B
#define CMDrecv_diag_results	  0x1C
#define CMDsend_diagnostic	  0x1D
#define CMDprevent_allow_media    0x1E
#define CMDmedia_census		  0x1F
#define CMDread_capacity	  0x25
#define CMDread10		  0x28
#define CMDlocate		  0x2B
#define CMDread_position	  0x34
#define CMDwrite_buffer		  0x3B
#define CMDread_buffer		  0x3C
#define CMDreport_density_support 0x44
#define CMDlog_select		  0x4C
#define CMDlog_sense		  0x4D
#define CMDmode_select10	  0x55
#define CMDreserve10		  0x56
#define CMDrelease10		  0x57
#define CMDmode_sense10		  0x5A
#define CMDpersistent_reserve_in  0x5E
#define CMDpersistent_reserve_out 0x5F
#define CMDread_attribute	  0x8C
#define CMDwrite_attribute	  0x8D
#define CMDlocate16		  0x92
#define CMDservice_in16		  0x9E
#define CMDservice_out16	  0x9F
#define CMDreport_luns		  0xA0
#define CMDsecurity_in		  0xA2
#define CMDmaintenance_in	  0xA3
#define CMDmaintenance_out	  0xA4
#define	CMDmove_medium		  0xA5
#define CMDservice_out12	  0xA9
#define CMDservice_in12		  0xAB
#define CMDsecurity_out		  0xB5
#define	CMDread_element_status	  0xB8

/*
 * SCSI Status values:
 */
#define S_NO_STATUS	        0xFF  /* No SCSI status phase occurred */
#define S_GOOD			0x00
#define S_CHECK_CONDITION	0x02
#define S_CONDITION_MET		0x04
#define S_BUSY			0x08
#define S_INTERMEDIATE		0x10
#define S_I_CONDITION_MET	0x14
#define S_RESV_CONFLICT		0x18
#define S_COMMAND_TERMINATED	0x22
#define S_QUEUE_FULL		0x28
#define S_ACA_ACTIVE		0x30
#define S_TASK_ABORTED		0x40

/*
 * Driver Status values:
 */
#define DS_ILLEGAL              0xFF /* Illegal request passed to driver 	  */
#define DS_GOOD		        0x00 /* Command succeeded			  */
#define DS_TIMEOUT	        0x01 /* Command didn't complete within timeout    */
#define DS_BUS_FREE_ERROR       0x03 /* Bus free occurred unexpectedly            */
#define DS_LENGTH_ERROR         0x05 /* Drive requested or supplied wrong # bytes */
#define DS_SELECTION_TIMEOUT    0x09 /* No response from drive during selection   */
#define DS_BUS_PHASE_ERROR      0x10 /* Some illegal phase change occurred        */
#define DS_AUTO_REQSENSE_FAILED 0x20 /* The auto request sense didn't work..      */
#define DS_FAILED		0x30 /* Command failed for some other reason      */
#define DS_RESET                0x40 /* The driver reported a (bus) reset event   */

/*
 * Here are some transport error definitions; originally from 'sg_err.h':
 * First the 'host_status' codes:
 */
#define SG_ERR_DID_OK           0
#define SG_ERR_DID_NO_CONNECT   1
#define SG_ERR_DID_BUS_BUSY     2
#define SG_ERR_DID_TIME_OUT     3
#define SG_ERR_DID_BAD_TARGET   4
#define SG_ERR_DID_ABORT        5
#define SG_ERR_DID_PARITY       6
#define SG_ERR_DID_ERROR        7
#define SG_ERR_DID_RESET        8
#define SG_ERR_DID_BAD_INTR     9
#define SG_ERR_DID_PASSTHROUGH  10
#define SG_ERR_DID_SOFT_ERROR   11

/*
 * Followed by 'driver_status' codes:
 */
#define SG_ERR_DRIVER_OK        0
#define SG_ERR_DRIVER_BUSY      1
#define SG_ERR_DRIVER_SOFT      2
#define SG_ERR_DRIVER_MEDIA     3
#define SG_ERR_DRIVER_ERROR     4
#define SG_ERR_DRIVER_INVALID   5
#define SG_ERR_DRIVER_TIMEOUT   6
#define SG_ERR_DRIVER_HARD      7
#define SG_ERR_DRIVER_SENSE     8

/*
 * Inquiry VPD page definitions:
 */
#define VPD_PAGE_SERIAL_NUMBER     0x80

/*
 * Mode page code definitions:
 */
#define MODE_PAGE_DATA_COMPRESSION     0x0F
#define MODE_PAGE_MEDIUM_CONFIGURATION 0x1D

/*
 * Some definitions to assist in determination of partition-capable drives
 *  for the Medium Partition mode page (11h):
 */
#define PARTTYPES_OFFSET  20  /* FDP, IDP, SDP bits are here in ModeSense10 data */
#define PARTTYPES_MASK  0xE0  /* FDP, IDP and SDP bit positions                  */

/*
 *  Log page definitions
 */
#define LOG_PAGE_HEADER_SIZE      (4)
#define LOG_PAGE_PARAMSIZE_OFFSET (3)
#define LOG_PAGE_PARAM_OFFSET     (4)

#define LOG_PAGE_VOLUMESTATS   (0x17)
#define LOG_PAGE_TAPE_ALERT    (0x2E)

#define LOG_PAGE_VOL_PART_HEADER_SIZE (4)

enum volstatvalues {
  VOLSTATS_MOUNTS           = 0x0001,	/* < Volume Mounts */
  VOLSTATS_WRITTEN_DS       = 0x0002,	/* < Volume Written DS */
  VOLSTATS_WRITE_TEMPS      = 0x0003,	/* < Volume Temp Errors on Write */
  VOLSTATS_WRITE_PERMS      = 0x0004,	/* < Volume Perm Errors_on Write */
  VOLSTATS_READ_DS          = 0x0007,	/* < Volume Read DS */
  VOLSTATS_READ_TEMPS       = 0x0008,	/* < Volume Temp Errors on Read */
  VOLSTATS_READ_PERMS       = 0x0009,	/* < Volume Perm Errors_on Read */
  VOLSTATS_WRITE_PERMS_PREV = 0x000C,	/* < Volume Perm Errors_on Write (previous mount)*/
  VOLSTATS_READ_PERMS_PREV  = 0x000D,	/* < Volume Perm Errors_on Read (previous mount) */
  VOLSTATS_WRITE_MB         = 0x0010,	/* < Volume Written MB */
  VOLSTATS_READ_MB          = 0x0011,	/* < Volume Read MB */
  VOLSTATS_PASSES_BEGIN     = 0x0101,	/* < Beginning of medium passes */
  VOLSTATS_PASSES_MIDDLE    = 0x0102,	/* < Middle of medium passes */
  VOLSTATS_USED_CAPACITY    = 0x0203,   /* < Approx used native capacity MB */
  VOLSTATS_VU_PGFMTVER      = 0xF000    /* < Vendor-unique PageFormatVersion */
};

/*
 * Const used in ltotape_modesense / ltotape_modeselect to limit length for 16-bit field:
 */
#define MAX_UINT16 (0x0000FFFF)

/*
 * Definition of "sequential access" peripheral device type:
 */
#define SCSI_PERIPHERAL_DEVICE_TYPE_SEQACCESS  0x01

/*
 * Definitions related to MAM Attributes
 *
 * Standard attribute 0x0800 is the Application Vendor, 8 ASCII bytes
 * Standard attribute 0x0801 is the Application Name, 32 ASCII bytes
 * Standard attribute 0x0802 is the Application Version, 8 ASCII bytes
 *
 */
#define LTOATTRIBID_APPLICATION_VENDOR    0x0800
#define LTOATTRIB_APPLICATION_VENDOR_LEN  8

#define LTOATTRIBID_APPLICATION_NAME      0x0801
#define LTOATTRIB_APPLICATION_NAME_LEN    32

#define LTOATTRIBID_APPLICATION_VERSION   0x0802
#define LTOATTRIB_APPLICATION_VERSION_LEN 8

#define LTOATTRIBID_APP_FORMAT_VERSION    0x080B
#define LTOATTRIB_APP_FORMAT_VERSION_LEN  16

#define LTOATTRIBID_USR_MED_TXT_LABEL	  0x0803
#define LTOATTRIBID_USR_MED_TXT_LABEL_LEN 160

#define LTOATTRIBID_BARCODE	  			  0x0806
#define LTOATTRIBID_BARCODE_LEN 		  32

#define LTOATTRIBID_VOL_LOCK_STATE		  0x1623
#define LTOATTRIBID_VOL_LOCK_STATE_LEN	  1

#define LTOATTRIBID_VOL_UUID			  0x0820
#define LTOATTRIBID_VOL_UUID_LEN		  36

#define ATTRIB_HEADER_LEN                5  /* every attrib has a five-byte header */

/*
 * Some useful macros to test for specific sense data.
 *  Parameter is assumed to be start of array of sense data bytes
 */
#define SENSE_IS_BLANK_CHECK_EOD(b)   (((b[2] & 0x0F) == 0x08) && (b[12] == 0x00) && (b[13] == 0x05))
#define SENSE_IS_BLANK_CHECK_NOEOD(b) (((b[2] & 0x0F) == 0x08) && (b[12] == 0x14) && (b[13] == 0x03))
#define SENSE_IS_FILEMARK_DETECTED(b) (((b[2] & 0x8F) == 0x80) && (b[12] == 0x00) && (b[13] == 0x01)) 
#define SENSE_IS_NO_MEDIA(b)          (((b[2] & 0x0F) == 0x02) && (b[12] == 0x3A) && (b[13] == 0x00))
#define SENSE_IS_EARLY_WARNING_EOM(b) (((b[2] & 0x4F) == 0x40) && (b[12] == 0x00) && (b[13] == 0x02))
#define SENSE_IS_EARLY_WARNING_PEOM(b) ((((b[2] & 0x4F) == 0x40) || ((b[2] & 0x4F) == 0x00)) && (b[12] == 0x00) && (b[13] == 0x07))
#define SENSE_IS_END_OF_MEDIA(b)      (((b[2] & 0x4F) == 0x4D) && (b[12] == 0x00) && (b[13] == 0x02))
#define SENSE_IS_BAD_ATTRIBID(b)      (((b[2] & 0x0F) == 0x05) && (b[12] == 0x24) && (b[13] == 0x00) && (b[15] == 0xCF))
#define SENSE_IS_UNIT_ATTENTION(b)    ( (b[2] & 0x0F) == 0x06                                       )
#define SENSE_HAS_ILI_SET(b)          ( (b[2] & 0x20) == 0x20                                       )
#define SENSE_IS_MODE_PARAMETER_ROUNDED(b)   ((b[2] == 0x01)  && (b[12] == 0x37) && (b[13] == 0x00))
#define SENSE_IS_MEDIA_NOT_LOGICALLY_LOADED(b) (((b[2] & 0x0F) == 0x02) && (b[12] == 0x04) && (b[13] == 0x02))

/*
 * Define the maximum transfer size we will support by default,
 *  and also an "unlimited" size (which is actually limited, to the realistic max)
 */
#ifdef __NetBSD__
#define LTOTAPE_MAX_TRANSFER_SIZE  MAXPHYS
#define LTOTAPE_OS_LIMITED_SIZE   MAXPHYS
#else
#define LTOTAPE_MAX_TRANSFER_SIZE  512*1024   /* 512kB */
#define LTOTAPE_OS_LIMITED_SIZE   1024*1024    /*  1MB  */
#endif

/*
 * Tape medium type identifiers, comprised of the density code + WORM flag:
 */
#define LTOMEDIATYPE_LTO8RW     0x005E
#define LTOMEDIATYPE_LTO8WORM   0x015E
#define LTOMEDIATYPE_LTO8TYPEM  0x005D
#define LTOMEDIATYPE_LTO7RW     0x005C
#define LTOMEDIATYPE_LTO7WORM   0x015C
#define LTOMEDIATYPE_LTO6RW     0x005A
#define LTOMEDIATYPE_LTO6WORM   0x015A
#define LTOMEDIATYPE_LTO5RW     0x0058
#define LTOMEDIATYPE_LTO5WORM   0x0158
#define LTOMEDIATYPE_LTO4RW     0x0046
#define LTOMEDIATYPE_LTO4WORM   0x0146
#define LTOMEDIATYPE_LTO3RW     0x0044
#define LTOMEDIATYPE_LTO3WORM   0x0144

#endif /* __ltotape_h */
