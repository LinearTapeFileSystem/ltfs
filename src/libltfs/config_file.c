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
** FILE NAME:       config_file.c
**
** DESCRIPTION:     Provides functions for reading the LTFS configuration file.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
**                  Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
**                  Atsushi Abe
**                  IBM Tokyo Lab., Japan
**               	piste@jp.ibm.com
**
*************************************************************************************
*/

#ifdef mingw_PLATFORM
#include "arch/win/win_util.h"
#include "ltfs_unistd.h"
#else
#include <unistd.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "libltfs/ltfslogging.h"
#include "libltfs/ltfs_error.h"
#include "config_file.h"


struct plugin_entry {
	TAILQ_ENTRY(plugin_entry) list;
	char *type;
	char *name;
	char *library;
};

struct option_entry {
	TAILQ_ENTRY(option_entry) list;
	char *type;
	char *option;
};

struct config_file {
	TAILQ_HEAD(plugin_struct, plugin_entry) plugins;          /**< Available plugins */
	TAILQ_HEAD(default_struct, plugin_entry) default_plugins; /**< Default plugins */
	TAILQ_HEAD(mount_struct, option_entry) mount_options;     /**< Mount options */
};

int _config_file_parse(const char *path, bool ignore_error, struct config_file *config);
int _config_file_validate(struct config_file *config);
int _config_file_parse_name(const char *directive, const char *name_desc, char *saveptr, char **out);
int _config_file_parse_default(char *saveptr, struct config_file *config);
int _config_file_remove_default(char *saveptr, struct config_file *config);
int _config_file_parse_plugin(char *saveptr, struct config_file *config);
int _config_file_remove_plugin(char *saveptr, struct config_file *config);
int _config_file_parse_option(const char *line, char *saveptr, struct option_entry **out);


int config_file_load(const char *path, struct config_file **config)
{
	int ret;

	CHECK_ARG_NULL(config, -LTFS_NULL_ARG);

	if (! path)
		path = LTFS_CONFIG_FILE;

	*config = calloc(1, sizeof(struct config_file));
	if (! (*config)) {
		ltfsmsg(LTFS_ERR, 10001E, "config_file_load: config structure");
		return -LTFS_NO_MEMORY;
	}
	TAILQ_INIT(&(*config)->plugins);
	TAILQ_INIT(&(*config)->default_plugins);
	TAILQ_INIT(&(*config)->mount_options);

	ret = _config_file_parse(path, false, *config);
	if (ret < 0)
		goto out;

	ret = _config_file_validate(*config);

out:
	if (ret < 0) {
		config_file_free(*config);
		*config = NULL;
	}
	return ret;
} /* BEAM: memory not freed - *config should be freed out of this function */

void config_file_free(struct config_file *config)
{
	struct plugin_entry *plug_entry, *plug_aux;
	struct option_entry *opt_entry, *opt_aux;

	if (config) {
		TAILQ_FOREACH_SAFE(plug_entry, &config->plugins, list, plug_aux) {
			free(plug_entry->type);
			free(plug_entry->name);
			free(plug_entry->library);
			free(plug_entry);
		}
		TAILQ_FOREACH_SAFE(plug_entry, &config->default_plugins, list, plug_aux) {
			free(plug_entry->type);
			free(plug_entry->name);
			/* plug_entry->library is always NULL */
			free(plug_entry);
		}
		TAILQ_FOREACH_SAFE(opt_entry, &config->mount_options, list, opt_aux) {
			free(opt_entry->type);
			free(opt_entry->option);
			free(opt_entry);
		}
		free(config);
	}
}

const char *config_file_get_default_plugin(const char *type, struct config_file *config)
{
	struct plugin_entry *entry = NULL;

	CHECK_ARG_NULL(type, NULL);
	CHECK_ARG_NULL(config, NULL);

	TAILQ_FOREACH(entry, &config->default_plugins, list) {
		if (! strcmp(entry->type, type))
			return entry->name;
	}

	return NULL;
}

