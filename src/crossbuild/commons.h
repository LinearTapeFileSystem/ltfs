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
**  crossbuild/commons.h
**
** Abstract:
**
**  This include file has common macros encapsulating functions and
**  non-standard functions that are commonly used among C projects.
**
** Author(s):
**
**  Gustavo Padilla Valdez
**  IBM Mexico Software Laboratory
**  gustavo.padilla@ibm.com
**
** Revision History:
**
**  08/14/2024      --- GPV ---  File created
**  10/24/2024      --- GPV ---  Added STRDUP,FOPEN,STRTOK,CHMOD,OPEN,WRITE,CLOSE
*************************************************************************************
*/

#ifndef COMMONS_H
#define COMMONS_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdio.h>
#include <string.h>

#ifndef WCHAR_TO_CHAR
#ifdef _MSC_VER
#define WCHAR_TO_CHAR(wide_str, narrow_str)                       \
    do {                                                         \
        size_t len = 0;                                          \
        wcstombs_s(&len, NULL, 0, wide_str, 0);                  \
        if (len == 0) { narrow_str = NULL; break; }              \
        if (NULL == narrow_str){narrow_str = (char *)malloc(len);   }     \
        if (narrow_str) wcstombs_s(&len, narrow_str, len, wide_str, len); \
    } while (0)
#endif // _MSC_VER
#endif // !WCHAR_TO_CHAR

#ifndef CHAR_TO_WCHAR
#ifdef _MSC_VER
#define CHAR_TO_WCHAR(narrow_str, wide_str)                       \
    do {                                                         \
        size_t len = 0;                                          \
        mbstowcs_s(&len, NULL, 0, narrow_str, 0);                 \
        if (len == 0) { wide_str = NULL; break; }                 \
        if(wide_str == NULL ){wide_str = (wchar_t *)malloc(len * sizeof(wchar_t));}      \
        if (wide_str) mbstowcs_s(&len, wide_str, len, narrow_str, len); \
    } while (0)
#endif // _MSC_VER
#endif // !CHAR_TO_WCHAR


#ifndef STRCPY_LIMITED
#define STRCPY_LIMITED(dest, src, count)                       \
    do {                                                         \
        size_t i;                                                \
        for (i = 0; i < (count) && (src)[i] != '\0'; i++) {      \
            (dest)[i] = (src)[i];                                \
        }                                                        \
        if (i < (count)) {                                        \
            (dest)[i] = '\0';                                     \
        }                                                        \
    } while (0)
#endif   

#ifndef SAFE_PRINTF
#ifdef _MSC_VER
#define SAFE_PRINTF(buffer, format, ...) sprintf_s(buffer,sizeof(buffer),(format),__VA_ARGS__)
#else
#define SAFE_PRINTF sprintf
#endif // _MSC_VER
#endif // !SAFE_PRINTF

#ifndef SAFE_PRINTF_S
#ifdef _MSC_VER
#define SAFE_PRINTF_S(buffer,size, format, ...) sprintf_s(buffer,size,(format),__VA_ARGS__)
#else
#define SAFE_PRINTF_S(buffer,unused, format, ...) sprintf(buffer,(format),__VA_ARGS__)
#endif // _MSC_VER
#endif // !SAFE_PRINTF

#ifndef SAFE_SCANF
#ifdef _MSC_VER
#define SAFE_SCANF sscanf_s
#else
#define SAFE_SCANF sscanf
#endif // _MSC_VER
#endif // !SAFE_SCANF

#ifndef SAFE_VSPRINTF
#ifdef _MSC_VER
#define SAFE_VSPRINTF(buffer, format, ...)                                      \
    do {                                                                              \
        int result = vsprintf_s((buffer), sizeof(buffer), (format), __VA_ARGS__);           \
        if (result < 0) {                                                             \
            fprintf(stderr, "Error: vsprintf_s failed with error code: %d\n", errno); \
        }                                                                             \
    } while (0)
