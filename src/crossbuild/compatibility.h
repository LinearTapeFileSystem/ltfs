/*************************************************************************************
**  OO_Copyright_BEGIN
**
**
**  Copyright 2024 IBM Corp. All rights reserved.
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
*************************************************************************************
**
** Module Name:
**
**  crossbuild/compatibility.h
**
** Abstract:
**
**  This include file has macros to ensure compatibility on cross-platform (UnixLike-Windows) builds.
**
** Author(s):
**
**  Gustavo Padilla Valdez
**  IBM Mexico Software Laboratory
**	gustavo.padilla@ibm.com
**
** Revision History:
**
**  10/25/2024      --- GPV ---  File created 
*************************************************************************************
*/

#ifndef COMPATIBILITY_H
#define COMPATIBILITY_H

#ifdef __cplusplus
extern "C" {
#endif


// For Windows
#ifdef mingw_PLATFORM	
#include <unicode/umachine.h>

	#ifndef SHARE_FLAG_DENYNO
		#define SHARE_FLAG_DENYNO  _SH_DENYNO
	#endif

	#ifndef SHARE_FLAG_DENYWR
		#define SHARE_FLAG_DENYWR   _SH_DENYWR
	#endif

	#ifndef SHARE_FLAG_DENYRD
		#define SHARE_FLAG_DENYRD   _SH_DENYRD
	#endif

	#ifndef SHARE_FLAG_DENYRW
		#define SHARE_FLAG_DENYRW   _SH_DENYRW
	#endif

	#ifndef PERMISSION_READWRITE
		#define PERMISSION_READWRITE   _S_IREAD | _S_IWRITE
	#endif

	#ifndef PERMISSION_READ
		#define PERMISSION_READ  _S_IREAD
	#endif

	#ifndef PERMISSION_WRITE
		#define PERMISSION_WRITE  _S_IWRITE
	#endif

	#ifndef S_IFLNK
		#define S_IFLNK 0xA000 
	#endif

	#ifndef O_NONBLOCK
		#define O_NONBLOCK 0
	#endif	
	#ifndef INVALID_KEY
		#define INVALID_KEY UINT_MAX
	#endif



#endif	

// For Unix-Like
#ifndef COMPAT_UCHAR
	#define COMPAT_UCHAR UChar
#endif

#ifndef SHARE_FLAG_DENYNO
	#define SHARE_FLAG_DENYNO   0 // No deny (shared access)
#endif

#ifndef SHARE_FLAG_DENYWR
	#define SHARE_FLAG_DENYWR   (S_IWUSR | S_IWGRP) // Deny write access
#endif

#ifndef SHARE_FLAG_DENYRD
	#define SHARE_FLAG_DENYRD   (S_IRUSR | S_IRGRP | S_IROTH) // Deny read access
#endif

#ifndef SHARE_FLAG_DENYRW
	#define SHARE_FLAG_DENYRW   (S_IWUSR | S_IWGRP | S_IRUSR | S_IRGRP | S_IROTH) // Deny both read and write access
#endif

#ifndef PERMISSION_READWRITE
#define PERMISSION_READWRITE   0666
#endif

#ifndef PERMISSION_READ
#define PERMISSION_READ  0444
#endif

#ifndef PERMISSION_WRITE
#define PERMISSION_WRITE  0222
#endif

#ifndef INVALID_KEY
#define INVALID_KEY (-1U)
#endif
#ifdef __cplusplus
}
#endif

#endif // COMPATIBILITY_H