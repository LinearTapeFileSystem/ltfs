/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2020 IBM Corp. All rights reserved.
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
** FILE NAME:       plugin.c
**
** DESCRIPTION:     Provides functions for working with libltfs plugins.
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

#ifdef mingw_PLATFORM
#include "arch/win/win_util.h"
#endif
#include <stdlib.h>
#include <string.h>
#ifndef mingw_PLATFORM
#include <dlfcn.h>
#endif
#include <errno.h>

#include "libltfs/ltfs_error.h"
#include "libltfs/ltfslogging.h"
#include "config_file.h"
#include "plugin.h"
#include "tape.h"
#include "kmi.h"

int plugin_load(struct libltfs_plugin *pl, const char *type, const char *name,
	struct config_file *config)
{
	int ret;
	const char *lib_path, *message_bundle_name;
	void *message_bundle_data;
	void *(*get_ops)(void) = NULL;
	const char *(*get_messages)(void **) = NULL;

	CHECK_ARG_NULL(pl, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(type, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(name, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(config, -LTFS_NULL_ARG);

	memset(pl, 0, sizeof(*pl));

	lib_path = config_file_get_lib(type, name, config);
	if (! lib_path) {
		ltfsmsg(LTFS_ERR, 11260E, name);
		return -LTFS_NO_PLUGIN;
	}

	pl->lib_handle = dlopen(lib_path, RTLD_NOW);
	if (! pl->lib_handle) {
#ifdef _MSC_VER	
		char *err = dlerror();
		ltfsmsg(LTFS_ERR, 11261E,err);
		free(err);
#else
		ltfsmsg(LTFS_ERR, 11261E, dlerror());

#endif // _MSC_VER
		return -LTFS_PLUGIN_LOAD;
	}

	/* Show loading plugins */
	ltfsmsg(LTFS_INFO, 17085I, name, type);

	/* Make sure the plugin knows how to describe its supported operations */
	if (! strcmp(type, "iosched"))
		get_ops = dlsym(pl->lib_handle, "iosched_get_ops");
	else if (! strcmp(type, "tape"))
		get_ops = dlsym(pl->lib_handle, "tape_dev_get_ops");
	else if (! strcmp(type, "changer"))
		get_ops = dlsym(pl->lib_handle, "changer_get_ops");
	else if (! strcmp(type, "dcache"))
		get_ops = dlsym(pl->lib_handle, "dcache_get_ops");
	else if (! strcmp(type, "kmi"))
		get_ops = dlsym(pl->lib_handle, "kmi_get_ops");
	else if (! strcmp(type, "crepos"))
		get_ops = dlsym(pl->lib_handle, "crepos_get_ops");
	/* config_file_get_lib already verified that "type" contains one of the values above */

	if (! get_ops) {
#ifdef _MSC_VER	
		char *err = dlerror();
		ltfsmsg(LTFS_ERR, 11263E, err);
		free(err);
#else
		ltfsmsg(LTFS_ERR, 11263E, dlerror());

#endif // _MSC_VER
		dlclose(pl->lib_handle);
		pl->lib_handle = NULL;
		return -LTFS_PLUGIN_LOAD;
	}

	/* Make sure the plugin knows how to describe its message bundle (if any) */
	if (! strcmp(type, "iosched"))
		get_messages = dlsym(pl->lib_handle, "iosched_get_message_bundle_name");
	else if (! strcmp(type, "tape"))
		get_messages = dlsym(pl->lib_handle, "tape_dev_get_message_bundle_name");
	else if (! strcmp(type, "changer"))
		get_messages = dlsym(pl->lib_handle, "changer_get_message_bundle_name");
	else if (! strcmp(type, "dcache"))
		get_messages = dlsym(pl->lib_handle, "dcache_get_message_bundle_name");
	else if (! strcmp(type, "kmi"))
		get_messages = dlsym(pl->lib_handle, "kmi_get_message_bundle_name");
	else if (! strcmp(type, "crepos"))
		get_messages = dlsym(pl->lib_handle, "crepos_get_message_bundle_name");
	/* config_file_get_lib already verified that "type" contains one of the values above */

	if (! get_messages) {
#ifdef _MSC_VER	
		char *err = dlerror();
		ltfsmsg(LTFS_ERR, 11284E, err);
		free(err);
#else
		ltfsmsg(LTFS_ERR, 11263E, dlerror());

#endif // _MSC_VER
		dlclose(pl->lib_handle);
		pl->lib_handle = NULL;
		return -LTFS_PLUGIN_LOAD;
	}

	/* Ask the plugin what operations and messages it provides */
	pl->ops = get_ops();
	if (! pl->ops) {
		ltfsmsg(LTFS_ERR, 11264E);
		dlclose(pl->lib_handle);
		pl->lib_handle = NULL;
		return -LTFS_PLUGIN_LOAD;
	}

	message_bundle_name = get_messages(&message_bundle_data);
	if (message_bundle_name) {
		ret = ltfsprintf_load_plugin(message_bundle_name, message_bundle_data, &pl->messages);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 11285E, type, name, ret);
			return ret;
		}
	}

	return 0;
}

int plugin_unload(struct libltfs_plugin *pl)
{
	if (! pl || ! pl->lib_handle)
		return 0;
	ltfsprintf_unload_plugin(pl->messages);

#ifndef VALGRIND_FRIENDLY
	/* Valgrind cannot resolve function name after closing shared library */
	if (dlclose(pl->lib_handle)) {
#ifdef _MSC_VER	
		char *err = dlerror();
		ltfsmsg(LTFS_ERR, 11262E, err);
		free(err);
#else
		ltfsmsg(LTFS_ERR, 11262E, dlerror());

#endif // _MSC_VER
		return -LTFS_PLUGIN_UNLOAD;
	}
#endif

	pl->lib_handle = NULL;
	pl->ops = NULL;
	return 0;
}

/**
 * Print the backend's LTFS help message.
 * @param progname the program name
 * @param ops tape operations for the backend
 * @param type Plugin type, must be "iosched", "kmi" or "driver"
 */
static void print_help_message(const char *progname, void *ops, const char * const type)
{
	if (! ops) {
		ltfsmsg(LTFS_WARN, 10006W, "ops", __FUNCTION__);
		return;
	}

	if (! strcmp(type, "kmi")) {
		int ret = kmi_print_help_message(ops);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 11316E);
		}
	} else if (! strcmp(type, "tape"))
		tape_print_help_message(progname, ops);
	else
		ltfsmsg(LTFS_ERR, 11317E, type);
}

void plugin_usage(const char* progname, const char *type, struct config_file *config)
{
	struct libltfs_plugin pl = {0};
	char **backends;
	int ret, i;

	backends = config_file_get_plugins(type, config);
	if (! backends) {
		if (! strcmp(type, "driver"))
			ltfsresult(14403I); /* -o devname=<dev> */
		return;
	}

	for (i = 0; backends[i] != NULL; ++i) {
		ret = plugin_load(&pl, type, backends[i], config);
		if (ret < 0)
			continue;
		print_help_message(progname, pl.ops, type);
		plugin_unload(&pl);
	}

	for (i = 0; backends[i] != NULL; ++i)
		free(backends[i]);
	free(backends);
}