#else
#define SAFE_VSPRINTF(buffer, format, ...)                                      \
    do {                                                                              \
        int result = vsprintf((buffer), (format), __VA_ARGS__);                      \
        if (result < 0) {                                                             \
            fprintf(stderr, "Error: vsprintf failed with error code: %d\n", errno);   \
        }                                                                             \
    } while (0)
#endif // _MSC_VER
#endif // !SAFE_VSPRINTF

#ifndef NAMEOF
#define NAMEOF(member) #member
#endif // !NAMEOF

#ifndef SAFE_STRCPY
#ifdef _MSC_VER
#define SAFE_STRCPY(dest, src)                                     \
    do                                                                           \
    {                                                                            \
        errno_t err = strcpy_s((dest), sizeof(dest), (src));        \
        if (err != 0)                                                            \
        {                                                                        \
            fprintf(stderr, "Error: Failed to copy string. Error code: %d\n", err); \
        }                                                                        \
    } while (0)
#else
#define SAFE_STRCPY(dest,src)                                     \
    do                                                                           \
    {                                                                            \
        strcpy((dest), (src));                                \
    } while (0)
#endif // _MSC_VER
#endif // !SAFE_STRNCPY

#ifndef SAFE_STRCPY_S
#ifdef _MSC_VER
#define SAFE_STRCPY_S(dest,destSize, src)                                     \
    do                                                                           \
    {                                                                            \
        errno_t err = strcpy_s((dest), destSize, (src));        \
        if (err != 0)                                                            \
        {                                                                        \
            fprintf(stderr, "Error: Failed to copy string. Error code: %d\n", err); \
        }                                                                        \
    } while (0)
#else
#define SAFE_STRCPY_S(dest,unused,src)                                     \
    do                                                                           \
    {                                                                            \
        strcpy((dest), (src));                                \
    } while (0)
#endif // _MSC_VER
#endif // !SAFE_STRNCPY

#ifndef SAFE_STRNCPY
#ifdef _MSC_VER
#define SAFE_STRNCPY(dest, src, destSize)                                     \
    do                                                                           \
    {                                                                            \
        errno_t err = strncpy_s((dest), (destSize), (src), (destSize));        \
        if (err != 0)                                                            \
        {                                                                        \
            fprintf(stderr, "Error: Failed to copy string. Error code: %d\n", err); \
        }                                                                        \
    } while (0)
#else
#define SAFE_STRNCPY(dest, src,destSize)                                     \
    do                                                                           \
    {                                                                            \
        strncpy((dest), (src), (destSize));                                \
    } while (0)
#endif // _MSC_VER
#endif // !SAFE_STRNCPY

#ifndef SAFE_STRNCPY_S
#ifdef _MSC_VER
#define SAFE_STRNCPY_S(dest, src, destSize, count)                                     \
    do                                                                           \
    {                                                                            \
        errno_t err = strncpy_s((dest), (destSize), (src), (count));        \
        if (err != 0)                                                            \
        {                                                                        \
            fprintf(stderr, "Error: Failed to copy string. Error code: %d\n", err); \
        }                                                                        \
    } while (0)
#else
#define SAFE_STRNCPY_S(dest, src,unused, count)                                     \
    do                                                                           \
    {                                                                            \
        strncpy((dest), (src), (count));                                \
    } while (0)
#endif // _MSC_VER
#endif // !SAFE_STRNCPY_S


#ifndef SAFE_STRCAT
#ifdef _MSC_VER
#define SAFE_STRCAT(dest, src)                                                              \
    do                                                                                      \
    {                                                                                       \
        errno_t err = strcat_s((dest), sizeof(dest), (src));                                \
        if (err != 0)                                                                       \
        {                                                                                   \
            fprintf(stderr, "Error: Failed to concatenate strings. Error code: %d\n", err); \
        }                                                                                   \
    } while (0)
#else
#define SAFE_STRCAT(dest, src)                                                    \
    do                                                                            \
    {                                                                             \
        strcat(dest,src);                                                                       \
    } while (0)                                                                     \ 
#endif // _MSC_VER
#endif // !SAFE_STRCAT

