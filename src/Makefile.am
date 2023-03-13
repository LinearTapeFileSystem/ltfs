#
#
#  OO_Copyright_BEGIN
#
#
#  Copyright 2010, 2020 IBM Corp. All rights reserved.
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

if OSS
bin_PROGRAMS = ltfs

ltfs_SOURCES = main.c ltfs_fuse.c
ltfs_DEPENDENCIES = libltfs/libltfs.la ../messages/libbin_ltfs_dat.a
ltfs_CPPFLAGS = @AM_CPPFLAGS@ -I ../ltfs-sde/src -fPIC
ltfs_LDADD = libltfs/libltfs.la
ltfs_LDFLAGS = @AM_LDFLAGS@ ../messages/libbin_ltfs_dat.a
endif

PLAT_DRV =

if PLATFORM_LINUX
PLAT_DRV += tape_drivers/linux/sg
endif

if PLATFORM_MAC
PLAT_DRV += tape_drivers/osx/iokit
endif

if PLATFORM_FREEBSD
PLAT_DRV += tape_drivers/freebsd/cam
endif

if PLATFORM_NETBSD
PLAT_DRV += tape_drivers/netbsd/scsipi-ibmtape
endif

if ENABLE_LIN_TAPE
PLAT_DRV += tape_drivers/linux/lin_tape
endif

GEN_DRV = tape_drivers/generic/file
SUB = iosched libltfs utils

if OSS
GEN_DRV += tape_drivers/generic/itdtimg
SUB += kmi
endif

SUBDIRS = libltfs $(GEN_DRV) $(PLAT_DRV) $(SUB) iosched utils

noinst_HEADERS = \
	libltfs/base64.h \
	libltfs/dcache.h \
	libltfs/fs.h \
	libltfs/index_criteria.h \
	libltfs/iosched.h \
	libltfs/kmi.h \
	libltfs/label.h \
	libltfs/ltfs_endian.h \
	libltfs/ltfs_fsops_raw.h \
	libltfs/ltfs_fuse_version.h \
	libltfs/ltfs_internal.h \
	libltfs/ltfssnmp.h \
	libltfs/pathname.h \
	libltfs/periodic_sync.h \
	libltfs/xattr.h \
	libltfs/xml_libltfs.h \
	libltfs/arch/filename_handling.h \
	libltfs/arch/uuid_internal.h \
	libltfs/arch/version.h

nobase_pkginclude_HEADERS = \
	libltfs/config_file.h \
	libltfs/dcache_ops.h \
	libltfs/iosched_ops.h \
	libltfs/kmi_ops.h \
	libltfs/ltfs_fsops.h \
	libltfs/tape_ops.h \
	libltfs/ltfs_error.h \
	libltfs/ltfs.h \
	libltfs/ltfslogging.h \
	libltfs/ltfstrace.h \
	libltfs/ltfs_types.h \
	libltfs/ltfs_thread.h \
	libltfs/ltfs_locking.h \
	libltfs/ltfs_locking_old.h \
	libltfs/ltfs_locking_new.h \
	libltfs/queue.h \
	libltfs/uthash.h \
	libltfs/uthash_ext.h \
	libltfs/plugin.h \
	libltfs/tape.h \
	libltfs/xml.h \
	libltfs/arch/signal_internal.h \
	libltfs/arch/arch_info.h \
	libltfs/arch/time_internal.h \
	libltfs/arch/errormap.h \
	tape_drivers/ibm_tape.h \
	tape_drivers/spc_op_codes.h \
	tape_drivers/ssc_op_codes.h \
	tape_drivers/tape_drivers.h
