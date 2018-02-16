/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2018 IBM Corp. All rights reserved.
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
** FILE NAME:       kmi/flatfile.c
**
** DESCRIPTION:     Implements the flat file key manager interface plugin.
**
** AUTHOR:          Yutaka Oishi
**                  IBM Yamato, Japan
**                  oishi@jp.ibm.com
**
*************************************************************************************
*/

#include <stdio.h>
#include <errno.h>
#include "libltfs/kmi_ops.h"
#include "libltfs/ltfs_fuse_version.h"
#include <fuse.h>
#include "key_format_ltfs.h"

#ifdef mingw_PLATFORM
#include "libltfs/arch/win/win_util.h"
#endif

struct kmi_flatfile_options_data {
	unsigned char *dk_list;        /**< DK and DKi pairs' list */
	unsigned char *dki_for_format; /**< DKi to get DK for formatting a volume */
};

static struct kmi_flatfile_options_data priv;

#define KMI_FLATFILE_OPT(templ,offset,value) { templ, offsetof(struct kmi_flatfile_options_data, offset), value }

static struct fuse_opt kmi_flatfile_options[] = {
	KMI_FLATFILE_OPT("kmi_dk_list=%s",        dk_list,       0),
	KMI_FLATFILE_OPT("kmi_dki_for_format=%s", dki_for_format, 0),
	FUSE_OPT_END
};

/**
 * Convert original dk_list to dk_list on simple plug-in format
 * @path original dk_list i.e. path to the key list file on flat file format
 * @dk_list dk_list on simple plug-in format
 * @return 0 on success or a negative value on error.
 */
static int convert_option(const unsigned char * const path, unsigned char **dk_list)
{
	CHECK_ARG_NULL(dk_list, -LTFS_NULL_ARG);

	struct {
		const char * const name;
		const unsigned char separetor;
	} tag[2] = { { "DK=", '/' }, { "DKi=", ':' } };
	int ret = 0;
	int dk_list_length = 1; /* for '\0' at the end of string */
	int dk_list_offset = 0;
	*dk_list = calloc(dk_list_length, sizeof(unsigned char));
	if (! *dk_list) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return -LTFS_NO_MEMORY;
	}

	FILE *fp = fopen((const char *) path, "r");
	if (! fp) {
		ret = -errno;
		ltfsmsg(LTFS_ERR, 15553E, path, ret);
		return ret;
	}

	char buf[1024];
	unsigned int num_of_lines = 0; /* number of lines which has a valid info */
	for (num_of_lines = 0; fgets(buf, sizeof(buf), fp); ++num_of_lines) {
		const int i = num_of_lines % 2;
		if (! strncmp(buf, tag[i].name, strlen(tag[i].name))) {
			if (buf[strlen(buf) - 1] == '\n')
				buf[strlen(buf) - 1] = '\0';

			if (num_of_lines == 0)
				dk_list_length += strlen(buf) - strlen(tag[i].name);
			else
				dk_list_length += SEPARATOR_LENGTH + strlen(buf) - strlen(tag[i].name);

			void *new_dk_list = realloc(*dk_list, dk_list_length);
			if (! new_dk_list) {
				ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
				fclose(fp);
				return -LTFS_NO_MEMORY;
			}
			*dk_list = new_dk_list;

			if (num_of_lines != 0) {
				*(*dk_list + dk_list_offset) = tag[i].separetor;
				dk_list_offset += SEPARATOR_LENGTH;
			}

			const size_t value_length = strlen(buf) - strlen(tag[i].name);
			memcpy(*dk_list + dk_list_offset, buf + strlen(tag[i].name), value_length);
			dk_list_offset += value_length;
			*(*dk_list + dk_list_offset) = '\0';
		} else if (buf[0] == '\n') {
			/* skip a blank line */
			--num_of_lines;
			continue;
		} else {
			ret = -1;
			ltfsmsg(LTFS_ERR, 15554E);
			break;
		}
	}
	fclose(fp);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

/**
 * Initialize the flat file key manager interface plugin.
 * @param vol LTFS volume
 * @return a pointer to the private data on success or NULL on error.
 */
void *flatfile_init(struct ltfs_volume *vol)
{
	void* km;

	km = key_format_ltfs_init(vol);
	if (km)
		ltfsmsg(LTFS_DEBUG, 15550D);

	return km;
}

/**
 * Destroy the flat file key manager interface plugin.
 * @param kmi_handle the key manager interface handle
 * @return 0 on success or a negative value on error.
 */
int flatfile_destroy(void * const kmi_handle)
{
	int ret;

	ret = key_format_ltfs_destroy(kmi_handle);
	ltfsmsg(LTFS_DEBUG, 15551D);

	return ret;
}

/**
 * Get Key
 * @param keyalias Get key of the key-alias. If *keyalias is NULL, get key of default key-alias
 * @param key Memory is allocated and key is stored at the address.
 * @param kmi_handle the key manager interface handle
 * @return 0 on success or a negative value on error.
 */
int flatfile_get_key(unsigned char **keyalias, unsigned char **key, void * const kmi_handle)
{
	static unsigned char *dk_list = NULL; /* dk_list on simple plug-in format */

	if (priv.dk_list && dk_list == NULL) {
		int ret = convert_option(priv.dk_list, &dk_list);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 15552E);
			if (dk_list) {
				memset(dk_list, 0, strlen((char *) dk_list));
				free(dk_list);
			}
			return ret;
		}
	}

	const int ret = key_format_ltfs_get_key(keyalias, key, kmi_handle, dk_list, priv.dki_for_format);

/*
 *  Cache DK and DKi for revalidation at tape drive POR
 *	if (dk_list) {
 *		memset(dk_list, 0, strlen((char *) dk_list));
 *		free(dk_list);
 *		dk_list = NULL;
 *	}
 */
	return ret;
}

/**
 * Print a help message for the flatfile plugin.
 */
int flatfile_help_message(void)
{
	ltfsresult(15568I);

	return 0;
}

static int null_parser(void *priv, const char *arg, int key, struct fuse_args *outargs)
{
	return 1;
}

/**
 * Parse flatfile plugin specific options.
 * @param opt_args Pointer to a FUSE argument structure, suitable for passing to
 *                 fuse_opt_parse(). See the file backend for an example of argument parsing.
 * @return 0 on success or a negative value on error.
 */
int flatfile_parse_opts(void *opt_args)
{
	struct fuse_args *args = (struct fuse_args *) opt_args;
	int ret;

#ifdef mingw_PLATFORM
	/* Initialized kmi_flatfile_options_data because it is reused by multi mounts on Windows. */
	free(priv.dki_for_format);
	priv.dki_for_format = NULL;
	free(priv.dk_list);
	priv.dk_list = NULL;
#endif

	/* fuse_opt_parse can handle a NULL device parameter just fine */
	ret = fuse_opt_parse(args, &priv, kmi_flatfile_options, null_parser);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 15564E, ret);
		return ret;
	}

	return 0;
}

struct kmi_ops flatfile_ops = {
	.init         = flatfile_init,
	.destroy      = flatfile_destroy,
	.get_key      = flatfile_get_key,
	.help_message = flatfile_help_message,
	.parse_opts   = flatfile_parse_opts,
};

struct kmi_ops *kmi_get_ops(void)
{
	return &flatfile_ops;
}

#ifndef mingw_PLATFORM
extern char kmi_flatfile_dat[];
#endif

const char *kmi_get_message_bundle_name(void ** const message_data)
{
#ifndef mingw_PLATFORM
	*message_data = kmi_flatfile_dat;
#else
	*message_data = NULL;
#endif
	return "kmi_flatfile";
}
