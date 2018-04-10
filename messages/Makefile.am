##
##
##  OO_Copyright_BEGIN
##
##
##  Copyright 2010, 2018 IBM Corp. All rights reserved.
##
##  Redistribution and use in source and binary forms, with or without
##   modification, are permitted provided that the following conditions
##  are met:
##  1. Redistributions of source code must retain the above copyright
##     notice, this list of conditions and the following disclaimer.
##  2. Redistributions in binary form must reproduce the above copyright
##     notice, this list of conditions and the following disclaimer in the
##  documentation and/or other materials provided with the distribution.
##  3. Neither the name of the copyright holder nor the names of its
##     contributors may be used to endorse or promote products derived from
##     this software without specific prior written permission.
##
##  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
##  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
##  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
##  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
##  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
##  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
##  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
##  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
##  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
##  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
##  POSSIBILITY OF SUCH DAMAGE.
##
##
##  OO_Copyright_END
##
######################################################################################
##
## COMPONENT NAME:  IBM Linear Tape File System
##
## FILE NAME:       Makefile.osx
##
## DESCRIPTION:     Automake template for LTFS message libraries on Linux.
##
## AUTHOR:          Brian Biskeborn
##                  IBM Almaden Research Center
##                  bbiskebo@us.ibm.com
##
######################################################################################

RESOURCES = \
	libbin_mkltfs_dat.a \
	libbin_ltfsck_dat.a \
	libbin_ltfs_dat.a \
	libkmi_simple_dat.a \
	libkmi_flatfile_dat.a \
	libtape_generic_file_dat.a \
	libtape_generic_itdtimg_dat.a \
	libtape_linux_lin_tape_dat.a \
	libtape_linux_sg_ibmtape_dat.a \
	libtape_iokit_ibmtape_dat.a \
	libtape_freebsd_cam_dat.a \
	libiosched_fcfs_dat.a \
	libiosched_unified_dat.a \
	liblibltfs_dat.a \
	libtape_common_dat.a \
	libinternal_error_dat.a

.PHONY: all $(RESOURCES) clean-local
if CHK_MESSAGE
all: $(RESOURCES) ../src/ltfsmsg.h
else
all: $(RESOURCES)
endif

$(RESOURCES):
	@./make_message_src.sh $@

if CHK_MESSAGE
../src/ltfsmsg.h: $(RESOURCES)
	./print_error_messages.py > ../src/ltfsmsg.h
endif

clean-local:
	rm -rf $(RESOURCES) */work
	rm -f ../src/ltfsmsg.h
