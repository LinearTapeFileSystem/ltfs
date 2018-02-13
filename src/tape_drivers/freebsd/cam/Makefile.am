#
#
#  OO_Copyright_BEGIN
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
#  OO_Copyright_END
#


lib_LTLIBRARIES = libtape-camtape-freebsd.la
libdir = @libdir@/ltfs

AM_LIBTOOLFLAGS = --tag=disable-static

libtape_camtape_freebsd_la_SOURCES = camtape_cmn.c camtape_tc.c
libtape_camtape_freebsd_la_DEPENDENCIES = ../../../../messages/tape_freebsd_camtape_dat.o libtape_camtape_freebsd_la-reed_solomon_crc.lo libtape_camtape_freebsd_la-crc32c_crc.lo libtape_camtape_freebsd_la-ibm_tape.lo
libtape_camtape_freebsd_la_LIBADD = ../../../../messages/tape_freebsd_camtape_dat.o ./libtape_camtape_freebsd_la-reed_solomon_crc.lo ./libtape_camtape_freebsd_la-crc32c_crc.lo ./libtape_camtape_freebsd_la-ibm_tape.lo
libtape_camtape_freebsd_la_LDFLAGS = -avoid-version -module
libtape_camtape_freebsd_la_CPPFLAGS = @AM_CPPFLAGS@ -I ../../.. -I ../..
libtape_camtape_freebsd_la-reed_solomon_crc.lo: ../../reed_solomon_crc.c
	$(LIBTOOL)  --tag=CC $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=compile $(CC) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(libtape_camtape_freebsd_la_CPPFLAGS) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS) $(CRC_OPTIMIZE) -MT libtape_camtape_freebsd_la-reed_solomon_crc.lo -MD -MP -c -o libtape_camtape_freebsd_la-reed_solomon_crc.lo ../../reed_solomon_crc.c

libtape_camtape_freebsd_la-crc32c_crc.lo: ../../crc32c_crc.c
	$(LIBTOOL)  --tag=CC $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=compile $(CC) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(libtape_camtape_freebsd_la_CPPFLAGS) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS) $(CRC_OPTIMIZE) -MT libtape_camtape_freebsd_la-crc32c_crc.lo -MD -MP -c -o libtape_camtape_freebsd_la-crc32c_crc.lo ../../crc32c_crc.c

libtape_camtape_freebsd_la-ibm_tape.lo: ../../ibm_tape.c
	$(LIBTOOL)  --tag=CC $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=compile $(CC) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(libtape_camtape_freebsd_la_CPPFLAGS) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS) $(CRC_OPTIMIZE) -MT libtape_camtape_freebsd_la-ibm_tape.lo -MD -MP -c -o libtape_camtape_freebsd_la-ibm_tape.lo ../../ibm_tape.c


install-exec-hook:
	for f in $(lib_LTLIBRARIES); do rm -f $(libdir)/$$f; done