/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2025 IBM Corp. All rights reserved.
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
*/

/* Test helper exercising syscalls that shell utilities do not reach
 * directly: renameat2() flags, ftruncate() on an open descriptor and
 * mmap().
 *
 * Usage:
 *   fsops_helper noreplace <old> <new>   renameat2 with RENAME_NOREPLACE
 *   fsops_helper exchange <a> <b>        renameat2 with RENAME_EXCHANGE
 *   fsops_helper ftruncate <file> <len>  ftruncate an open fd, print new size
 *   fsops_helper mmap <file> <len>       map the file shared and writable
 *
 * Exit codes: 0 = success, 2 = syscall failed (strerror printed on stdout),
 * 3 = usage, 4 = not supported on this platform.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int usage(const char *prog)
{
	fprintf(stderr, "usage: %s noreplace|exchange|ftruncate|mmap <arg> <arg>\n",
		prog);
	return 3;
}

static int parse_len(const char *arg, long long *len)
{
	char *end;

	*len = strtoll(arg, &end, 10);
	return end != arg && *end == '\0';
}

/* Print the error before close() can change errno. */
static int syscall_failed(int fd)
{
	printf("%s\n", strerror(errno));
	if (fd >= 0)
		close(fd);
	return 2;
}

int main(int argc, char **argv)
{
	if (argc != 4)
		return usage(argv[0]);

	if (!strcmp(argv[1], "noreplace") || !strcmp(argv[1], "exchange")) {
		/* renameat2() and RENAME_* need glibc 2.28 or later. */
#if defined(__linux__) && defined(RENAME_NOREPLACE)
		unsigned int flags = !strcmp(argv[1], "noreplace") ?
			RENAME_NOREPLACE : RENAME_EXCHANGE;

		if (renameat2(AT_FDCWD, argv[2], AT_FDCWD, argv[3], flags) < 0)
			return syscall_failed(-1);
		return 0;
#else
		fprintf(stderr, "rename flags are not supported on this platform\n");
		return 4;
#endif
	}

	if (!strcmp(argv[1], "ftruncate")) {
		struct stat st;
		long long len;
		int fd;

		if (!parse_len(argv[3], &len))
			return usage(argv[0]);
		fd = open(argv[2], O_RDWR);
		if (fd < 0)
			return syscall_failed(-1);
		if (ftruncate(fd, len) < 0 || fstat(fd, &st) < 0)
			return syscall_failed(fd);
		printf("%lld\n", (long long)st.st_size);
		close(fd);
		return 0;
	}

	if (!strcmp(argv[1], "mmap")) {
		long long len;
		void *addr;
		int fd;

		if (!parse_len(argv[3], &len))
			return usage(argv[0]);
		fd = open(argv[2], O_RDWR);
		if (fd < 0)
			return syscall_failed(-1);
		addr = mmap(NULL, (size_t)len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (addr == MAP_FAILED)
			return syscall_failed(fd);
		munmap(addr, (size_t)len);
		close(fd);
		return 0;
	}

	fprintf(stderr, "unknown command: %s\n", argv[1]);
	return 3;
}