const char *config_file_get_lib(const char *type, const char *name, struct config_file *config)
{
	struct plugin_entry *entry;

	CHECK_ARG_NULL(type, NULL);
	CHECK_ARG_NULL(name, NULL);
	CHECK_ARG_NULL(config, NULL);

	TAILQ_FOREACH(entry, &config->plugins, list) {
		if (! strcmp(entry->type, type) && ! strcmp(entry->name, name))
			return entry->library;
	}

	ltfsmsg(LTFS_ERR, 11267E, type, name);

	return NULL;
}

char **config_file_get_plugins(const char *type, struct config_file *config)
{
	size_t count = 0, pos = 0;
	char **list;
	struct plugin_entry *entry;

	TAILQ_FOREACH(entry, &config->plugins, list)
		if (! strcmp(entry->type, type))
			++count;

	list = calloc(count + 1, sizeof(char *));
	if (! list) {
		ltfsmsg(LTFS_ERR, 10001E, "config_file_get_plugins: pointer list");
		return NULL;
	}

	TAILQ_FOREACH(entry, &config->plugins, list) {
		if (! strcmp(entry->type, type)) {
			list[pos] = SAFE_STRDUP(entry->name);
			if (! list[pos]) {
				ltfsmsg(LTFS_ERR, 10001E, "config_file_get_plugins: list entry");
				for (count=0; count<pos; ++count)
					free(list[pos]);
				free(list);
				return NULL;
			}
			++pos;
		}
	}

	return list;
}

char **config_file_get_options(const char *type, struct config_file *config)
{
	size_t count = 0, pos = 0;
	char **list;
	struct option_entry *entry;

	TAILQ_FOREACH(entry, &config->mount_options, list)
		if (! strcmp(entry->type, type))
			++count;

	list = calloc(count + 1, sizeof(char *));
	if (! list) {
		ltfsmsg(LTFS_ERR, 10001E, "config_file_get_options: pointer list");
		return NULL;
	}

	TAILQ_FOREACH(entry, &config->mount_options, list) {
		if (! strcmp(entry->type, type)) {
			list[pos] = SAFE_STRDUP(entry->option);
			if (! list[pos]) {
				ltfsmsg(LTFS_ERR, 10001E, "config_file_get_options: list entry");
				goto out_free;
			}
			++pos;
		}
	}

	return list;

out_free:
	for (count=0; count<pos; ++count)
		free(list[pos]);
	free(list);
	return NULL;
}

/**
 * Parse a configuration file. This is a helper function called by @config_file_load.
 * @param path File to parse. This must not be NULL.
 * @param config Configuration structure to populate.
 * @return 0 on success or a negative value on error.
 */
