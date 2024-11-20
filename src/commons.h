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
**  commons.h
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

#ifndef SAFE_PRINTF
#ifdef _MSC_VER
#define SAFE_PRINTF sprintf_s
#else
#define SAFE_PRINTF sprintf
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
#define SAFE_STRNCPY(dest,src)                                     \
    do                                                                           \
    {                                                                            \
        strcpy((dest), (src));                                \
    } while (0)
#endif // _MSC_VER
#endif // !SAFE_STRNCPY

#ifndef SAFE_STRNCPY
#ifdef _MSC_VER
#define SAFE_STRNCPY(dest, destSize, src)                                     \
    do                                                                           \
    {                                                                            \
        errno_t err = strncpy_s((dest), (destSize), (src), (destSize));        \
        if (err != 0)                                                            \
        {                                                                        \
            fprintf(stderr, "Error: Failed to copy string. Error code: %d\n", err); \
        }                                                                        \
    } while (0)
#else
#define SAFE_STRNCPY(dest, destSize, src)                                     \
    do                                                                           \
    {                                                                            \
        strncpy((dest), (src), (destSize) - 1);                                \
        (dest)[(destSize) - 1] = '\0';                                         \
        if (strlen(src) >= (destSize))                                         \
        {                                                                        \
            fprintf(stderr, "Warning: String truncated during copy.\n");        \
        }                                                                        \
    } while (0)
#endif // _MSC_VER
#endif // !SAFE_STRNCPY


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
        strncat((dest), (src), sizeof(dest) - strlen(dest) - 1);                  \
        if (strlen((src)) > (sizeof(dest) - strlen(dest) - 1))                    \
        {                                                                         \
            fprintf(stderr, "Warning: String truncated during concatenation.\n"); \
        }                                                                         \
    } while (0)
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




#ifdef __cplusplus
}
#endif

#endif // COMMONS_H
