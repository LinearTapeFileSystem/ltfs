/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2019 IBM Corp. All rights reserved.
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
** FILE NAME:       arch/arch_info.h
**
** DESCRIPTION:     Prototypes for platform information
**
** AUTHOR:          Atsushi Abe
**                  IBM Yamato, Japan
**                  PISTE@jp.ibm.com
**
*************************************************************************************
*/

#ifndef arch_info_h_
#define arch_info_h_

#if defined(__linux__)

#if defined(__i386__)
#define BUILD_SYS_FOR "This binary is built for Linux (i386)"
#define BUILD_SYS_GCC __VERSION__
#elif defined(__x86_64__)
#define BUILD_SYS_FOR "This binary is built for Linux (x86_64)"
#define BUILD_SYS_GCC __VERSION__
#elif defined(__ppc__)
#define BUILD_SYS_FOR "This binary is built for Linux (ppc)"
#define BUILD_SYS_GCC __VERSION__
#elif defined(__ppc64__)
#define BUILD_SYS_FOR "This binary is built for Linux (ppc64)"
#define BUILD_SYS_GCC __VERSION__
#else
#define BUILD_SYS_FOR "This binary is built for Linux (unknown)"
#define BUILD_SYS_GCC __VERSION__
#endif

#elif defined(__APPLE__)

#define BUILD_SYS_FOR "This binary is built for Mac OS X "
#define BUILD_SYS_GCC __VERSION__

#elif defined(__FreeBSD__)

#define BUILD_SYS_FOR "This binary is built for FreeBSD"
#define BUILD_SYS_GCC __VERSION__

#elif defined(__NetBSD__)

#define BUILD_SYS_FOR "This binary is built for NetBSD"
#define BUILD_SYS_GCC __VERSION__

#elif defined(mingw_PLATFORM)

#define BUILD_SYS_FOR "This binary is built for Windows"
#define BUILD_SYS_GCC __VERSION__

#else

#define BUILD_SYS_FOR "This binary is built on an unknown OS"
#define BUILD_SYS_GCC __VERSION__

#endif

void show_runtime_system_info(void);

#endif /* arch_info_h_ */
