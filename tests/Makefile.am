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

#
# Integration tests for LTFS using the file tape backend.
#
# Run with: make check
# The tests need a Linux host with /dev/fuse; elsewhere they are skipped.
# On macOS use tests/run-in-docker.sh to run them in a Linux container.
#

TEST_EXTENSIONS = .sh
SH_LOG_COMPILER = $(SHELL)

AM_TESTS_ENVIRONMENT = \
	top_builddir='$(abs_top_builddir)' top_srcdir='$(abs_top_srcdir)'; \
	export top_builddir top_srcdir;

check_PROGRAMS = helpers/fsops_helper
helpers_fsops_helper_SOURCES = helpers/fsops_helper.c

# TESTS_LIST is set by configure from tests/t/*.sh; GNU make functions
# cannot be used because automake runs with -Wall -Werror.
TESTS = $(TESTS_LIST)

EXTRA_DIST = \
	lib/harness.sh \
	$(TESTS) \
	docker/Dockerfile \
	run-in-docker.sh
