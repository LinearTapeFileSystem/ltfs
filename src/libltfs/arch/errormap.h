/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2017 IBM Corp.
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
** FILE NAME:       errormap.h
**
** DESCRIPTION:     Platform-specific error code mapping functions.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
*************************************************************************************
*/

#ifndef arch_error_h__
#define arch_error_h__

#ifdef __cplusplus
extern "C" {
#endif


/** Initialize the error map. Call this function before using the error map functions.
 * @return 0 on success or -LTFS_NO_MEMORY if memory allocation fails.
 */
int errormap_init();

/** Free the error map. Call this function when the error map is no longer needed. */
void errormap_finish();

/** Map a libltfs error code to the corresponding operating system error code.
 * @param val Error code to look up in the table. This value must be less than or equal to zero.
 * @return The mapped error code. If val "looks like" an OS error code already,
 *         i.e. abs(val) < LTFS_ERR_MIN, then val is returned unmodified. If val is not found in
 *         the error table, -EIO is returned.
 */
int errormap_fuse_error(int val);

/** Map a libltfs error code to the corresponding error message.
 * @param val Error code to look up in the table. This value must be less than or equal to zero.
 * @return pointer of the mapped message. if val is not found in the error table, NULL is returned.
 */
char* errormap_msg_id(int val);

#ifdef __cplusplus
}
#endif

#endif /* arch_error_h__ */
