#
#
#  OS_Copyright_BEGIN
#
#
#  Copyright 2010, 2018 IBM Corp. All rights reserved.
#  Copyright (c) 2013-2018 Spectra Logic Corporation. All rights reserved.
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
#  OS_Copyright_END
#


lib_LTLIBRARIES = libtape-cam.la
BASENAMES = libtape-cam

AM_LIBTOOLFLAGS = --tag=disable-static

libtape_cam_la_SOURCES = cam_cmn.c cam_tc.c vendor_compat.c ibm_tape.c hp_tape.c quantum_tape.c
libtape_cam_la_DEPENDENCIES = ../../../../messages/libtape_freebsd_cam_dat.a ../../../libltfs/libltfs.la libtape_cam_la-reed_solomon_crc.lo libtape_cam_la-crc32c_crc.lo
libtape_cam_la_LIBADD = ../../../libltfs/libltfs.la ./libtape_cam_la-reed_solomon_crc.lo ./libtape_cam_la-crc32c_crc.lo
libtape_cam_la_LDFLAGS = -avoid-version -module @AM_LDFLAGS@ ../../../../messages/libtape_freebsd_cam_dat.a
libtape_cam_la_CPPFLAGS = @AM_CPPFLAGS@ -I ../../.. -I ../..

vendor_compat.c:
	ln -s ../../vendor_compat.c ./vendor_compat.c

ibm_tape.c:
	ln -s ../../ibm_tape.c ./ibm_tape.c

hp_tape.c:
	ln -s ../../hp_tape.c ./hp_tape.c

quantum_tape.c:
	ln -s ../../quantum_tape.c ./quantum_tape.c

libtape_cam_la-reed_solomon_crc.lo: ../../reed_solomon_crc.c
	$(LIBTOOL)  --tag=CC $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=compile $(CC) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(libtape_cam_la_CPPFLAGS) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS) $(CRC_OPTIMIZE) -MT libtape_cam_la-reed_solomon_crc.lo -MD -MP -c -o libtape_cam_la-reed_solomon_crc.lo ../../reed_solomon_crc.c

libtape_cam_la-crc32c_crc.lo: ../../crc32c_crc.c
	$(LIBTOOL)  --tag=CC $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=compile $(CC) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(libtape_cam_la_CPPFLAGS) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS) $(CRC_OPTIMIZE) -MT libtape_cam_la-crc32c_crc.lo -MD -MP -c -o libtape_cam_la-crc32c_crc.lo ../../crc32c_crc.c

install-exec-hook:
	mkdir -p $(DESTDIR)$(libdir)/ltfs
	for f in $(lib_LTLIBRARIES); do rm -f $(DESTDIR)$(libdir)/$$f; done
	for f in $(BASENAMES); do mv $(DESTDIR)$(libdir)/$$f* $(DESTDIR)$(libdir)/ltfs; done
