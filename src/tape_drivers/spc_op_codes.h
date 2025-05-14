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
** FILE NAME:       tape_drivers/spc_opcodes.h
**
** DESCRIPTION:     Definitions of SCSI Primary Command (SPC) Operation Codes
**
** AUTHOR:          Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/

#ifndef __spc_op_codes_h

#define __spc_op_codes_h

#ifdef __cplusplus
extern "C" {
#endif

#define XCOPY COPY_OPERATION_ABORT

enum spc_codes {
	CHANGE_DEFINITION          = 0x40,
	XCOPY                      = 0x83,
	INQUIRY                    = 0x12,
	LOG_SELECT                 = 0x4C,
	LOG_SENSE                  = 0x4D,
	MODE_SELECT6               = 0x15,
	MODE_SELECT10              = 0x55,
	MODE_SENSE6                = 0x1A,
	MODE_SENSE10               = 0x5A,
	PERSISTENT_RESERVE_IN      = 0x5E,
	PERSISTENT_RESERVE_OUT     = 0x5F,
	READ_ATTRIBUTE             = 0x8C,
	READ_BUFFER                = 0x3C,
	RECEIVE_DIAGNOSTIC_RESULTS = 0x1C,
	RELEASE_UNIT6              = 0x17,
	RELEASE_UNIT10             = 0x57,
	REPORT_LUNS                = 0xA0,
	REQUEST_SENSE              = 0x03,
	RESERVE_UNIT6              = 0x16,
	RESERVE_UNIT10             = 0x56,
	SPIN                       = 0xA2,
	SPOUT                      = 0xB5,
	SEND_DIAGNOSTIC            = 0x1D,
	TEST_UNIT_READY            = 0x00,
	WRITE_ATTRIBUTE            = 0x8D,
	WRITE_BUFFER               = 0x3B,
	THIRD_PARTY_COPY_IN        = 0x84,
	MAINTENANCE_IN             = 0xA3,
	MAINTENANCE_OUT            = 0xA4,
};

#ifdef __cplusplus
}
#endif

#endif // __spc_op_codes_h
