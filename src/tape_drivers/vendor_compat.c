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
** FILE NAME:       tape_drivers/vendor_compat.c
**
** DESCRIPTION:     Function of vendor unique features (Compatibility layer)
**
** AUTHOR:          Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/

#include "tape_drivers/ibm_tape.h"
#include "tape_drivers/hp_tape.h"
#include "tape_drivers/quantum_tape.h"
#include "libltfs/ltfs_endian.h"

#define DEFAULT_TIMEOUT (60)

/* Standard SCSI sense table */
struct error_table standard_tape_errors[] = {
	/* Sense Key 0 (No Sense) */
	{0x000000, -EDEV_NO_SENSE,                  "No Additional Sense Information"},
	{0x000001, -EDEV_FILEMARK_DETECTED,         "Filemark Detected"},
	{0x000002, -EDEV_EARLY_WARNING,             "End-of-Partition/Medium Detected (Early Warning)"},
	{0x000004, -EDEV_BOP_DETECTED,              "Beginning-of-Partition/Medium Detected"},
	{0x000007, -EDEV_PROG_EARLY_WARNING,        "End-of-Partition/Medium Detected (Programable Early Warning)"},
	{0x000016, -EDEV_OPERATION_IN_PROGRESS,     "Operation in Progress"},
	{0x000017, -EDEV_CLEANING_REQUIRED,         "Cleaning Required"},
	{0x000018, -EDEV_OPERATION_IN_PROGRESS,     "Erase Operation in Progress"},
	{0x001401, -EDEV_RECORD_NOT_FOUND,          "Record Not Found (String Search)"},
	{0x002E00, -EDEV_INSUFFICIENT_TIME,         "Insufficient Time For Operation (String Search)"},
	{0x003003, -EDEV_CLEANING_CART,             "Cleaning tape installed"},
	/* Sense Key 1 (Recovered Error) */
	{0x010000, -EDEV_RECOVERED_ERROR,           "No Additional Sense Information"},
	{0x010017, -EDEV_CLEANING_REQUIRED,         "Drive Needs Cleaning"},
	{0x010A00, -EDEV_RECOVERED_ERROR,           "Error log overflow"},
	{0x010C00, -EDEV_RECOVERED_ERROR,           "Write Error: A write error occurred, but was recovered."},
	{0x011100, -EDEV_RECOVERED_ERROR,           "Read Error: A read error occurred, but was recovered."},
	{0x011701, -EDEV_RECOVERED_ERROR,           "Recovered Data with Retries"},
	{0x011800, -EDEV_RECOVERED_ERROR,           "Recovered Data with Error Correction Applied"},
	{0x013700, -EDEV_MODE_PARAMETER_ROUNDED,    "Mode Parameters Rounded"},
	{0x014700, -EDEV_RECOVERED_ERROR,           "SCSI parity error"},
	{0x015B02, -EDEV_RECOVERED_ERROR,           "Log counter at maximum"},
	{0x015D00, -EDEV_RECOVERED_ERROR,           "Failure Prediction Threshold Exceeded"},
	{0x015DFF, -EDEV_RECOVERED_ERROR,           "Failure Prediction Threshold Exceeded (FALSE)"},
	{0x01EF13, -EDEV_RECOVERED_ERROR,           "Encryption - Key Translate"},
	/* Sense Key 2 (Not Ready) */
	{0x020017, -EDEV_CLEANING_IN_PROGRESS,      "Drive cleaning requested"},
	{0x020400, -EDEV_NOT_REPORTABLE,            "Logical Unit Not Ready, Cause Not Reportable"},
	{0x020401, -EDEV_BECOMING_READY,            "Logical Unit Is in Process of Becoming Ready"},
	{0x020402, -EDEV_NEED_INITIALIZE,           "Initializing Command Required"},
	{0x020403, -EDEV_NO_MEDIUM,                 "Logical Unit Not Ready, Manual Intervention Required"},
	{0x020404, -EDEV_OPERATION_IN_PROGRESS,     "Logical Unit Not Ready, Format in Progress"},
	{0x020407, -EDEV_OPERATION_IN_PROGRESS,     "Operation in progress"},
	{0x020412, -EDEV_OFFLINE,                   "Logical Unit Not Ready, Offline"},
	{0x020413, -EDEV_OPERATION_IN_PROGRESS,     "Logical Unit Not Ready, SA Creation in Progress"},
	{0x020B01, -EDEV_OVER_TEMPERATURE,          "Warning - Specified Temperature Exceeded"},
	{0x023003, -EDEV_CLEANING_IN_PROGRESS,      "Cleaning Cartridge Installed"},
	{0x023007, -EDEV_NOT_READY,                 "Cleaning Failure"},
	{0x023A00, -EDEV_NO_MEDIUM,                 "Medium Not Present"},
	{0x023A02, -EDEV_IE_OPEN,                   "Medium Not Present - Tray Open"},
	{0x023A04, -EDEV_NO_MEDIUM,                 "Not Ready - Medium Auxiliary Memory Accessible"},
	{0x023B12, -EDEV_DOOR_OPEN,                 "Magazine removed"},
	{0x023E00, -EDEV_NOT_SELF_CONFIGURED_YET,   "Logical Unit Has Not Self-configured"},
	{0x025300, -EDEV_LOAD_UNLOAD_ERROR,         "Media Load or Eject Failed"},
	{0x027411, -EDEV_PARAMETER_VALUE_REJECTED,  "SA Creation Parameter Value Rejected"},
	/* Sense Key 3 (Medium Error) */
	{0x030302, -EDEV_WRITE_PERM,                "Excessive Write Errors"},
	{0x030410, -EDEV_CM_PERM,                   "Logical Unit Not Ready, Auxiliary Memory Not Accessible"},
	{0x030900, -EDEV_RW_PERM,                   "Track Following Error (Servo)"},
	{0x030C00, -EDEV_WRITE_PERM,                "Write Error"},
	{0x031100, -EDEV_READ_PERM,                 "Unrecovered Read Error"},
	{0x031101, -EDEV_READ_PERM,                 "Read Retries Exhausted"},
	{0x031108, -EDEV_READ_PERM,                 "Incomplete Block Read"},
	{0x031112, -EDEV_CM_PERM,                   "Auxiliary Memory Read Error"},
	{0x031400, -EDEV_RW_PERM,                   "Recorded Entity Not Found"},
	{0x031401, -EDEV_RW_PERM,                   "Record Not Found"},
	{0x031402, -EDEV_RW_PERM,                   "Filemark or Setmark Not Found"},
	{0x031403, -EDEV_RW_PERM,                   "End-of-Data Not Found"},
	{0x031404, -EDEV_MEDIUM_ERROR,              "Block Sequence Error"},
	{0x033000, -EDEV_MEDIUM_FORMAT_ERROR,       "Incompatible Medium Installed"},
	{0x033001, -EDEV_MEDIUM_FORMAT_ERROR,       "Cannot Read Medium, Unknown Format"},
	{0x033002, -EDEV_MEDIUM_FORMAT_ERROR,       "Cannot Read Medium, Incompatible Format"},
	{0x033003, -EDEV_MEDIUM_FORMAT_ERROR,       "Cleaning tape installed"},
	{0x033007, -EDEV_CLEANING_FALIURE,          "Cleaning failure"},
	{0x03300D, -EDEV_MEDIUM_ERROR,              "Medium Error/WORM Medium"},
	{0x033100, -EDEV_MEDIUM_FORMAT_CORRUPTED,   "Medium Format Corrupted"},
	{0x033101, -EDEV_MEDIUM_ERROR,              "Format Command Failed"},
	{0x033300, -EDEV_MEDIUM_ERROR,              "Tape Length Error"},
	{0x033B00, -EDEV_RW_PERM,                   "Sequential Positioning Error"},
	{0x035000, -EDEV_RW_PERM,                   "Write Append Error"},
	{0x035100, -EDEV_MEDIUM_ERROR,              "Erase Failure"},
	{0x035200, -EDEV_RW_PERM,                   "Cartridge Fault"},
	{0x035300, -EDEV_LOAD_UNLOAD_ERROR,         "Media Load or Eject Failed"},
	{0x035304, -EDEV_LOAD_UNLOAD_ERROR,         "Medium Thread or Unthread Failure"},
	/* Sense Key 4 (Hardware or Firmware Error) */
	{0x040302, -EDEV_HARDWARE_ERROR,            "Open Writer Failure"},
	{0x040403, -EDEV_HARDWARE_ERROR,            "Manual Intervention Required"},
	{0x040801, -EDEV_HARDWARE_ERROR,            "Logical Unit Communication Failure"},
	{0x040900, -EDEV_HARDWARE_ERROR,            "Track Following Error"},
	{0x041001, -EDEV_LBP_WRITE_ERROR,           "Logical Block Guard Check Failed"},
	{0x041004, -EDEV_HARDWARE_ERROR,            "Logical Block Protection Error On Recover Buffered Data"},
	{0x041501, -EDEV_HARDWARE_ERROR,            "Machanical Position Error"},
	{0x043B00, -EDEV_HARDWARE_ERROR,            "Sequential Positioning Error"},
	{0x043B08, -EDEV_HARDWARE_ERROR,            "Reposition Error"},
	{0x043B0D, -EDEV_HARDWARE_ERROR,            "Medium Destination Element Full"},
	{0x043B0E, -EDEV_HARDWARE_ERROR,            "Medium Source Element Empty"},
	{0x043F0F, -EDEV_HARDWARE_ERROR,            "Echo buffer overwritten"},
	{0x044000, -EDEV_HARDWARE_ERROR,            "Diagnostic Failure"},
	{0x044100, -EDEV_HARDWARE_ERROR,            "Data Path Failure"},
	{0x044400, -EDEV_HARDWARE_ERROR,            "Internal Target Failure"},
	{0x044C00, -EDEV_HARDWARE_ERROR,            "Logical Unit Failed Self-Configuration"},
	{0x045100, -EDEV_HARDWARE_ERROR,            "Erase Failure"},
	{0x045200, -EDEV_HARDWARE_ERROR,            "Cartridge Fault"},
	{0x045300, -EDEV_HARDWARE_ERROR,            "Media Load or Eject Failed"},
	{0x045301, -EDEV_HARDWARE_ERROR,            "A drive did not unload a cartridge."},
	{0x045304, -EDEV_HARDWARE_ERROR,            "Medium Thread or Unthread Failure"},
	/* Sense Key 5 (Illegal Request) */
	{0x050E03, -EDEV_ILLEGAL_REQUEST,           "Invalid Field in Command Information Unit (e.g., FCP_DL error)"},
	{0x051A00, -EDEV_ILLEGAL_REQUEST,           "Parameter List Length Error"},
	{0x052000, -EDEV_ILLEGAL_REQUEST,           "Invalid Command Operation Code"},
	{0x05200C, -EDEV_ILLEGAL_REQUEST,           "Illegal Command When Not In Append-Only Mode"},
	{0x052101, -EDEV_INVALID_ADDRESS,           "Invalid Element Address"},
	{0x052400, -EDEV_INVALID_FIELD_CDB,         "Invalid Field in CDB"},
	{0x052500, -EDEV_ILLEGAL_REQUEST,           "Logical Unit Not Supported"},
	{0x052600, -EDEV_ILLEGAL_REQUEST,           "Invalid Field in Parameter List"},
	{0x052601, -EDEV_ILLEGAL_REQUEST,           "Parameter list error: parameter not supported"},
	{0x052602, -EDEV_ILLEGAL_REQUEST,           "Parameter value invalid"},
	{0x052603, -EDEV_ILLEGAL_REQUEST,           "Threshold Parameters Not Supported"},
	{0x052604, -EDEV_ILLEGAL_REQUEST,           "Invalid release of persistent reservation"},
	{0x052611, -EDEV_ILLEGAL_REQUEST,           "Encryption - Incomplete Key-Associate Data Set"},
	{0x052612, -EDEV_ILLEGAL_REQUEST,           "Vendor Specific Key Reference Not Found"},
	{0x052690, -EDEV_ILLEGAL_REQUEST,           "Wrong firmware image, does not fit boot code"},
	{0x052691, -EDEV_ILLEGAL_REQUEST,           "Wrong personality firmware image"},
	{0x052693, -EDEV_ILLEGAL_REQUEST,           "Wrong firmware image, checksum error"},
	{0x052904, -EDEV_ILLEGAL_REQUEST,           "Device Internal Reset"},
	{0x052C00, -EDEV_ILLEGAL_REQUEST,           "Command Sequence Error"},
	{0x052C0B, -EDEV_ILLEGAL_REQUEST,           "Not Reserved"},
	{0x053000, -EDEV_ILLEGAL_REQUEST,           "Incompatible Medium Installed"},
	{0x053005, -EDEV_ILLEGAL_REQUEST,           "Cannot Write Medium - Incompatible Format"},
	{0x053900, -EDEV_ILLEGAL_REQUEST,           "Saving Parameters Not Supported"},
	{0x053B00, -EDEV_ILLEGAL_REQUEST,           "Sequential Positioning Error"},
	{0x053B0C, -EDEV_ILLEGAL_REQUEST,           "Position Past Beginning of Medium"},
	{0x053B0D, -EDEV_DEST_FULL,                 "Medium Destination Element Full"},
	{0x053B0E, -EDEV_SRC_EMPTY,                 "Medium Source Element Empty"},
	{0x053B11, -EDEV_MAGAZINE_INACCESSIBLE,     "Medium magazine not accessible"},
	{0x053B12, -EDEV_MAGAZINE_INACCESSIBLE,     "Media magazine not installed."},
	{0x053D00, -EDEV_ILLEGAL_REQUEST,           "Invalid Bits in Identify Message"},
	{0x054900, -EDEV_ILLEGAL_REQUEST,           "Invalid Message Error"},
	{0x055301, -EDEV_MEDIUM_LOCKED,            "A drive did not unload a cartridge."},
	{0x055302, -EDEV_MEDIUM_LOCKED,             "Medium Removal Prevented"},
	{0x055303, -EDEV_MEDIUM_LOCKED,             "Drive media removal prevented state set"},
	{0x055508, -EDEV_ILLEGAL_REQUEST,           "Maximum Number of Supplemental Decryption Keys Exceeded"},
	{0x055B03, -EDEV_ILLEGAL_REQUEST,           "Log List Codes Exhausted"},
	{0x057408, -EDEV_ILLEGAL_REQUEST,           "Digital Signature Validation Failure"},
	{0x05740C, -EDEV_ILLEGAL_REQUEST,           "Unable to Decrypt Parameter List"},
	{0x057410, -EDEV_ILLEGAL_REQUEST,           "SA Creation Parameter Value Invalid"},
	{0x057411, -EDEV_ILLEGAL_REQUEST,           "SA Creation Parameter Value Rejected"},
	{0x057412, -EDEV_ILLEGAL_REQUEST,           "Invalid SA Usage"},
	{0x057430, -EDEV_ILLEGAL_REQUEST,           "SA Creation Parameter not Supported"},
	/* Sense Key 6 (Unit Attention) */
	{0x060002, -EDEV_EARLY_WARNING,             "End-of-Partition/Medium Detected, Early Warning"},
	{0x062800, -EDEV_MEDIUM_MAY_BE_CHANGED,     "Not Ready to Ready Transition, Medium May Have Changed"},
	{0x062801, -EDEV_IE_ACCESSED,               "Import or Export Element Accessed"},
	{0x062900, -EDEV_POR_OR_BUS_RESET,          "Power On, Reset, or Bus Device Reset Occurred"},
	{0x062901, -EDEV_POR_OR_BUS_RESET,          "Power on occurred"},
	{0x062902, -EDEV_POR_OR_BUS_RESET,          "SCSI Bus reset occurred"},
	{0x062903, -EDEV_POR_OR_BUS_RESET,          "Internal reset occurred"},
	{0x062904, -EDEV_POR_OR_BUS_RESET,          "Internal reset occurred"},
	{0x062905, -EDEV_UNIT_ATTENTION,            "Transceiver Mode Changed To Single-ended"},
	{0x062906, -EDEV_UNIT_ATTENTION,            "Transceiver Mode Changed To LVD"},
	{0x062A01, -EDEV_CONFIGURE_CHANGED,         "Mode Parameters Changed"},
	{0x062A02, -EDEV_CONFIGURE_CHANGED,         "Mode Parameters Changed"},
	{0x062A03, -EDEV_RESERVATION_PREEMPTED,     "Reservations preempted"},
	{0x062A04, -EDEV_RESERVATION_RELEASED,      "Reservations released"},
	{0x062A05, -EDEV_REGISTRATION_PREEMPTED,    "Registrations preempted"},
	{0x062A10, -EDEV_TIME_STAMP_CHANGED,        "Time stamp changed"},
	{0x062A11, -EDEV_CRYPTO_ERROR,              "Encryption - Data Encryption Parameters Changed by Another I_T Nexus"},
	{0x062A12, -EDEV_CRYPTO_ERROR,              "Encryption - Data Encryption Parameters Changed by Vendor Specific Event"},
	{0x062A14, -EDEV_UNIT_ATTENTION,            "SA Creation Capabilities Data Has Changed"},
	{0x062F00, -EDEV_COMMAND_CLEARED,           "Commands Cleared by Another Initiator"},
	{0x063000, -EDEV_MEDIUM_ERROR,              "Incompatible Medium Installed"},
	{0x063B12, -EDEV_DOOR_CLOSED,               "Medium magazine removed"},
	{0x063B13, -EDEV_DOOR_CLOSED,               "Medium magazine inserted"},
	{0x063F01, -EDEV_CONFIGURE_CHANGED,         "Microcode Has Been Changed"},
	{0x063F02, -EDEV_CONFIGURE_CHANGED,         "Changed Operating Definition"},
	{0x063F03, -EDEV_CONFIGURE_CHANGED,         "Inquiry Data Has Changed"},
	{0x063F05, -EDEV_CONFIGURE_CHANGED,         "Device Identifier Changed"},
	{0x063F0E, -EDEV_CONFIGURE_CHANGED,         "Reported LUNs Data Has Changed"},
	{0x065302, -EDEV_MEDIA_REMOVAL_PREV,        "Media removal prevented"},
	{0x065A01, -EDEV_MEDIUM_REMOVAL_REQ,        "Operator Medium Removal Request"},
	/* Sense Key 7 (Data Protect) */
	{0x072610, -EDEV_CRYPTO_ERROR,              "Encryption - Data Decryption Key Fail Limit"},
	{0x072700, -EDEV_WRITE_PROTECTED,           "Write Protected"},
	{0x072A13, -EDEV_CRYPTO_ERROR,              "Encryption - Data Encryption Key Instance Counter Has Changed"},
	{0x073005, -EDEV_DATA_PROTECT,              "Cannot Write Medium, Incompatible Format"},
	{0x073000, -EDEV_WRITE_PROTECTED_WORM,      "Data Protect/WORM Medium"},
	{0x07300C, -EDEV_WRITE_PROTECTED_WORM,      "Data Protect/WORM Medium - Overwrite Attempted"},
	{0x07300D, -EDEV_WRITE_PROTECTED_WORM,      "Data Protect/WORM Medium - Integrity Check"},
	{0x075001, -EDEV_WRITE_PROTECTED_WORM,      "Write Append Position Error (WORM)"},
	{0x075200, -EDEV_DATA_PROTECT,              "Cartridge Fault"},
	{0x075A02, -EDEV_WRITE_PROTECTED_OPERATOR,  "Data Protect/Operator - Overwrite Attempted"},
	{0x077400, -EDEV_WRITE_PROTECTED_WORM,      "Security Error"},
	{0x077401, -EDEV_CRYPTO_ERROR,              "Encryption - Unable to Decrypt Data"},
	{0x077402, -EDEV_CRYPTO_ERROR,              "Encryption - Unencrypted Data Encountered While Decrypting"},
	{0x077403, -EDEV_CRYPTO_ERROR,              "Encryption - Incorrect Data Encryption Key"},
	{0x077404, -EDEV_CRYPTO_ERROR,              "Encryption - Cryptographic Integrity Validation Failed"},
	{0x077405, -EDEV_CRYPTO_ERROR,              "Encryption - Error Decrypting Data"},
	/* Sense Key 8 (Blank Check) */
	{0x080005, -EDEV_EOD_DETECTED,              "End-of-Data (EOD) Detected"},
	{0x081401, -EDEV_RECORD_NOT_FOUND,          "Record Not Found, Void Tape"},
	{0x081403, -EDEV_EOD_NOT_FOUND,             "End-of-Data (EOD) not found"},
	{0x080B01, -EDEV_OVER_TEMPERATURE,          "The drive detected an overtemperature condition."},
	/* Sense Key B (Aborted Command) */
	{0x0B0E01, -EDEV_ABORTED_COMMAND,           "Information Unit Too Short"},
	{0x0B1400, -EDEV_ABORTED_COMMAND,           "Recorded Entity Not Found"},
	{0x0B1401, -EDEV_ABORTED_COMMAND,           "Record Not Found"},
	{0x0B1402, -EDEV_ABORTED_COMMAND,           "Filemark or Setmark Not Found"},
	{0x0B1B00, -EDEV_ABORTED_COMMAND,           "Synchronous Data Transfer Error"},
	{0x0B3D00, -EDEV_ABORTED_COMMAND,           "Invalid Bits in Identify Message"},
	{0x0B3F0F, -EDEV_ABORTED_COMMAND,           "Echo Buffer Overwritten"},
	{0x0B4100, -EDEV_ABORTED_COMMAND,           "LDI command Failure"},
	{0x0B4300, -EDEV_ABORTED_COMMAND,           "Message Error"},
	{0x0B4400, -EDEV_ABORTED_COMMAND,           "Internal Target Failure"},
	{0x0B4500, -EDEV_ABORTED_COMMAND,           "Select/Reselect Failure"},
	{0x0B4700, -EDEV_ABORTED_COMMAND,           "SCSI Parity Error"},
	{0x0B4703, -EDEV_ABORTED_COMMAND,           "Information Unit iuCRC Error Detected"},
	{0x0B4800, -EDEV_ABORTED_COMMAND,           "Initiator Detected Error Message Received"},
	{0x0B4900, -EDEV_ABORTED_COMMAND,           "Invalid Message Error"},
	{0x0B4A00, -EDEV_ABORTED_COMMAND,           "Command Phase Error"},
	{0x0B4B00, -EDEV_ABORTED_COMMAND,           "Data Phase Error"},
	{0x0B4B02, -EDEV_ABORTED_COMMAND,           "Too Much Write Data"},
	{0x0B4B03, -EDEV_ABORTED_COMMAND,           "ACK/NAK Timeout"},
	{0x0B4B04, -EDEV_ABORTED_COMMAND,           "NAK Received"},
	{0x0B4B05, -EDEV_ABORTED_COMMAND,           "Data Offset Error"},
	{0x0B4B06, -EDEV_TIMEOUT,                   "Initiator Response Timeout"},
	{0x0B4E00, -EDEV_OVERLAPPED,                "Overlapped Commands"},
	{0x0B0801, -EDEV_ABORTED_COMMAND,           "LU Communication - Timeout"},

