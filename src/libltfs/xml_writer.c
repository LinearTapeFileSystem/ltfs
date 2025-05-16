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
**
*************************************************************************************
**
** COMPONENT NAME:  IBM Linear Tape File System
**
** FILE NAME:       xml_writer.c
**
** DESCRIPTION:     XML writer routines for Indexes and Labels.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
**                  Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
*************************************************************************************
*/

#include <libxml/xmlstring.h>
#include <libxml/xmlwriter.h>

#include "libltfs/arch/ltfs_arch_ops.h"
#include "ltfs.h"
#include "xml.h"
#include "fs.h"
#include "tape.h"
#include "pathname.h"
#include "arch/time_internal.h"

/**
 * Format a raw timespec structure for the XML file.
 */
int xml_format_time(struct ltfs_timespec t, char** out)
{
	char *timebuf;
	struct tm tm, *gmt;
	ltfs_time_t sec;
	int noramized;

	*out = NULL;
	noramized = normalize_ltfs_time(&t);
	sec = t.tv_sec;

	gmt = ltfs_gmtime(&sec, &tm);
	if (! gmt) {
		ltfsmsg(LTFS_ERR, 17056E);
		return -1;
	}

	timebuf = calloc(31, sizeof(char));
	if (!timebuf) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return -1;
	}
	arch_sprintf(timebuf, (31*sizeof(char)), "%04d-%02d-%02dT%02d:%02d:%02d.%09ldZ", tm.tm_year + 1900, tm.tm_mon + 1,
			tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, t.tv_nsec);
	*out = timebuf;

	return noramized;
}

/**
 * Write callback for XML output using libxml2's I/O routines. It buffers the data it receives
 * into chunks of 1 tape block each and writes each chunk to the tape.
 */
int xml_output_tape_write_callback(void *context, const char *buffer, int len)
{
	ssize_t ret;
	struct xml_output_tape *ctx = context;
	uint32_t copy_count; /* number of bytes of "buffer" to write immediately */
	uint32_t bytes_remaining; /* number of input bytes waiting to be handled */

	if (len == 0)
		return 0;

	if (ctx->err_code || ctx->errno_fd)
		return -1;

	if (ctx->buf_used + len < ctx->buf_size) {
		memcpy(ctx->buf + ctx->buf_used, buffer, len);
		ctx->buf_used += len;
	} else {
		bytes_remaining = len;
		do {
			copy_count = ctx->buf_size - ctx->buf_used;
			memcpy(ctx->buf + ctx->buf_used, buffer + (len - bytes_remaining), copy_count);
			ret = tape_write(ctx->device, ctx->buf, ctx->buf_size, true, true);
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, 17060E, (int)ret);
				ctx->err_code = ret;
				return -1;
			}

			if (ctx->fd > 0) {
				ret = arch_write(ctx->fd, ctx->buf, ctx->buf_size);
				if (ret < 0) {
					ltfsmsg(LTFS_ERR, 17244E, (int)errno);
					ctx->errno_fd = -LTFS_CACHE_IO;
					return -1;
				}
			}

			ctx->buf_used = 0;
			bytes_remaining -= copy_count;
		} while (bytes_remaining > ctx->buf_size);
		if (bytes_remaining > 0)
			memcpy(ctx->buf, buffer + (len - bytes_remaining), bytes_remaining);
		ctx->buf_used = bytes_remaining;
	}

	return len;
}

/**
 * Close callback for XML output using libxml2's I/O routines. It flushes any partial buffer
 * which might be left after the write callback has received all XML data.
 */
int xml_output_tape_close_callback(void *context)
{
	int ret_t = 0, ret_d = 0, ret = 0, sret = 0;
	struct xml_output_tape *ctx = context;

	if (!ctx->err_code && !ctx->errno_fd && ctx->buf_used > 0) {
		ret_t = tape_write(ctx->device, ctx->buf, ctx->buf_used, true, true);
		if (ret_t < 0) {
			ltfsmsg(LTFS_ERR, 17061E, (int)ret);
			ctx->err_code = ret_t;
			ret = -1;
		} else {
			if (ctx->fd >= 0)
				ret_d = arch_write(ctx->fd, ctx->buf, ctx->buf_used);
			if (ret_d < 0) {
				ltfsmsg(LTFS_ERR, 17245E, (int)errno);
				ctx->errno_fd = -LTFS_CACHE_IO;
				ret = -1;
			}
		}
	} else
		ret = 0;

	if (!ctx->errno_fd && ctx->fd >= 0) {
		sret = fsync(ctx->fd);
		if (sret < 0) {
			ltfsmsg(LTFS_ERR, 17206E, "tape write callback (fsync)", errno, (unsigned long)ctx->buf_used);
			return -1;
		}
	}

	return ret;
}