int _config_file_parse(const char *path, bool ignore_error, struct config_file *config)
{
	FILE *conf_file;
	char line[65536], *saveline = NULL;
	char *tok, *saveptr, *strip_pos;
	char *include_file = NULL;
	struct option_entry *ol;
	int ret, offset;
	
	SAFE_FOPEN(path, "rb", conf_file);
	if (! conf_file) {
		if (!ignore_error) {
			ret = -errno;
			ltfsmsg(LTFS_ERR, 11268E, path, ret);
			return ret;
		} else
			return 0;
	}

	/* Parse the config file */
	while (fgets(line, 65536, conf_file)) {
		if (strlen(line) == 65535) {
			ltfsmsg(LTFS_ERR, 11269E);
			ret = -LTFS_CONFIG_INVALID;
			goto out;
		}

		/* Ignore comments and trailing whitespace */
		strip_pos = strstr(line, "#");
		if (! strip_pos)
			strip_pos = line + strlen(line);
		while (strip_pos > line &&
				   (*(strip_pos - 1) == ' ' || *(strip_pos - 1) == '\t' ||
				    *(strip_pos - 1) == '\r' || *(strip_pos - 1) == '\n'))
				--strip_pos;
		*strip_pos = '\0';

		saveline = SAFE_STRDUP(line);
		if (! saveline) {
			ltfsmsg(LTFS_ERR, 10001E, "_config_file_parse: saveline");
			ret = -LTFS_NO_MEMORY;
			goto out;
		}

		/* Parse the configuration directive */
		tok = strtok_r(line, " \t\r\n", &saveptr);
		if (tok) {
			if (! strcmp(tok, "plugin")) {
				ret = _config_file_parse_plugin(saveptr, config);
				if (ret < 0)
					goto out;

			} else if (! strcmp(tok, "default")) {
				ret = _config_file_parse_default(saveptr, config);
				if (ret < 0)
					goto out;

			} else if (! strcmp(tok, "option")) {
				offset = tok - line + strlen(tok) + 1;
				ret = _config_file_parse_option(&saveline[offset], saveptr, &ol);
				if (ret < 0)
					goto out;
				TAILQ_INSERT_TAIL(&config->mount_options, ol, list);

			} else if (! strcmp(tok, "include")) {
				ret = _config_file_parse_name("include", "include file", saveptr, &include_file);
				if (ret < 0)
					goto out;
				ret = _config_file_parse(include_file, false, config);
				free(include_file);
				include_file = NULL;
				if (ret < 0)
					goto out;

			} else if (! strcmp(tok, "include_noerror")) {
				ret = _config_file_parse_name("include", "include file", saveptr, &include_file);
				if (ret < 0)
					goto out;
				ret = _config_file_parse(include_file, true, config);
				free(include_file);
				include_file = NULL;

			} else if (! strcmp(tok, "-default")) {
				ret = _config_file_remove_default(saveptr, config);
				if (ret < 0)
					goto out;

			} else if (! strcmp(tok, "-plugin")) {
				ret = _config_file_remove_plugin(saveptr, config);
				if (ret < 0)
					goto out;

			} else
				ltfsmsg(LTFS_WARN, 11276W, tok);
		}

		free(saveline);
		saveline = NULL;
	}

	ret = 0;

out:
	if (saveline)
		free(saveline);
	fclose(conf_file);
	return ret;
}

/**
 * Validate a configuration structure.
 * @param config Structure to validate.
 * @return 0 on success or a negative value on error.
 */
int _config_file_validate(struct config_file *config)
{
	struct plugin_entry *de, *pe;
	struct stat st;
	bool found = false;

	/* Validate plugin configuration. For each configured plugin, return an error if the configured
	 * default plugin is not in	the list of known plugins. */
	TAILQ_FOREACH(de, &config->default_plugins, list) {
		found = false;
		TAILQ_FOREACH(pe, &config->plugins, list) {
			if (! strcmp(de->type, pe->type) && ! strcmp(de->name, pe->name))
				found = true;
		}
		if (! found && strcmp(de->name, "none")) {
			ltfsmsg(LTFS_ERR, 11280E, de->type, de->name);
			return -LTFS_CONFIG_INVALID;
		}
	}

	/* Emit a warning if plugin library does not exist. */
	TAILQ_FOREACH(pe, &config->plugins, list) {
		if (stat(pe->library, &st) < 0)
			ltfsmsg(LTFS_WARN, 11277W, pe->type, pe->name, pe->library);
	}

	return 0;
}

/**
 * Parse the tail end of a directive which takes a single entity name (plugin, file) as
 * an argument.
 * @param directive Name of the configuration directive.
 * @param name_desc Description of the directive.
 * @param saveptr State variable generated by strtok().
 * @param out On success, points to a newly allocated buffer containing the name.
 *            Undefined on failure.
 * @return 0 on success or a negative value on error.
 */
int _config_file_parse_name(const char *directive, const char *name_desc, char *saveptr, char **out)
{
	char *tok;

	if (*out) {
		free(*out);
		*out = NULL;
	}

#ifdef mingw_PLATFORM
	tok = strtok_r(NULL, "\t\r\n", &saveptr);
#else
	tok = strtok_r(NULL, " \t\r\n", &saveptr);
#endif
	if (! tok) {
		ltfsmsg(LTFS_ERR, 11273E, directive, name_desc);
		return -LTFS_CONFIG_INVALID;
	}
	*out = SAFE_STRDUP(tok);
	if (! (*out)) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return -LTFS_NO_MEMORY;
	}

	tok = strtok_r(NULL, " \t\r\n", &saveptr);
	if (tok) {
		ltfsmsg(LTFS_ERR, 11273E, directive, name_desc);
		return -LTFS_CONFIG_INVALID;
	}

	return 0;
}