	/* Sense Key D (Volume Overflow) */
	{0x0D0002, -EDEV_OVERFLOW,                  "End-of-Partition/Medium Detected"},
	/* END MARK*/
	{0xFFFFFF, -EDEV_UNKNOWN,                   "Unknown Error code"},
};

int get_vendor_id(char* vendor)
{
	if (!strncmp(vendor, IBM_VENDOR_ID, strlen(IBM_VENDOR_ID)))
		return VENDOR_IBM;
	else if (!strncmp(vendor, HP_VENDOR_ID, strlen(HP_VENDOR_ID)))
		return VENDOR_HP;
	else if (!strncmp(vendor, HPE_VENDOR_ID, strlen(HPE_VENDOR_ID)))
		return VENDOR_HP;
	else if (!strncmp(vendor, QUANTUM_VENDOR_ID, strlen(QUANTUM_VENDOR_ID)))
		return VENDOR_QUANTUM;
	else
		return VENDOR_UNKNOWN;
}

struct supported_device **get_supported_devs(int vendor)
{
	struct supported_device **cur = NULL;

	switch (vendor) {
		case VENDOR_IBM:
			cur = ibm_supported_drives;
			break;
		case VENDOR_HP:
			cur = hp_supported_drives;
			break;
		case VENDOR_QUANTUM:
			cur = quantum_supported_drives;
			break;
	}

