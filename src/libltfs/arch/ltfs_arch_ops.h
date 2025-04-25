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
#include <corecrt_io.h>
#include <process.h>
#include <time.h>
#include <libxml/xmlmemory.h>
 
#define arch_safe_free(memobject)                               \
        do {                                                    \
            if(memobject)                                       \
            {                                                   \
                free(memobject);                                \
                memobject = NULL;                               \
            }                                                   \
        }while(0)

    inline void arch_strcpy_limited(char* dest, const char* src, int count)
    {
        size_t i;
        for (i = 0; i < (count) && (src)[i] != '\0'; i++) {

            (dest)[i] = (src)[i];
        }
        if (i < (count)) {

            (dest)[i] = '\0';
        }
    }



#ifdef _MSC_VER
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


    #define arch_vsprintf(buffer,bufferCount, fmt, ...) vsprintf_s((buffer), (bufferCount), (fmt), __VA_ARGS__)

    #define arch_sprintf(buffer, bufferCount, fmt, ...) sprintf_s((buffer), (bufferCount), (fmt), __VA_ARGS__)

    #define arch_sscanf( buffer, fmt, ...) sscanf_s((buffer),(fmt), __VA_ARGS__)

    #define arch_open(pFileDescriptor,pFileName, openFlag,shareFlag, permission) _sopen_s(pFileDescriptor, pFileName, openFlag, shareFlag, permission);

    #define arch_fopen(file, mode, file_ptr) fopen_s(&(file_ptr), file, mode)

    #define arch_ctime(buffer, timeptr) ctime_s(buffer, sizeof(buffer), timeptr)

    #define arch_getenv( buffer, varname) do { size_t len; _dupenv_s(&(buffer), &(len), varname);     } while (0)


    inline void arch_strcpy(char* dest, size_t bufferCount, const char* src)
    {
        strcpy_s(dest, bufferCount, src);
    }


    inline void arch_strncpy(char* dest, const char* src, size_t sizeInBytes, size_t maxCount)
    {
        strncpy_s(dest, sizeInBytes, src, maxCount);
    }


    inline void arch_strcat(char* dest, size_t sizeInBytes, const char* src)
    {
        strcat_s(dest, sizeInBytes, src);
    }


    inline char* arch_strtok(char* string, const char* delimiter, char** context)
    {
        return strtok_s(string, delimiter, context);
    }

    inline int arch_unlink(const char* filename)
    {
        return _unlink(filename);
    }

    inline int arch_write(int fileHandle, const void* buf, unsigned int maxCharCount)
    {
        return _write(fileHandle, buf, maxCharCount);
    }

    inline int arch_close(int fileHandle)
    {
        return _close(fileHandle);
    }

    inline int arch_read(int fileHandle, void* dstBuf, unsigned int maxCharCount)
    {
        return _read(fileHandle, dstBuf, maxCharCount);
    }

    inline char* arch_strdup(const char* source)
    {
        return _strdup(source);
    }

    inline int arch_chmod(const char* filename, int mode)
    {
        return _chmod(filename, mode);
    }

    inline int arch_getpid(void)
    {
        return _getpid();
    }

    inline int arch_access(const char* filename, int mode)
    {
        return _access(filename, mode);
    }


    inline void arch_xmlfree(void* ptr)
    {
        xmlFree(ptr);
    }


#else
#define SHARE_FLAG_DENYNO   0													// No deny (shared access)
#define SHARE_FLAG_DENYRD   (S_IRUSR | S_IRGRP | S_IROTH)						// Deny read access
#define SHARE_FLAG_DENYRW   (S_IWUSR | S_IWGRP | S_IRUSR | S_IRGRP | S_IROTH)	// Deny both read and write access
#define PERMISSION_READWRITE   0666
#define PERMISSION_READ  0444
#define PERMISSION_WRITE  0222
#define INVALID_KEY (-1U)

    #define arch_vsprintf(buffer,unused, fmt, ...) vsprintf((buffer), (fmt), __VA_ARGS__)

    #define arch_sprintf(buffer,unused, fmt, ...) sprintf((buffer), (fmt), __VA_ARGS__)

    #define arch_sscanf( buffer, fmt, ...) sscanf((buffer),(fmt), __VA_ARGS__)

    #define arch_open( pFileDescriptor, pFileName, openFlag, shareFlag, unused) do{ *pFileDescriptor = open(pFileName, openFlag, shareFlag); }while(0)

    #define arch_fopen( file, mode, filePtr)  do {filePtr = fopen(file, mode);}while(0)

    #define arch_ctime(buffer,timeptr) do { buffer = ctime(timeptr); } while (0)

    #define arch_getenv(buffer,varname) do {  buffer = getenv(varname); } while (0)


    inline void arch_strcpy(char* dest, size_t unused, const char* src)
    {
        strcpy(dest, src);
    }

    inline void arch_strncpy(char* dest, const char* src, size_t unused, size_t count)
    {
        strncpy(dest, src, count);
    }

    inline void arch_strcat(char* dest, size_t unused, const char* src)
    {
        strcat(dest, src);
    }

    inline char* arch_strtok(char* string, const char* delimiter, char** unused)
    {
        return strtok(string, delimiter);
    }

    inline int arch_unlink(const char* filename)
    {
        return unlink(filename);
    }

    inline int arch_write(int fileHandle, const void* buf, unsigned int maxCharCount)
    {
        return write(fileHandle, buf, maxCharCount);
    }

    inline int arch_close(int fileHandle)
    {
        return close(fileHandle);
    }

    inline int arch_read(int fileHandle, void* dstBuf, unsigned int maxCharCount)
    {
        return read(fileHandle, dstBuf, maxCharCount);
    }

    inline char* arch_strdup(const char* source)
    {
        return strdup(source);
    }

    inline int arch_chmod(const char* filename, int mode)
    {
        return chmod(filename, mode);
    }

    inline int arch_getpid(void)
    {
        return getpid();
    }

    inline int arch_access(const char* filename, int mode)
    {
        return access(filename, mode);
    }
    

    inline void arch_xmlfree(void* ptr)
    {
        arch_safe_free(ptr);
    }

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
