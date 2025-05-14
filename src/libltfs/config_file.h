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
** FILE NAME:       config_file.h
**
** DESCRIPTION:     Declares the interface for reading LTFS configuration files.
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

#ifndef __CONFIG_FILE_H__
#define __CONFIG_FILE_H__

#include "queue.h"

struct config_file;

/**
 * Read LTFS configuration information from the given file.
 * @param path File to read. If NULL, the default path is used.
 * @param config On success, points to a newly allocated configuration data structure.
 *               Its value is undefined on failure.
 * @return 0 on success or a negative value on error.
 */
int config_file_load(const char *path, struct config_file **config);

/**
 * Free an LTFS configuration structure.
 * @param config Configuration structure to free.
 */
void config_file_free(struct config_file *config);

/**
 * Read the default plugin from a config file structure.
 * @param type Plugin type.
 * @param config Configuration data structure to read. 
 * @return The default driver name, or NULL on error.
 */
const char *config_file_get_default_plugin(const char *type, struct config_file *config);

/**
 * Get the library path for a given plugin.
 * @param type Plugin type.
 * @param name Plugin to look up.
 * @param config Configuration structure to search.
 * @return The library path on success, or NULL on error.
 */
const char *config_file_get_lib(const char *type, const char *name, struct config_file *config);

/**
 * Get a list of all plugins found in the configuration file.
 * @param type Plugin type.
 * @param config Configuration structure to search.
 * @return A NULL-terminated list of plugin names on success, or NULL on failure.
 */
char **config_file_get_plugins(const char *type, struct config_file *config);

/**
 * Get a list of all default options found in the configuration file. 
 * @param type Option type 
 * @param config Configuration structure to search.
 * @return a NULL-terminated list of option names on success, or NULL on failure.
 */
char **config_file_get_options(const char *type, struct config_file *config);

#endif /* __CONFIG_FILE_H__ */