#ifndef SAFE_STRCAT_S
#ifdef _MSC_VER
#define SAFE_STRCAT_S(dest, size ,src)                                                              \
    do                                                                                      \
    {                                                                                       \
        errno_t err = strcat_s((dest), size, (src));                                \
        if (err != 0)                                                                       \
        {                                                                                   \
            fprintf(stderr, "Error: Failed to concatenate strings. Error code: %d\n", err); \
        }                                                                                   \
    } while (0)
#else
#define SAFE_STRCAT_S(dest, unused ,src)                                                    \
    do                                                                            \
    {                                                                             \
        strcat(dest,src);                  \                                                                   
    } while (0)                             \ 
#endif // _MSC_VER
#endif // !SAFE_STRCAT



#ifndef SAFE_SNPRINTF
#define SAFE_SNPRINTF(dest, size, format, ...)                             \
    do                                                                     \
    {                                                                      \
        if ((size) > 0)                                                    \
        {                                                                  \
            int result = snprintf((dest), (size), (format), __VA_ARGS__);  \
            if (result < 0 || result >= (size))                            \
            {                                                              \
                fprintf(stderr, "Error: snprintf failed or truncated.\n"); \
            }                                                              \
        }                                                                  \
        else                                                               \
        {                                                                  \
            fprintf(stderr, "Error: Invalid buffer size.\n");              \
        }                                                                  \
    } while (0)
#endif // !SAFE_SNPRINTF

#ifndef SAFE_STRCMP
#define SAFE_STRCMP(str1, str2, cmp_result)                              \
    do                                                                   \
    {                                                                    \
        if ((str1) != NULL && (str2) != NULL)                            \
        {                                                                \
            (cmp_result) = strcmp((str1), (str2));                       \
        }                                                                \
        else                                                             \
        {                                                                \
            fprintf(stderr, "Error: NULL string provided to strcmp.\n"); \
            (cmp_result) = -1; /* Indicate error */                      \
        }                                                                \
    } while (0)
#endif // !SAFE_STRCMP

#ifndef SAFE_STRSTR
#define SAFE_STRSTR(str, substr, result)                                 \
    do                                                                   \
    {                                                                    \
        if ((str) != NULL && (substr) != NULL)                           \
        {                                                                \
            (result) = strstr((str), (substr));                          \
        }                                                                \
        else                                                             \
        {                                                                \
            fprintf(stderr, "Error: NULL string provided to strstr.\n"); \
            (result) = NULL; /* Indicate error */                        \
        }                                                                \
    } while (0)
#endif // !SAFE_STRSTR

#ifndef SAFE_OPEN
#ifdef _MSC_VER
#define SAFE_OPEN(pFileHandle, pFileName, openFlag, shareFlag, permission)               \
    do                                                                                   \
    {                                                                                    \
        errno_t err = _sopen_s(pFileHandle, pFileName, openFlag, shareFlag, permission); \
        if (err != 0)                                                                    \
        {                                                                                \
            fprintf(stderr, "Error: Failed to open file. Error code: %d\n", err);        \
        }                                                                                \
    } while (0)
#else
#define SAFE_OPEN(pFileHandle, pFileName, openFlag, shareFlag, UNUSED)                   \
    do                                                                                   \
    {                                                                                    \
        pFileHandle = open(pFileName, openFlag, shareFlag);                              \
     } while (0)
#endif // !_MSC_VER


#endif // !SAFE_OPEN

#ifndef SAFE_STRTOK
#ifdef _MSC_VER
#define SAFE_STRTOK(token, string, delimiter, contextValue) \
    do                                                      \
    {                                                       \
        char *context = contextValue;                       \
        token = strtok_s((string), (delimiter), &context);  \
    } while (0)
#else
#define SAFE_STRTOK(token, string, delimiter, UNUSED)       \
    do                                                      \
    {                                                       \
        token = strtok((string), (delimiter));               \
    } while (0)
#endif // !_MSC_VER
#endif // !SAFE_STRTOK

