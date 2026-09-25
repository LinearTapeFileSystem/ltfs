/* Test helper exercising syscalls that shell utilities do not reach
 * directly: renameat2() flags and ftruncate() on an open descriptor.
 *
 * Usage:
 *   fsops_helper noreplace <old> <new>   renameat2 with RENAME_NOREPLACE
 *   fsops_helper exchange <a> <b>        renameat2 with RENAME_EXCHANGE
 *   fsops_helper ftruncate <file> <len>  ftruncate an open fd, print new size
 *
 * Exit codes: 0 = success, 2 = syscall failed (errno printed), 3 = usage.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	if (argc != 4) {
		fprintf(stderr, "usage: %s noreplace|exchange|ftruncate <arg> <arg>\n",
			argv[0]);
		return 3;
	}

	if (!strcmp(argv[1], "noreplace") || !strcmp(argv[1], "exchange")) {
#ifdef __linux__
		unsigned int flags = !strcmp(argv[1], "noreplace") ?
			RENAME_NOREPLACE : RENAME_EXCHANGE;

		if (renameat2(AT_FDCWD, argv[2], AT_FDCWD, argv[3], flags) < 0) {
			printf("%s\n", strerror(errno));
			return 2;
		}
		return 0;
#else
		/* The integration tests only run on Linux; keep the helper
		 * compiling on the other platforms. */
		fprintf(stderr, "rename flags are not supported on this platform\n");
		return 3;
#endif
	}

	if (!strcmp(argv[1], "ftruncate")) {
		struct stat st;
		off_t len = strtoll(argv[3], NULL, 10);
		int fd = open(argv[2], O_RDWR);

		if (fd < 0 || ftruncate(fd, len) < 0 || fstat(fd, &st) < 0) {
			printf("%s\n", strerror(errno));
			return 2;
		}
		printf("%lld\n", (long long)st.st_size);
		close(fd);
		return 0;
	}

	fprintf(stderr, "unknown command: %s\n", argv[1]);
	return 3;
}