/**
 * Write callback for XML output using libxml2's I/O routines for file descriptor.
 */
int xml_output_fd_write_callback(void *context, const char *buffer, int len)
{
	ssize_t ret;
	struct xml_output_fd *ctx = context;

	if (len > 0) {
		ret = arch_write(ctx->fd, buffer, len);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 17206E, "write callback (write)", errno, (unsigned long)len);
			return -1;
		}

		ret = fsync(ctx->fd);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 17206E, "write callback (fsync)", errno, (unsigned long)len);
			return -1;
		}
	}

	return len;
}

/**
 * Close callback for XML output using libxml2's I/O routines. It flushes any partial buffer
 * which might be left after the write callback has received all XML data.
 */
int xml_output_fd_close_callback(void *context)
{
	struct xml_output_fd *ctx = context;

	free(ctx);

	return 0;
}

#define COPY_BUF_SIZE (512 * KB)

#ifdef _WIN32
#include <windows.h>
#include <io.h>

int ftruncate(int fd, off_t length) {
	HANDLE hFile = (HANDLE)_get_osfhandle(fd);
	if (hFile == INVALID_HANDLE_VALUE) {
		return -1;
	}

	LARGE_INTEGER li;
	li.QuadPart = length;

	if (!SetFilePointerEx(hFile, li, NULL, FILE_BEGIN)) {
		return -1;
	}

	if (!SetEndOfFile(hFile)) {
		return -1;
	}

	return 0;
}

#endif


static int _copy_file_contents(int dest, int src)
{
	int ret = 0;
	size_t len_read, len_written;
	char *buf = NULL;

	buf = malloc(COPY_BUF_SIZE);
	if (!buf) {
		ltfsmsg(LTFS_ERR, 10001E, "_copy_file: buffer");
		return -LTFS_NO_MEMORY;
	}

	ret = lseek(src, 0, SEEK_SET);
	if (ret < 0){
		ltfsmsg(LTFS_ERR, 17246E, "source seek", errno);
		free(buf);
		return -LTFS_CACHE_IO;
	}

	ret = lseek(dest, 0, SEEK_SET);
	if (ret < 0){
		ltfsmsg(LTFS_ERR, 17246E, "destination seek", errno);
		free(buf);
		return -LTFS_CACHE_IO;
	}

	ret = ftruncate(dest, 0);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 17246E, "destination truncate", errno);
		free(buf);
		return -LTFS_CACHE_IO;
	}

	while ((len_read = arch_read(src, buf, COPY_BUF_SIZE)) > 0) {
		len_written = arch_write(dest, buf, len_read);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 17246E, "_copy_file", errno);
			free(buf);
			return -LTFS_CACHE_IO;
		} else if (len_written != len_read) {
			ltfsmsg(LTFS_ERR, 17246E, "_copy_file unexpected len", errno);
			free(buf);
			return -LTFS_CACHE_IO;
		}
	}

	free(buf);
	fsync(dest);

	if (len_read) {
		ltfsmsg(LTFS_ERR, 17246E, "_copy_file unexpected read", errno);
		return -LTFS_CACHE_IO;
	}

	ret = lseek(src, 0, SEEK_SET);
	if (ret < 0){
		ltfsmsg(LTFS_ERR, 17246E, "source seek (P)", errno);
		return -LTFS_CACHE_IO;
	}

	ret = lseek(dest, 0, SEEK_SET);
	if (ret < 0){
		ltfsmsg(LTFS_ERR, 17246E, "destination seek (P)", errno);
		return -LTFS_CACHE_IO;
	}

	return 0;
}

/**
 * Open a file and acquire its lock
 * @param file name to open with lock
 * @param write true if write lock
 * @param bk backup fd to revert
 */
