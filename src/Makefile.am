#
#
#  OO_Copyright_BEGIN
#
#
#  Copyright 2010, 2017 IBM Corp.
#
#  Redistribution and use in source and binary forms, with or without
#   modification, are permitted provided that the following conditions
#  are met:
#  1. Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
#  2. Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#  documentation and/or other materials provided with the distribution.
#  3. Neither the name of the copyright holder nor the names of its
#     contributors may be used to endorse or promote products derived from
#     this software without specific prior written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
#  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
#  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
#  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
#  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
#  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
#  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
#  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
#  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
#  POSSIBILITY OF SUCH DAMAGE.
#
#
#  OO_Copyright_END
#

bin_PROGRAMS = ltfs

ltfs_SOURCES = main.c ltfs_fuse.c
ltfs_DEPENDENCIES = libltfs/libltfs.la ../messages/bin_ltfs_dat.o
ltfs_CPPFLAGS = @AM_CPPFLAGS@ -I ../ltfs-sde/src
ltfs_LDADD = libltfs/libltfs.la ../messages/bin_ltfs_dat.o

if PLATFORM_LINUX
PLAT_OPT = tape_drivers/linux/lin_tape tape_drivers/linux/sg-ibmtape
endif

if PLATFORM_MAC
PLAT_OPT = tape_drivers/osx/iokit-ibmtape
endif

generic_drivers = tape_drivers/generic/file tape_drivers/generic/itdtimg
platform_drivers = $(PLAT_OPT)
SUBDIRS = $(generic_drivers) $(platform_drivers) iosched kmi libltfs utils
