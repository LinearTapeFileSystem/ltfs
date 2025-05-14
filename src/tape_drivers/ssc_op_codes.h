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
** FILE NAME:       tape_drivers/ssc_opcodes.h
**
** DESCRIPTION:     Definitions of SCSI Stream Command (SSC) Operation Codes
**
** AUTHOR:          Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/

#ifndef __ssc_op_codes_h

#define __ssc_op_codes_h

#ifdef __cplusplus
extern "C" {
#endif

enum ssc_codes {
	ALLOW_OVERWRITE                 = 0x82,
	DISPLAY_MESSAGE                 = 0xC0,
	ERASE                           = 0x19,
	FORMAT_MEDIUM                   = 0x04,
	LOAD_UNLOAD                     = 0x1B,
	LOCATE10                        = 0x2B,
	LOCATE16                        = 0x92,
	PREVENT_ALLOW_MEDIUM_REMOVAL    = 0x1E,
	READ                            = 0x08,
	READ_BLOCK_LIMITS               = 0x05,
	READ_DYNAMIC_RUNTIME_ATTRIBUTE  = 0xD1,
	READ_POSITION                   = 0x34,
	READ_REVERSE                    = 0x0F,
	RECOVER_BUFFERED_DATA           = 0x14,
	REPORT_DENSITY_SUPPORT          = 0x44,
	REWIND                          = 0x01,
	SET_CAPACITY                    = 0x0B,
	SPACE6                          = 0x11,
	SPACE16                         = 0x91,
	STRING_SEARCH                   = 0xE3,
	VERIFY                          = 0x13,
	WRITE                           = 0x0A,
	WRITE_DYNAMIC_RUNTIME_ATTRIBUTE = 0xD2,
	WRITE_FILEMARKS6                = 0x10,
};

#ifdef __cplusplus
}
#endif

#endif // __ssc_op_codes_h