const struct timespec lock_wait = {0, 100000000}; /* 100ms */
const struct timespec lock_zero = {0, 0};
#define LOCK_RETRIES (12000) /* 100ms x 12,000 = 1200sec = 20 min */
int xml_acquire_file_lock(const char *file, int *fd, int *bk_fd, bool is_write)
{
	int ret = -LTFS_CACHE_IO;

	int errno_save = 0;
	char *backup_file = NULL;
#ifndef mingw_PLATFORM /* There isn't flock in windows */
	struct flock lock;
#endif

	*fd = *bk_fd = -1;

	/* Open specified file to lock */
	arch_open(fd,file,O_RDWR | O_CREAT | O_BINARY, SHARE_FLAG_DENYRW, PERMISSION_READWRITE);
	if (*fd < 0) {
		/* Failed to open the advisory lock '%s' (%d) */
		errno_save = errno;
		ltfsmsg(LTFS_WARN, 17241W, file, errno);
		goto out;
	}

#ifndef mingw_PLATFORM /* There isn't flock in windows */
	int retry_count = 0;
	struct timespec next_wait  = lock_wait;
	struct timespec remaining = {0, 0};

retry:
	/* Acquire lock */
	lock.l_type = is_write ? F_WRLCK : F_RDLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	lock.l_pid = 0;

	ret = fcntl(*fd, F_SETLKW, &lock);
	if (ret < 0) {
		if (errno == EDEADLK && retry_count < LOCK_RETRIES) {
			if (retry_count % 600 == 0) {
				ltfsmsg(LTFS_INFO, 17261I, file, retry_count);
			}

			next_wait = lock_wait;
			while (next_wait.tv_sec != 0 || next_wait.tv_nsec != 0) {
				errno = 0;
				ret = nanosleep(&next_wait, &remaining);
				if (ret < 0) {
					if (errno == EINTR) {
						/* Sleep again with remaining timer */
						ltfsmsg(LTFS_INFO, 17260I, file);
						next_wait = remaining;
						remaining = lock_zero;
					} else {
						/* Sleep fails on unexpected error but retry to acquire the lock */
						ltfsmsg(LTFS_INFO, 17263I, file, errno, retry_count);
						next_wait = lock_zero;
						remaining = lock_zero;
					}
				} else {
					/* Sleep success, retry to acquire the lock */
					next_wait = lock_zero;
					remaining = lock_zero;
				}
			}

			/* Retry to acquire the lock */
			retry_count++;
			goto retry;
		} else {
			/* Failed to acquire the advisory lock '%s' (%d) */
			errno_save = errno;
			ltfsmsg(LTFS_WARN, 17242W, file, errno);
			close(*fd);
			*fd = -1;
			goto out;
		}
	}
#endif

	/* Create backup file if required */
	if (bk_fd) {
		asprintf(&backup_file, "%s.%s", file, "bk");
		if (!backup_file){
			ltfsmsg(LTFS_ERR, 10001E, "xml_acquire_file_lock: backup name");
			arch_close(*fd);
			*fd = -1;
			goto out;
		}
		arch_open(bk_fd,backup_file,
					  O_RDWR | O_CREAT | O_BINARY | O_TRUNC,SHARE_FLAG_DENYRW, PERMISSION_READWRITE);
		if (*bk_fd < 0) {
			ltfsmsg(LTFS_ERR, 17246E, "backup file creation", errno);
			errno_save = errno;
			arch_close(*fd);
			*fd = -1;
			goto out;
		}
		free(backup_file);
		backup_file = NULL;

		ret = _copy_file_contents(*bk_fd, *fd);
		if (ret < 0) {
			errno_save = errno;
			arch_close(*fd);
			*fd = -1;
			arch_close(*bk_fd);
			*bk_fd = -1;
			goto out;
		}
	}

	ret = lseek(*fd, 0, SEEK_SET);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 17246E, "seek", errno);
		errno_save = errno;
		arch_close(*fd);
		*fd = -1;
		arch_close(*bk_fd);
		*bk_fd = -1;
		goto out;
	}

	ret = ftruncate(*fd, 0);
	if (ret < 0){
		ltfsmsg(LTFS_ERR, 17246E, "truncate", errno);
		errno_save = errno;
		arch_close(*fd);
		*fd = -1;
		arch_close(*bk_fd);
		*bk_fd = -1;
		goto out;
	}

	ret = 0;

out:
    errno = errno_save;
	return ret;
}

/**
 * Release advisory lock and close
 * @param fd File descriptor to unlock and close
 */
int xml_release_file_lock(const char *file, int fd, int bk_fd, bool revert)
{
	int ret = 0;
	int errno_save = 0;
	char *backup_file = NULL;
#ifndef mingw_PLATFORM /* There isn't flock in windows */
	struct flock lock;
#endif

	if (bk_fd >= 0 && revert) {
		ret = _copy_file_contents(fd, bk_fd);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 17246E, "revert seek", errno);
			arch_close(bk_fd);
			arch_close(fd);
			return -1;
		}
	}

#ifndef mingw_PLATFORM /* There isn't flock in windows */
	/* Release lock */
	lock.l_type = F_UNLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	lock.l_pid = 0;
	ret = fcntl(fd, F_SETLK, &lock);
	if (ret < 0) {
		/* Failed to release the advisory lock (%d) */
		errno_save = errno;
		ltfsmsg(LTFS_WARN, 17243W, errno);
	}
#endif

	if (fd >= 0) arch_close(fd);
	if (bk_fd >= 0) arch_close(bk_fd);
    errno = errno_save;

	asprintf(&backup_file, "%s.%s", file, "bk");
	if (!backup_file){
		ltfsmsg(LTFS_ERR, 10001E, "xml_release_file_lock: backup name");
		ret = -LTFS_NO_MEMORY;
	} else {
		arch_unlink(backup_file);
		free(backup_file);
	}

	return ret;
}
