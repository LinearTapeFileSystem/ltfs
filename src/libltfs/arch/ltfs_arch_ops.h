/*************************************************************************************
**  OO_Copyright_BEGIN
**
**
**  Copyright 2025 IBM Corp. All rights reserved.
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
**  arch/ltfs_arch_ops.h
**
** Abstract:
**
**  LTFS specific platform methods version implementation
**
** Author(s):
**
**  Gustavo Padilla Valdez
**  IBM Mexico Software Laboratory
**  gustavo.padilla@ibm.com
**
** Revision History:
**
**  04/23/2025      --- GPV ---  ltfs_arch_ops created
*************************************************************************************
*/


#ifndef __LTFS_ARCH_OPS_H__
#define __LTFS_ARCH_OPS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
 
#define arch_safe_free(memobject)                               \
        do {                                                    \
            if(memobject)                                       \
            {                                                   \
                free(memobject);                                \
                memobject = NULL;                               \
            }                                                   \
        }while(0)

static inline void arch_strcpy_limited(char *dest, const char *src, int count)
    {
        int i;
        for (i = 0; i < (count) && (src)[i] != '\0'; i++) 
            (dest)[i] = (src)[i];
        if (i < (count)) 
            (dest)[i] = '\0';
    }



#ifdef _MSC_VER
#include <libxml/xmlmemory.h>
#include <corecrt_io.h>
#include <process.h>
#include <string.h>
#include <unicode/umachine.h>

#define SHARE_FLAG_DENYNO  _SH_DENYNO
#define SHARE_FLAG_DENYWR   _SH_DENYWR
#define SHARE_FLAG_DENYRD   _SH_DENYRD
#define SHARE_FLAG_DENYRW   _SH_DENYRW
#define PERMISSION_READWRITE   _S_IREAD | _S_IWRITE
#define PERMISSION_READ  _S_IREAD
#define PERMISSION_WRITE  _S_IWRITE
#define S_IFLNK 0xA000 
#define O_NONBLOCK 0
#define INVALID_KEY UINT_MAX


    #define arch_vsprintf   vsprintf_s

    #define arch_sprintf    sprintf_s

    #define arch_sscanf     sscanf_s

    #define arch_open       _sopen_s

    #define arch_fopen(file, mode, file_ptr) fopen_s(&(file_ptr), file, mode)

    #define arch_ctime(buf, time_ptr) ctime_s(buf, sizeof(buf), time_ptr)

    #define arch_getenv(buf, name) do { size_t len; _dupenv_s(&(buf), &(len), name);     } while (0)

    #define arch_strtok(str, delm, ctxt) strtok_s((str), (delm), &(ctxt))

    #define arch_strcpy(dest, size, src) strcpy_s((dest), (size), (src))

    #define arch_strncpy(dest, src, size, cnt) strncpy_s((dest), (size), (src), (cnt))

    #define arch_strcat(dest, size, src) strcat_s((dest), (size), (src))

    #define arch_unlink     _unlink

    #define arch_write      _write

    #define arch_close      _close

    #define arch_read       _read

    #define arch_strdup     _strdup

    #define arch_chmod      _chmod

    #define arch_getpid     _getpid

    #define arch_access     _access

    #define arch_xmlfree    xmlFree


#else
#define SHARE_FLAG_DENYNO   0	
#define SHARE_FLAG_DENYWR   (S_IWUSR | S_IWGRP)                                 // Deny write access
#define SHARE_FLAG_DENYRD   (S_IRUSR | S_IRGRP | S_IROTH)						// Deny read access
#define SHARE_FLAG_DENYRW   (S_IWUSR | S_IWGRP | S_IRUSR | S_IRGRP | S_IROTH)	// Deny both read and write access
#define PERMISSION_READWRITE   0666
#define PERMISSION_READ  0444
#define PERMISSION_WRITE  0222
#define INVALID_KEY (-1U)

    #define arch_vsprintf(buffer,unused, fmt, ...) vsprintf((buffer), (fmt), __VA_ARGS__)

    #define arch_sprintf(buffer,unused, fmt, ...) sprintf((buffer), (fmt), __VA_ARGS__)

    #define arch_sscanf     sscanf

    #define arch_open( descriptor_ptr, filename_ptr, open_flg, share_flg, unused) do{ *descriptor_ptr = open(filename_ptr, open_flg, share_flg); }while(0)

    #define arch_fopen(file, mode, file_ptr)  do {file_ptr = fopen(file, mode);}while(0)

    #define arch_ctime(buf ,time_ptr) do { buf = ctime(time_ptr); } while (0)

    #define arch_getenv(buf ,name) do {  buf = getenv(name); } while (0)

    #define arch_strcpy(dest, unused, src) ({if(unused || !unused) {strcpy(dest, src);}})

    #define arch_strncpy(dest, src, unused, cnt) strncpy(dest, src, cnt)

    #define arch_strcat(dest, unused, src)( {if(unused || !unused){ strcat(dest, src);}})

    #define arch_strtok(str, delim, unused) ((void)(unused), strtok(str, delim))

    #define arch_unlink     unlink

    #define arch_write      write

    #define arch_close      close

    #define arch_read       read

    #define arch_strdup     strdup

    #define arch_chmod      chmod

    #define arch_getpid     getpid

    #define arch_access     access
    
    #define arch_xmlfree    free


#endif /* _MSC_VER */

    /* These needs to be declared at the end to avoid redefinition and to avoid code replication */
    #define arch_vsprintf_auto( buffer,  fmt, ...) arch_vsprintf(buffer,sizeof(buffer),fmt,__VA_ARGS__)

    #define arch_strcpy_auto(dest, src) arch_strcpy(dest, sizeof(dest), src);

    #define arch_strncpy_auto(dest, src, destSize) arch_strncpy(dest, src, destSize, destSize);

    #define arch_strcat_auto(dest,src) arch_strcat(dest, sizeof(dest), src);

    #define arch_sprintf_auto(buffer, fmt, ...) arch_sprintf(buffer,sizeof(buffer),fmt, __VA_ARGS__)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __LTFS_ARCH_OPS_H__ */