#ifndef SAFE_FOPEN
#ifdef _MSC_VER
#define SAFE_FOPEN(file, mode, file_ptr)                                           \
    do                                                                             \
    {                                                                              \
        errno_t err = fopen_s(&(file_ptr), file, mode);                            \
        if (err != 0)                                                              \
        {                                                                          \
            fprintf(stderr, "Error: Failed to fopen file. Error code: %d\n", err); \
        }                                                                          \
    } while (0)
#else
#define SAFE_FOPEN(file, mode, file_ptr)                                           \
    do                                                                             \
    {                                                                              \
        file_ptr = fopen(file, mode);                                                \
    } while (0)
#endif // !_MSC_VER
#endif // !SAFE_FOPEN


#ifndef SAFE_UNLINK
#ifdef _MSC_VER
#define SAFE_UNLINK _unlink
#else
#define SAFE_UNLINK unlink
#endif // !_MSC_VER
#endif // !SAFE_UNLINK

#ifndef SAFE_WRITE
#ifdef _MSC_VER
#define SAFE_WRITE _write
#else
#define SAFE_WRITE write
#endif // !_MSC_VER
#endif // !SAFE_WRITE

#ifndef SAFE_CLOSE
#ifdef _MSC_VER
#define SAFE_CLOSE _close
#else
#define SAFE_CLOSE close
#endif // !_MSC_VER
#endif // !SAFE_CLOSE

#ifndef SAFE_READ
#ifdef _MSC_VER
#define SAFE_READ _read
#else
#define SAFE_READ read
#endif // !_MSC_VER
#endif // !SAFE_READ

#ifndef SAFE_STRDUP
#ifdef _MSC_VER
#define SAFE_STRDUP _strdup
#else
#define SAFE_STRDUP strdup
#endif // !_MSC_VER
#endif // !SAFE_STRDUP

#ifndef SAFE_CHMOD
#ifdef _MSC_VER
#define SAFE_CHMOD _chmod
#else
#define SAFE_CHMOD chmod
#endif // !_MSC_VER
#endif // !SAFE_CHMOD

#ifndef SAFE_GETPID
#ifdef _MSC_VER
#define SAFE_GETPID _getpid
#else
#define SAFE_GETPID getpid
#endif // !_MSC_VER
#endif // !SAFE_GETPID

#ifndef SAFE_STRERROR
#ifdef _MSC_VER
#define SAFE_STRERROR(buf, buf_size, errnum)                          \
    do                                                                \
    {                                                                 \
        errno_t err = strerror_s(buf, buf_size, errnum);              \
        if (err != 0)                                                 \
        {                                                             \
            fprintf(stderr, "Error: Failed to retrieve error string. Error code: %d\n", err); \
        }                                                             \
    } while (0)
#else
#define SAFE_STRERROR(buf, buf_size, errnum)                          \
    do                                                                \
    {                                                                 \
        (void)buf_size; /* Unused */                                  \
        strncpy(buf, strerror(errnum), buf_size - 1);                 \
        buf[buf_size - 1] = '\0'; /* Ensure null-termination */       \
    } while (0)
#endif // !_MSC_VER
#endif // !SAFE_STRERROR

#ifndef SAFE_GETENV
#ifdef _MSC_VER
#define SAFE_GETENV(var, name)                                    \
    do                                                            \
    {                                                             \
        size_t len;                                               \
        errno_t err = _dupenv_s(&var, &len, name);                \
        if (err != 0 || var == NULL)                              \
        {                                                         \
            fprintf(stderr, "Error: Failed to retrieve env var: %s\n", name); \
        }                                                         \
    } while (0)
#else
#define SAFE_GETENV(var, name)                                    \
    do                                                            \
    {                                                             \
        var = getenv(name);                                       \
    } while (0)
#endif // !_MSC_VER
#endif // !SAFE_GETENV

#ifndef SAFE_ACCESS
#ifdef _MSC_VER
#define SAFE_ACCESS _access
#else
#define SAFE_ACCESS access
#endif // !_MSC_VER
#endif // !SAFE_ACCESS