	return cur;
}

bool drive_has_supported_fw(int vendor, int drive_type, const unsigned char * const revision)
{
	bool ret = false;

	switch (vendor) {
		case VENDOR_IBM:
			ret = ibm_tape_is_supported_firmware(drive_type, revision);
			break;
		default:
			ret = true;
			break;
	}

	return ret;
}

unsigned char assume_cart_type(const unsigned char dc)
{
	unsigned char cart = 0x00;

	switch (dc) {
		case TC_DC_LTO5:
			cart = TC_MP_LTO5D_CART;
			break;
		case TC_DC_LTO6:
			cart = TC_MP_LTO6D_CART;
			break;
		case TC_DC_LTO7:
			cart = TC_MP_LTO7D_CART;
			break;
		case TC_DC_LTOM8:
			cart = TC_MP_LTO7D_CART;
			break;
		case TC_DC_LTO8:
			cart = TC_MP_LTO8D_CART;
			break;
		case TC_DC_LTO9:
			cart = TC_MP_LTO9D_CART;
			break;
		default:
			// Do nothing
			break;
	}

	return cart;
}

int is_supported_tape(unsigned char type, unsigned char density, bool *is_worm)
{
	int ret = -LTFS_UNSUPPORTED_MEDIUM, i;

	for (i = 0; i < num_supported_cart; ++i) {
		if(type == supported_cart[i]) {
			if(IS_WORM_MEDIUM(type)) {
				/* Detect WORM cartridge */
				ltfsmsg(LTFS_DEBUG, 39809D);
				*is_worm = true;
			}
			ret = 0;
			break;
		}
	}

	if (!ret) {
		ret = -LTFS_UNSUPPORTED_MEDIUM;
		for (i = 0; i < num_supported_density; ++i) {
			if(density == supported_density[i]) {
				ret = 0;
				break;
			}
		}
	}

	return ret;
}

