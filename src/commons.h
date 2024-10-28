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
#define SAFE_PRINTF printf_s
#else
#define SAFE_PRINTF printf
#endif // _MSC_VER
#endif // !SAFE_PRINTF

#ifndef NAMEOF
#define NAMEOF(member) #member
#endif // !NAMEOF

#ifndef SAFE_STRCPY
#ifdef _MSC_VER
#define SAFE_STRCPY(dest, destSize, src)                                        \
    do                                                                           \
    {                                                                            \
        errno_t err = strcpy_s((dest), (destSize), (src));                     \
        if (err != 0)                                                            \
        {                                                                        \
            fprintf(stderr, "Error: Failed to copy string. Error code: %d\n", err); \
        }                                                                        \
    } while (0)
#else
#define SAFE_STRCPY(dest, src)                                                \
    do                                                                           \
    {                                                                            \
        strcpy((dest), (src));                                                  \
    } while (0)
#endif // _MSC_VER
#endif // !SAFE_STRCPY

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

#ifndef TCS_LOWER
#define TCS_LOWER(str)                      \
    do                                      \
    {                                       \
        TCHAR *p = (str);                   \
        while (*p)                          \
        {                                   \
            *p = (TCHAR)_totlower((int)*p); \
            p++;                            \
        }                                   \
    } while (0)
#endif // !TCS_LOWER

#ifndef TCS_UPPER
#define TCS_UPPER(str)                                                         \
    do                                                                         \
    {                                                                          \
        TCHAR *p = (str);                                                      \
        while (*p)                                                             \
        {                                                                      \
            *p = (TCHAR)(_istupper((int)*p) ? *p : (TCHAR)_totupper((int)*p)); \
            p++;                                                               \
        }                                                                      \
    } while (0)
#endif // !TCS_UPPER

#ifndef SAFE_STRDUP
#define SAFE_STRDUP _strdup
#endif // !SAFE_STRDUP

#ifndef SAFE_CHMOD
#define SAFE_CHMOD _chmod
#endif // !SAFE_CHMOD

#ifndef SAFE_OPEN
#define SAFE_OPEN(pFileHandle, pFileName, openFlag, shareFlag, permission)               \
    do                                                                                   \
    {                                                                                    \
        errno_t err = _sopen_s(pFileHandle, pFileName, openFlag, shareFlag, permission); \
        if (err != 0)                                                                    \
        {                                                                                \
            fprintf(stderr, "Error: Failed to open file. Error code: %d\n", err);        \
        }                                                                                \
    } while (0)
#endif // !SAFE_OPEN

#ifndef SAFE_WRITE
#define SAFE_WRITE _write
#endif // !SAFE_WRITE

#ifndef SAFE_CLOSE
#define SAFE_CLOSE _close
#endif // !SAFE_CLOSE

#ifndef SAFE_READ
#define SAFE_READ _read
#endif // !SAFE_READ

#ifndef SAFE_STRTOK
#define SAFE_STRTOK(token, string, delimiter, contextValue) \
    do                                                      \
    {                                                       \
        char *context = contextValue;                       \
        token = strtok_s((string), (delimiter), &context);  \
    } while (0)
#endif // !SAFE_STRTOK

#ifndef SAFE_FOPEN
#define SAFE_FOPEN(file, mode, file_ptr)                                           \
    do                                                                             \
    {                                                                              \
        errno_t err = fopen_s(&(file_ptr), file, mode);                            \
        if (err != 0)                                                              \
        {                                                                          \
            fprintf(stderr, "Error: Failed to fopen file. Error code: %d\n", err); \
        }                                                                          \
    } while (0)
#endif // !SAFE_FOPEN

#ifdef __cplusplus
}
#endif

#endif // COMMONS_H