/**
 * Parse the tail end of a "default" line.
 * The syntax is: default PLUGIN-TYPE PLUGIN-NAME, where PLUGIN-TYPE is one of "iosched"
 * or "driver".
 * @param saveptr State variable generated by strtok().
 * @param config Configuration structure to populate.
 * @return 0 on success or a negative value on error.
 */
int _config_file_parse_default(char *saveptr, struct config_file *config)
{
	bool found = false;
	char *tok;
	char *type, *name;
	struct plugin_entry *entry;

	/* Read the plugin type */
	tok = strtok_r(NULL, " \t\r\n", &saveptr);
	if (! tok) {
		ltfsmsg(LTFS_ERR, 11265E);
		return -LTFS_CONFIG_INVALID;
	}

	type = SAFE_STRDUP(tok);
	if (! type) {
		ltfsmsg(LTFS_ERR, 10001E, "_config_file_parse_default: plugin type");
		return -LTFS_NO_MEMORY;
	}

	/* Read the plugin name */
	tok = strtok_r(NULL, " \t\r\n", &saveptr);
	if (! tok) {
		ltfsmsg(LTFS_ERR, 11265E);
		free(type);
		return -LTFS_CONFIG_INVALID;
	}

	name = SAFE_STRDUP(tok);
	if (! name) {
		ltfsmsg(LTFS_ERR, 10001E, "_config_file_parse_default: plugin name");
		free(type);
		return -LTFS_NO_MEMORY;
	}

	/* Make sure there's no end of line garbage */
	tok = strtok_r(NULL, " \t\r\n", &saveptr);
	if (tok) {
		ltfsmsg(LTFS_ERR, 11265E);
		free(name);
		free(type);
		return -LTFS_CONFIG_INVALID;
	}

	/* Store the default */
	TAILQ_FOREACH(entry, &config->default_plugins, list) {
		if (! strcmp(entry->type, type)) {
			free(entry->name);
			entry->name = name;
			free(entry->type);
			entry->type = type;
			found = true;
		}
	}
	if (!found) {
		entry = calloc(1, sizeof(struct plugin_entry));
		if (! entry) {
			ltfsmsg(LTFS_ERR, 10001E, "_config_file_parse_default: plugin entry");
			free(name);
			free(type);
			return -LTFS_NO_MEMORY;
		}
		entry->type = type;
		entry->name = name;
		TAILQ_INSERT_TAIL(&config->default_plugins, entry, list);
	}

	return 0;
}

/**
 * Parse the tail end of a "-default" line.
 * The syntax is: -default PLUGIN-TYPE.
 * @param saveptr State variable generated by strtok().
 * @param config Configuration structure to modify.
 * @return 0 on success or a negative value on error.
 */
int _config_file_remove_default(char *saveptr, struct config_file *config)
{
	bool found = false;
	char *tok, *type;
	struct plugin_entry *pl, *aux;

	/* Read the plugin type */
	tok = strtok_r(NULL, " \t\r\n", &saveptr);
	if (! tok) {
		ltfsmsg(LTFS_ERR, 11270E);
		return -LTFS_CONFIG_INVALID;
	}

	type = SAFE_STRDUP(tok);
	if (! type) {
		ltfsmsg(LTFS_ERR, 10001E, "_config_file_remove_default: plugin type");
		return -LTFS_NO_MEMORY;
	}

	/* Make sure there's no end of line garbage */
	tok = strtok_r(NULL, " \t\r\n", &saveptr);
	if (tok) {
		ltfsmsg(LTFS_ERR, 11270E);
		free(type);
		return -LTFS_CONFIG_INVALID;
	}

	/* Remove the specified default value */
	TAILQ_FOREACH_SAFE(pl, &config->default_plugins, list, aux) {
		if (! strcmp(pl->type, type)) {
			TAILQ_REMOVE(&config->default_plugins, pl, list);
			free(pl->type);
			free(pl->name);
			free(pl);
			found = true;
		}
	}

	if (!found) {
		ltfsmsg(LTFS_ERR, 11271E, type);
		free(type);
		return -LTFS_CONFIG_INVALID;
	}

	free(type);
	return 0;
}

