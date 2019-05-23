#
#
#  OO_Copyright_BEGIN
#
#
#  Copyright 2010, 2019 IBM Corp. All rights reserved.
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

lib_LTLIBRARIES = libltfs.la

libltfs_la_SOURCES = \
	ltfs.c \
	ltfs_internal.c \
	ltfs_fsops.c \
	ltfs_fsops_raw.c \
	ltfssnmp.c \
	fs.c \
	xml_common.c \
	xml_writer.c \
	xml_reader.c \
	xml_writer_libltfs.c \
	xml_reader_libltfs.c \
	label.c \
	base64.c \
	tape.c \
	iosched.c \
	dcache.c \
	kmi.c \
	pathname.c \
	index_criteria.c \
	xattr.c \
	ltfslogging.c \
	ltfstrace.c \
	ltfs_thread.c \
	config_file.c \
	plugin.c \
	periodic_sync.c \
	arch/uuid_internal.c \
	arch/filename_handling.c \
	arch/time_internal.c \
	arch/arch_info.c \
	arch/errormap.c

libltfs_la_DEPENDENCIES = ../../messages/liblibltfs_dat.a ../../messages/libinternal_error_dat.a ../../messages/libtape_common_dat.a
libltfs_la_LIBADD =
libltfs_la_CPPFLAGS = @AM_CPPFLAGS@ @AM_EXTRA_CPPFLAGS@ @AM_EXTRA_CPPFLAGS@ -I ..
libltfs_la_LDFLAGS = @AM_LDFLAGS@ -L../../messages -llibltfs_dat -linternal_error_dat -ltape_common_dat

install-data-local:
	if [ ! -d "$(DESTDIR)$(prefix)/share/snmp" ]; then \
		mkdir -p "$(DESTDIR)$(prefix)/share/snmp"; \
		cp LtfsSnmpTrapDef.txt LTFS-MIB.txt "$(DESTDIR)$(prefix)/share/snmp/"; \
	elif [ ! -f "$(DESTDIR)$(prefix)/share/snmp/LtfsSnmpTrapDef.txt" ]; then \
		cp LtfsSnmpTrapDef.txt "$(DESTDIR)$(prefix)/share/snmp/LtfsSnmpTrapDef.txt"; \
	elif [ ! -f "$(DESTDIR)$(prefix)/share/snmp/LTFS-MIB.txt" ]; then \
		cp LTFS-MIB.txt "$(DESTDIR)$(prefix)/share/snmp/LTFS-MIB.txt"; \
	fi