void init_error_table(int vendor,
					  struct error_table **standard_table,
					  struct error_table **vendor_table)
{
	*standard_table = standard_tape_errors;

	switch (vendor) {
		case VENDOR_IBM:
			*vendor_table = ibm_tape_errors;
			break;
		case VENDOR_HP:
			*vendor_table = hp_tape_errors;
			break;
		case VENDOR_QUANTUM:
			*vendor_table = quantum_tape_errors;
			break;
	}
}

int init_timeout(int vendor, struct timeout_tape **table, int type)
{
	int ret = -EDEV_UNKNOWN;

	switch (vendor) {
		case VENDOR_IBM:
			ret = ibm_tape_init_timeout(table, type);
			break;
		case VENDOR_HP:
			ret = hp_tape_init_timeout(table, type);
			break;
		case VENDOR_QUANTUM:
			ret = quantum_tape_init_timeout(table, type);
			break;
	}

	return ret;
}

void destroy_timeout(struct timeout_tape** table)
{
	struct timeout_tape *entry, *tmp;

	HASH_ITER(hh, *table, entry, tmp) {
		HASH_DEL(*table, entry);
		free(entry);
	}
}

int get_timeout(struct timeout_tape* table, int op_code)
{
	struct timeout_tape *out = NULL;

	if (!table) {
		ltfsmsg(LTFS_WARN, 39802W, op_code);
		return DEFAULT_TIMEOUT;
	}

	HASH_FIND_INT(table, &op_code, out);

	if (out) {
		if (out->timeout == -1) {
			ltfsmsg(LTFS_WARN, 39800W, op_code);
			return -1;
		} else {
			ltfsmsg(LTFS_DEBUG3, 39801D, op_code, out->timeout);
			return out->timeout;
		}
	} else {
		ltfsmsg(LTFS_WARN, 39805W, op_code);
		return DEFAULT_TIMEOUT;
	}
}
