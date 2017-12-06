/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2017 IBM Corp.
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

#include "ltfs.h"
#include "xml.h"
#include "fs.h"
#include "tape.h"
#include "pathname.h"
#include "arch/time_internal.h"

/* O_BINARY is defined only in MinGW */
#ifndef O_BINARY
#define O_BINARY 0
#endif

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
	sprintf(timebuf, "%04d-%02d-%02dT%02d:%02d:%02d.%09ldZ", tm.tm_year+1900, tm.tm_mon+1,
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
				return -1;
			}

			if (ctx->fd > 0) {
				ret = write(ctx->fd, ctx->buf, ctx->buf_size);
				if (ret < 0) {
					ltfsmsg(LTFS_ERR, 17244E, (int)errno);
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
	int ret_t = 0, ret_d = 0, ret = 0;
	struct xml_output_tape *ctx = context;

	if (ctx->buf_used > 0) {
		ret_t = tape_write(ctx->device, ctx->buf, ctx->buf_used, true, true);

		if (ctx->fd >= 0)
			ret_d = write(ctx->fd, ctx->buf, ctx->buf_used);

		ret = (ret_t < 0 || ret_d < 0) ? -1 : 0;
	} else
		ret = 0;

	if (!ret && ctx->fd >= 0) {
		ret = fsync(ctx->fd);
		xml_release_file_lock(ctx->fd);
		ctx->fd = -1;
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 17206E, "tape write callback (fsync)", errno, (unsigned long)ctx->buf_used);
			return -1;
		}
	}
	else if (ctx->fd >= 0) {
		xml_release_file_lock(ctx->fd);
		ctx->fd = -1;
	}

	if (ret_t < 0)
		ltfsmsg(LTFS_ERR, 17061E, (int)ret_t);
	if (ret_d < 0)
		ltfsmsg(LTFS_ERR, 17245E, (int)errno);

	free(ctx->buf);
	free(ctx);
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
		ret = write(ctx->fd, buffer, len);
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

/**
 * Open a file and acquire its lock
 * @param file name to open with lock
 * @param write true if write lock
 */
int xml_acquire_file_lock(const char *file, bool is_write)
{
	int fd;
	int ret;
	int errno_save = 0;
#ifndef mingw_PLATFORM /* There isn't flock in windows */
	struct flock lock;
#endif

	/* Open specified file to lock */
	fd = open(file,
			  O_RDWR | O_CREAT | O_BINARY,
			  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH );
	if (fd < 0) {
		/* Failed to open the advisory lock '%s' (%d) */
		errno_save = errno;
		ltfsmsg(LTFS_WARN, 17241W, file, errno);
		goto out;
	}

#ifndef mingw_PLATFORM /* There isn't flock in windows */
	/* Acquire lock */
	lock.l_type = is_write ? F_WRLCK : F_RDLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	lock.l_pid = 0;
	ret = fcntl(fd, F_SETLKW, &lock);
	if (ret < 0) {
		/* Failed to acquire the advisory lock '%s' (%d) */
		errno_save = errno;
		ltfsmsg(LTFS_WARN, 17242W, file, errno);
		close(fd);
		fd = -1;
		goto out;
	}
#endif

	ret = lseek(fd, 0, SEEK_SET);
	if (ret < 0){
		ltfsmsg(LTFS_ERR, 17246E, "seek", errno);
		errno_save = errno;
		close(fd);
		fd = -1;
		goto out;
	}

	ret = ftruncate(fd, 0);
	if (ret < 0){
		ltfsmsg(LTFS_ERR, 17246E, "seek", errno);
		errno_save = errno;
		close(fd);
		fd = -1;
		goto out;
	}

out:
    errno = errno_save;
	return fd;
}

/**
 * Release advisory lock and close
 * @param fd File descriptor to unlock and close
 */
int xml_release_file_lock(int fd)
{
	int ret = 0;
	int errno_save = 0;
#ifndef mingw_PLATFORM /* There isn't flock in windows */
	struct flock lock;
#endif

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

	close(fd);
    errno = errno_save;

	return ret;
}