/**
 * Parse the tail end of a plugin line.
 * @param saveptr State variable generated by strtok().
 * @param config Configuration structure to modify.
 * @return 0 on success or a negative value on error.
 */
int _config_file_parse_plugin(char *saveptr, struct config_file *config)
{
	int ret;
	char *tok = NULL, *type = NULL, *name = NULL, *library = NULL;
	struct plugin_entry *entry;
	bool found = false;

	/* Get the plugin type */
	tok = strtok_r(NULL, " \t\r\n", &saveptr);
	if (! tok) {
		ltfsmsg(LTFS_ERR, 11275E);
		ret = -LTFS_CONFIG_INVALID;
		goto out_free;
	}

	type = SAFE_STRDUP(tok);
	if (! type) {
		ltfsmsg(LTFS_ERR, 10001E, "_config_file_parse_plugin: plugin type");
		ret = -LTFS_NO_MEMORY;
		goto out_free;
	}

	/* Get the driver name */
	tok = strtok_r(NULL, " \t\r\n", &saveptr);
	if (! tok) {
		ltfsmsg(LTFS_ERR, 11275E);
		ret = -LTFS_CONFIG_INVALID;
		goto out_free;
	}

	name = SAFE_STRDUP(tok);
	if (! name) {
		ltfsmsg(LTFS_ERR, 10001E, "_config_file_parse_plugin: plugin name");
		ret = -LTFS_NO_MEMORY;
		goto out_free;
	}

	/* Get the driver path */
	tok = strtok_r(NULL, "\r\n", &saveptr);
	if (! tok) {
		ltfsmsg(LTFS_ERR, 11275E);
		ret = -LTFS_CONFIG_INVALID;
		goto out_free;
	}

	library = SAFE_STRDUP(tok);
	if (! library) {
		ltfsmsg(LTFS_ERR, 10001E, "_config_file_parse_plugin: plugin path");
		ret = -LTFS_NO_MEMORY;
		goto out_free;
	}

	TAILQ_FOREACH(entry, &config->plugins, list) {
		if (! strcmp(entry->type, type) && ! strcmp(entry->name, name)) {
			free(type);
			free(name);
			free(entry->library);
			entry->library = library;
			found = true;
		}
	}
	if (!found) {
		entry = calloc(1, sizeof(struct plugin_entry));
		if (! entry) {
			ltfsmsg(LTFS_ERR, 10001E, "_config_file_parse_plugin: plugin entry");
			ret = -LTFS_NO_MEMORY;
			goto out_free;
		}
		entry->type = type;
		entry->name = name;
		entry->library = library;
		TAILQ_INSERT_TAIL(&config->plugins, entry, list);
	}

	return 0;

out_free:
	if (type)
		free(type);
	if (name)
		free(name);
	if (library)
		free(library);

	return ret;
}

/**
 * Parse the tail end of a "-plugin" line.
 * The syntax is: -plugin PLUGIN-TYPE PLUGIN-NAME
 * @param saveptr State variable generated by strtok().
 * @param config Configuration structure to modify.
 * @return 0 on success or a negative value on error.
 */
