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
** FILE NAME:       tape_drivers/vendor_compat.h
**
** DESCRIPTION:     Function prototypes of vendor unique features
**
** AUTHOR:          Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/
#include "tape_drivers/spc_op_codes.h"
#include "tape_drivers/ssc_op_codes.h"
#include "tape_drivers/tape_drivers.h"

/* Supported vendors */
#include "tape_drivers/ibm_tape.h"
#include "tape_drivers/hp_tape.h"

#ifndef __vendor_compat_h

#define __vendor_compat_h

#ifdef __cplusplus
extern "C" {
#endif

extern struct error_table standard_tape_errors[];

int  get_vendor_id(char* vendor);
struct supported_device **get_supported_devs(int vendor);
bool drive_has_supported_fw(int vendor, int drive_type, const unsigned char * const revision);
int  is_supported_tape(unsigned char type, unsigned char density, bool *is_worm);

void init_error_table(int vendor,
					  struct error_table **standard_table,
					  struct error_table **vendor_table);

int  init_timeout(int vendor, struct timeout_tape **table, int type);
void destroy_timeout(struct timeout_tape **table);
int  get_timeout(struct timeout_tape *table, int op_code);

#ifdef __cplusplus
}
#endif

#endif // __vendor_compat_h