#ifndef SAFE_STRNCASECMP
#ifdef _MSC_VER
#define SAFE_STRNCASECMP _strnicmp
#else
#define SAFE_STRNCASECMP strncasecmp
#endif // !_MSC_VER
#endif // !SAFE_STRNCASECMP

#ifndef SAFE_STRCASECMP
#ifdef _MSC_VER
#define SAFE_STRCASECMP(s1, s2) _strnicmp((s1), (s2), strlen(s1) > strlen(s2) ? strlen(s1) : strlen(s2))
#else
#define SAFE_STRCASECMP strncasecmp
#endif // !_MSC_VER
#endif // !SAFE_STRNCASECMP

#ifndef SAFE_VSNWPRINTF
#ifdef _MSC_VER
#define SAFE_VSNWPRINTF _vsnwprintf_s
#else
#define SAFE_VSNWPRINTF(buffer,unused,bufferCount,format,argptr)      \
        do {    \
                _vsnwprintf(buffer, bufferCount, format, argptr); \
        } while (0)
#endif // !_MSC_VER
#endif // !SAFE_VSNWPRINTF

#ifndef SAFE_STPRINTF
#ifdef _MSC_VER
#define SAFE_STPRINTF(buffer, buffer_size, format, ...)                         \
    do {                                                                        \
        int result = _stprintf_s((buffer), ((buffer_size) / sizeof(TCHAR)), (format), __VA_ARGS__); \
        if (result < 0) {                                                       \
            fwprintf(stderr, L"Error: _stprintf_s failed with error code: %d\n", result); \
        }                                                                       \
    } while (0)
#else
#define SAFE_STPRINTF(buffer, unused, format, ...)                         \
    do {                                                                        \
        int result = _stprintf((buffer), (format), __VA_ARGS__);  \
        if (result < 0) {                                                       \
            fwprintf(stderr, L"Error: snprintf failed with error code: %d\n", result); \
        }                                                                       \
    } while (0)
#endif // _MSC_VER
#endif // !SAFE_STPRINTF



#ifndef SAFE_CTIME
#ifdef _MSC_VER
#define SAFE_CTIME(buffer, time_ptr)                                            \
    do {                                                                        \
        errno_t result = ctime_s((buffer), sizeof(buffer), (time_ptr));         \
        if (result != 0) {                                                      \
            fprintf(stderr, "Error: ctime_s failed with error code: %d\n", result); \
        }                                                                       \
    } while (0)
#else
#define SAFE_CTIME(buffer, time_ptr)                                            \
    do {                                                                        \
        char *result = ctime_r((time_ptr), (buffer));                           \
        if (result == NULL) {                                                   \
            fprintf(stderr, "Error: ctime_r failed with error code: %d\n", errno); \
        }                                                                       \
    } while (0)
#endif // _MSC_VER
#endif // !SAFE_CTIME



#ifndef SAFE_LOCALTIME
#ifdef _MSC_VER
#define SAFE_LOCALTIME(dest, src)                                     \
    do                                                                           \
    {                                                                            \
       localtime_s((dest),(src));    \
    } while (0)
#else
#define SAFE_LOCALTIME(dest,src)                                     \
    do                                                                           \
    {                                                                            \
        (dest) = localtime(src);                                \
    } while (0)
#endif // _MSC_VER
#endif // !SAFE_STRNCPY


#ifndef SAFE_VSNPRINTF
#ifdef _MSC_VER
#define SAFE_VSNPRINTF vsnprintf_s
#else
#define SAFE_VSNPRINTF vsnprintf(buffer,unused,bufferCount,format,argptr)   \
do { \
        vsnprintf(buffer, bufferCount, format, argptr); \
} while (0)
#endif // !_MSC_VER
#endif // !SAFE_VSNPRINTF

#ifndef SAFE_FREE   
#define SAFE_FREE(memobject)                            \
        do {                                            \
            if(NULL != memobject)                       \
            {                                           \
                free(memobject);                        \
                memobject = NULL;                           \
            }                                              \
        }while(0)
#endif


#ifdef __cplusplus
}
#endif

#endif // COMMONS_H