int _config_file_remove_plugin(char *saveptr, struct config_file *config)
{
	char *tok, *type, *name;
	struct plugin_entry *pl, *aux;

	/* Read the plugin type */
	tok = strtok_r(NULL, " \t\r\n", &saveptr);
	if (! tok) {
		ltfsmsg(LTFS_ERR, 11309E);
		return -LTFS_CONFIG_INVALID;
	}

	type = SAFE_STRDUP(tok);
	if (! type) {
		ltfsmsg(LTFS_ERR, 10001E, "_config_file_remove_plugin: plugin type");
		return -LTFS_NO_MEMORY;
	}

	/* Read the plugin name */
	tok = strtok_r(NULL, " \t\r\n", &saveptr);
	if (! tok) {
		ltfsmsg(LTFS_ERR, 11309E);
		free(type);
		return -LTFS_CONFIG_INVALID;
	}

	name = SAFE_STRDUP(tok);
	if (! name) {
		ltfsmsg(LTFS_ERR, 10001E, "_config_file_remove_plugin: plugin name");
		free(type);
		return -LTFS_NO_MEMORY;
	}

	/* Make sure there's no end of line garbage */
	tok = strtok_r(NULL, " \t\r\n", &saveptr);
	if (tok) {
		ltfsmsg(LTFS_ERR, 11309E);
		free(type);
		free(name);
		return -LTFS_CONFIG_INVALID;
	}

	/* Remove the specified plugin value */
	TAILQ_FOREACH_SAFE(pl, &config->plugins, list, aux) {
		if (! strcmp(pl->type, type) && ! strcmp(pl->name, name)) {
			TAILQ_REMOVE(&config->plugins, pl, list);
			free(pl->type);
			free(pl->name);
			free(pl->library);
			free(pl);
		}
	}

	free(type);
	free(name);
	return 0;
}

/**
 * Parse the tail end of an "option" line.
 * The syntax is: option TYPE OPTION, where OPTION is a mount option recognized by LTFS
 * or a configuration parameter of the Administration service.
 * @param line Reference variable containing the full contents of the line being parsed.
 * @param saveptr State variable generated by strtok().
 * @param out On success, points to a newly allocated option data structure. Its value is
 *            undefined on failure.
 * @return 0 on success or a negative value on error.
 */
int _config_file_parse_option(const char *line, char *saveptr, struct option_entry **out)
{
	int ret;
	char *tok, *type, *option, *start;
	bool is_admin_service = false;
	bool is_dcache = false;
	bool is_startup = false;
	bool is_snmp = false;

	/* Read the option type */
	tok = strtok_r(NULL, " \t\r\n", &saveptr);
	if (! tok) {
		/* Cannot parse configuration file: \'option\' directive must be followed
		 * by a option type and LTFS mount option */
		ltfsmsg(LTFS_ERR, 11272E);
		return -LTFS_CONFIG_INVALID;
	}
	start = tok;

	type = SAFE_STRDUP(tok);
	if (! type) {
		ltfsmsg(LTFS_ERR, 10001E, "_config_file_parse_mount_option: option");
		return -LTFS_NO_MEMORY;
	}

	if (! strcmp(type, "adminservice"))
		is_admin_service = true;
	else if (! strcmp(type, "dcache"))
		is_dcache = true;
	else if (! strcmp(type, "startup"))
		is_startup = true;
	else if (! strcmp(type, "snmp"))
		is_snmp = true;

	/* Read the option */
	tok = strtok_r(NULL, " \t\r\n", &saveptr);
	if (! tok) {
		/* Cannot parse configuration file: \'option\' directive must be followed
		 * by a LTFS mount option */
		ltfsmsg(LTFS_ERR, 11272E);
		free(type);
		return -LTFS_CONFIG_INVALID;
	}

	if (is_admin_service || is_dcache || is_startup || line[tok-start] == '-' || is_snmp)
		ret = asprintf(&option, "%s", &line[tok-start]);
	else
		ret = asprintf(&option, "-o%s", &line[tok-start]);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 10001E, "_config_file_parse_mount_option: option");
		free(type);
		return -LTFS_NO_MEMORY;
	}

	*out = calloc(1, sizeof(struct option_entry));
	if (! (*out)) {
		ltfsmsg(LTFS_ERR, 10001E, "_config_file_parse_plugin: plugin structure");
		free(type);
		free(option);
		return -LTFS_NO_MEMORY;
	}

	(*out)->type = type;
	(*out)->option = option;

	return 0;
}
