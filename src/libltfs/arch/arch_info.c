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
** FILE NAME:       arch/arch_info.c
**
** DESCRIPTION:     Show platform information
**
** AUTHOR:          Atsushi Abe
**                  IBM Yamato, Japan
**                  PISTE@jp.ibm.com
**
*************************************************************************************
*/

#include "libltfs/ltfs.h"
#ifndef mingw_PLATFORM
#include <sys/sysctl.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

void show_runtime_system_info(void)
#if defined(__linux__)
{
	int fd;
	char kernel_version[512];
	char destribution[256];
	DIR *dir;
	struct dirent *dent;
	struct stat stat_vm64, stat_rel;
	char *path, *tmp;

	fd = open("/proc/version", O_RDONLY);
	if( fd == -1) {
		ltfsmsg(LTFS_WARN, 17086W);
	} else {
		memset(kernel_version, 0, sizeof(kernel_version));
		read(fd, kernel_version, sizeof(kernel_version));
		if((tmp = strchr(kernel_version, '\n')) != NULL)
			*tmp = '\0';

		if(stat("/proc/sys/kernel/vsyscall64", &stat_vm64) != -1 && S_ISREG(stat_vm64.st_mode)) {
#if defined(__i386__) || defined(__x86_64__)
			strcat(kernel_version, " x86_64");
#elif defined(__ppc__) || defined(__ppc64__)
			strcat(kernel_version, " ppc64");
#else
			strcat(kernel_version, " unknown");
#endif
		}
		else {
#if defined(__i386__) || defined(__x86_64__)
			strcat(kernel_version, " i386");
#elif defined(__ppc__) || defined(__ppc64__)
			strcat(kernel_version, " ppc");
#else
			strcat(kernel_version, " unknown");
#endif
		}
		ltfsmsg(LTFS_INFO, 17087I, kernel_version);
		close(fd);
	}

	dir = opendir("/etc");
	if (dir) {
		while( (dent = readdir(dir)) != NULL) {
			if(strlen(dent->d_name) > strlen("-release") &&
			   !strcmp(&(dent->d_name[strlen(dent->d_name) - strlen("-release")]), "-release")) {
				path = calloc(1, strlen(dent->d_name) + strlen("/etc/") + 1);
				if (!path) {
					/* Memory allocation failed */
					ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
					closedir(dir);
					return;
				}
				strcat(path, "/etc/");
				strcat(path, dent->d_name);
				if(stat(path, &stat_rel) != -1 && S_ISREG(stat_rel.st_mode)) {
					fd = open(path, O_RDONLY);
					if( fd == -1) {
						ltfsmsg(LTFS_WARN, 17088W);
					} else {
						memset(destribution, 0, sizeof(destribution));
						read(fd, destribution, sizeof(destribution));
						if((tmp = strchr(destribution, '\n')) != NULL)
							*tmp = '\0';
						ltfsmsg(LTFS_INFO, 17089I, destribution);
						close(fd);
					}
				}
				free(path);
			}
		}
		closedir(dir);
	}

	return;
}
#elif defined(__APPLE__)
{
	int mib[2];
	size_t len;
	char *kernel_version;

	mib[0] = CTL_KERN;
	mib[1] = KERN_VERSION;

	if (sysctl(mib, 2, NULL, &len, NULL, 0) == -1) {
		ltfsmsg(LTFS_WARN, 17090W, "Length check");
		return;
	}

	kernel_version = malloc(len);
	if (!kernel_version) {
		/* Memory allocation failed */
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return;
	}

	if (sysctl(mib, 2, kernel_version, &len, NULL, 0) == -1)
		ltfsmsg(LTFS_WARN, 17090W, "Getting kernel version");
	else if (len > 0)
		ltfsmsg(LTFS_INFO, 17087I, kernel_version);

	free(kernel_version);

	return;
}
#elif defined(mingw_PLATFORM)
{
	/* Windows kernel detection is not supported yet*/
	ltfsmsg(LTFS_INFO, 17087I, "Windows");
	return;
}
#else
{
	ltfsmsg(LTFS_INFO, 17087I, "Unknown kernel");
	return;
}
#endif
