/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2019 IBM Corp. All rights reserved.
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
** FILE NAME:       libltfs/dcache.c
**
** DESCRIPTION:     Dentry Cache API implementation
**
** AUTHOR:          Lucas C. Villa Real
**                  IBM Brazil Research Lab
**                  lucasvr@br.ibm.com
**
*************************************************************************************
*/
#include <string.h>

#include "dcache.h"

struct dcache_priv {
	void *dlopen_handle;            /**< Handle returned from dlopen */
	struct libltfs_plugin *plugin;  /**< Reference to the plugin */
	struct dcache_ops *ops;         /**< Dentry cache manager operations */
	void *backend_handle;           /**< Backend private data */
};

/**
 * Initialize the Dentry cache manager.
 * @param plugin The plugin to take Dentry cache operations from.
 * @param vol LTFS volume
 * @return on success, 0 is returned and the Dentry cache handle is stored in the ltfs_volume
 * structure. On failure a negative value is returned.
 */

int dcache_init(struct libltfs_plugin *plugin, const struct dcache_options *options,
	struct ltfs_volume *vol)
{
	struct dcache_priv *priv;
	unsigned int i;

	CHECK_ARG_NULL(plugin, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);

	priv = calloc(1, sizeof(struct dcache_priv));
	if (! priv) {
		ltfsmsg(LTFS_ERR, 10001E, "dcache_init: private data");
		return -LTFS_NO_MEMORY;
	}

	priv->plugin = plugin;
	priv->ops = plugin->ops;

	/* Verify that backend implements all required operations */
	for (i=0; i<sizeof(struct dcache_ops)/sizeof(void *); ++i) {
		if (((void **)(priv->ops))[i] == NULL) {
			/* Dentry cache backend does not implement all required methods */
			ltfsmsg(LTFS_ERR, 13004E);
			free(priv);
			return -LTFS_PLUGIN_INCOMPLETE;
		}
	}

	priv->backend_handle = priv->ops->init(options, vol);
	if (! priv->backend_handle) {
		free(priv);
		return -1;
	}

	/*
	 * dcache initialization can be performed at any time, even before the tape is mounted.
	 * For that reason, the dcache_handle is attached to the LTFS Volume structure at this
	 * point. We lend the dcache_handle to the LTFS Index structure on dcache_load(), which
	 * must be called after the tape has been mounted.
	 */
	vol->dcache_handle = priv;
	return 0;
}

/**
 * Destroy the Dentry cache manager.
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
int dcache_destroy(struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;
	int ret;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->destroy, -LTFS_NULL_ARG);

	ret = priv->ops->destroy(priv->backend_handle);
	vol->dcache_handle = NULL;
	free(priv);

	return ret;
}

/**
 * Parse dcache options parsed from the LTFS configuration file.
 * @param options NULL terminated list of options to parse
 * @param[out] out On success, contains an allocated and populated dcache_options structure.
 * @return 0 on success or a negative value on error.
 */
int dcache_parse_options(const char **options, struct dcache_options **out)
{
	char *option, *value, *line = NULL;
	struct dcache_options *opt;
	int i, ret = 0;

	CHECK_ARG_NULL(options, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(out, -LTFS_NULL_ARG);

	*out = NULL;

	opt = calloc(1, sizeof(struct dcache_options));
	if (! opt) {
		ltfsmsg(LTFS_ERR, 10001E, "dcache_parse_options: opt");
		return -ENOMEM;
	}

	for (i=0; options[i]; ++i) {
		line = strdup(options[i]);
		if (! line) {
			ltfsmsg(LTFS_ERR, 10001E, "dcache_parse_options: line");
			ret = -ENOMEM;
			goto out_free;
		}
		option = strtok(line, " \t");
		if (! option) {
			/* Failed to parse LTFS dcache configuration rules: invalid option '%s' */
			ltfsmsg(LTFS_ERR, 17170E, options[i]);
			ret = -EINVAL;
			goto out_free;
		}

		if (! strcmp(option, "enabled")) {
			opt->enabled = true;
			free(line);
			line = NULL;
			continue;
		} else if (! strcmp(option, "disabled")) {
			opt->enabled = false;
			free(line);
			line = NULL;
			continue;
		}

		value = strtok(NULL, " \t");
		if (! value) {
			/* Failed to parse LTFS dcache configuration rules: invalid option '%s' */
			ltfsmsg(LTFS_ERR, 17170E, options[i]);
			ret = -EINVAL;
			goto out_free;
		}

		if (! strcmp(option, "minsize")) {
			opt->minsize = atoi(value);
			if (opt->minsize <= 0) {
				/* Failed to parse LTFS dcache configuration rules: invalid value '%d' for option '%s' */
				ltfsmsg(LTFS_ERR, 17171E, opt->minsize, option);
				ret = -EINVAL;
				goto out_free;
			}
		} else if (! strcmp(option, "maxsize")) {
			opt->maxsize = atoi(value);
			if (opt->maxsize <= 0) {
				/* Failed to parse LTFS dcache configuration rules: invalid value '%d' for option '%s' */
				ltfsmsg(LTFS_ERR, 17171E, opt->maxsize, option);
				ret = -EINVAL;
				goto out_free;
			}
		} else {
			/* Failed to parse LTFS dcache configuration rules: invalid option '%s' */
			ltfsmsg(LTFS_ERR, 17170E, options[i]);
			ret = -EINVAL;
			goto out_free;
		}

		free(line);
		line = NULL;
	}

	*out = opt;

out_free:
	if (ret != 0 && opt)
		dcache_free_options(&opt);
	if (line)
		free(line);
	return ret;
}

/**
 * Free a previously allocated dcache_options structure.
 * @param options dcache structure
 */
void dcache_free_options(struct dcache_options **options)
{
	if (options && *options) {
		free(*options);
		*options = NULL;
	}
}

/**
 * Checks if the Dentry cache manager has been initialized for the given volume
 * @param vol LTFS volume
 * @return true to indicate that the Dentry cache manager has been initialized or false if not
 */
bool dcache_initialized(struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;
	bool assigned = false;

	CHECK_ARG_NULL(vol, false);

	if (priv) {
		CHECK_ARG_NULL(priv->ops, false);
		CHECK_ARG_NULL(priv->ops->is_name_assigned, false);
		priv->ops->is_name_assigned(&assigned, priv->backend_handle);
	}

	return assigned;
}

/**
 * Create a new Dentry cache for a given cartridge.
 * @param name Name of the cache to create
 * @param vol LTFS volume
 * @return 0 to indicate success or a negative value on error.
 */
int dcache_mkcache(const char *name, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(name, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->mkcache, -LTFS_NULL_ARG);

	return priv->ops->mkcache(name, priv->backend_handle);
}

/**
 * Remove the Dentry cache of a given cartridge.
 * @param name Name of the cache to remove
 * @param vol LTFS volume
 * @return 0 to indicate success or a negative value on error.
 */
int dcache_rmcache(const char *name, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(name, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->rmcache, -LTFS_NULL_ARG);

	return priv->ops->rmcache(name, priv->backend_handle);
}

/**
 * Verify if the cache of a specific cartridge exists.
 * @param name Name of the cache to verify
 * @param[out] exists Outputs 'true' or 'false' after a successful call to this function.
 * @param[out] dirty If the cache exists, then contains the status of the cache's dirty flag.
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
int dcache_cache_exists(const char *name, bool *exists, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(name, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(exists, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->cache_exists, -LTFS_NULL_ARG);

	return priv->ops->cache_exists(name, exists, priv->backend_handle);
}

/**
 * Set the Configure the Dentry cache work directory.
 * @param workdir LTFS work directory
 * @param clean clean work directory
 * @param vol LTFS volume
 * @return 0 to indicate success or a negative value on error.
 */
int dcache_set_workdir(const char *workdir, bool clean, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(workdir, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->set_workdir, -LTFS_NULL_ARG);

	return priv->ops->set_workdir(workdir, clean, priv->backend_handle);
}

/**
 * Get the Configure the Dentry cache work directory.
 * @param[out] workdir On success, contains a pointer to the LTFS work directory
 * @param vol LTFS volume
 * @return 0 to indicate success or a negative value on error.
 */
int dcache_get_workdir(char **workdir, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(workdir, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->get_workdir, -LTFS_NULL_ARG);

	return priv->ops->get_workdir(workdir, priv->backend_handle);
}

/**
 * Load the Dentry cache for a given cartridge.
 * @param name Name of the cache to load.
 * @param vol LTFS volume of the cartridge. Must have been initialized with dcache_init().
 * @return 0 to indicate success or a negative value on error.
 */
int dcache_assign_name(const char *name, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(name, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol->index, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->assign_name, -LTFS_NULL_ARG);

	return priv->ops->assign_name(name, priv->backend_handle);
}

/**
 * Unload the Dentry cache.
 * @param vol LTFS volume of the cartridge. Must have been initialized with dcache_init().
 * @return 0 to indicate success or a negative value on error.
 */
int dcache_unassign_name(struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol->index, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->unassign_name, -LTFS_NULL_ARG);

	return priv->ops->unassign_name(priv->backend_handle);
}

/**
 * Free in-memory dentry tree to reduce memory usage
 * @param vol LTFS volume of the cartridge. Must have been initialized with dcache_init().
 * @return 0 to indicate success or a negative value on error.
 */
int dcache_wipe_dentry_tree(struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol->index->root, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->wipe_dentry_tree, -LTFS_NULL_ARG);

	return priv->ops->wipe_dentry_tree(priv->backend_handle);
}

/**
 * Get the volume UUID stored in dcache space.
 * @param work_dir Work directory.
 * @param barcode barcode
 * @param uuid	Pointer to return the volume UUID.
 * @param vol LTFS volume of the cartridge. Must have been initialized with dcache_init().
 * @return 0 to indicate success or a negative value on error.
 */
int dcache_get_vol_uuid(const char *work_dir, const char *barcode, char  **uuid, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(uuid, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->get_vol_uuid, -LTFS_NULL_ARG);

	return priv->ops->get_vol_uuid(work_dir, barcode, uuid);
}

/**
 * Set the volume UUID.
 * @param uuid Volume UUID to store into dcache space.
 * @param vol LTFS volume of the cartridge. Must have been initialized with dcache_init().
 * @return 0 to indicate success or a negative value on error.
 */
int dcache_set_vol_uuid(char *uuid, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(uuid, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->set_vol_uuid, -LTFS_NULL_ARG);

	return priv->ops->set_vol_uuid(uuid, priv->backend_handle);
}

/**
 * Get the generation code stored in dcache space.
 * @param work_dir Work directory.
 * @param barcode barcode
 * @param gen Pointer to return the generation code.
 * @param vol LTFS volume of the cartridge. Must have been initialized with dcache_init().
 * @return 0 to indicate success or a negative value on error.
 */
int dcache_get_generation(const char *work_dir, const char *barcode, unsigned int *gen, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(gen, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->get_generation, -LTFS_NULL_ARG);

	return priv->ops->get_generation(work_dir, barcode, gen);
}

/**
 * Set the generation code into dcache space
 * @param gen Generation code to store into dcache space.
 * @param vol LTFS volume of the cartridge. Must have been initialized with dcache_init().
 * @return 0 to indicate success or a negative value on error.
 */
int dcache_set_generation(unsigned int gen, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->set_generation, -LTFS_NULL_ARG);

	return priv->ops->set_generation(gen, priv->backend_handle);
}

/**
 * Get the generation code.
 * @param work_dir Work directory.
 * @param barcode barcode
 * @param dirty	Pointer to return the dirty flag.
 * @param vol LTFS volume of the cartridge. Must have been initialized with dcache_init().
 * @return 0 to indicate success or a negative value on error.
 */
int dcache_get_dirty(const char *work_dir, const char *barcode, bool *dirty, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(dirty, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->get_dirty, -LTFS_NULL_ARG);

	return priv->ops->get_dirty(work_dir, barcode, dirty);
}

/**
 * Set the Dentry cache 'dirty' flag.
 * @param dirty True to indicate that the disk-cache is out of sync with the latest LTFS Index,
 *        False otherwise.
 * @param vol LTFS volume of the cartridge. Must have been initialized with dcache_init().
 * @return 0 to indicate success or a negative value on error.
 */
int dcache_set_dirty(bool dirty, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->set_dirty, -LTFS_NULL_ARG);

	return priv->ops->set_dirty(dirty, priv->backend_handle);
}

/**
 * Create a new disk image.
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
int dcache_diskimage_create(struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->diskimage_create, -LTFS_NULL_ARG);

	return priv->ops->diskimage_create(priv->backend_handle);
}

/**
 * Remove disk image.
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
int dcache_diskimage_remove(struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->diskimage_remove, -LTFS_NULL_ARG);

	return priv->ops->diskimage_remove(priv->backend_handle);
}

/**
 * Mount a disk image.
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
int dcache_diskimage_mount(struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->diskimage_mount, -LTFS_NULL_ARG);

	return priv->ops->diskimage_mount(priv->backend_handle);
}

/**
 * Unmount a disk image.
 * @param vol LTFS volume
 * @return 0 on success or a negative value on error.
 */
int dcache_diskimage_unmount(struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->diskimage_unmount, -LTFS_NULL_ARG);

	return priv->ops->diskimage_unmount(priv->backend_handle);
}

bool dcache_diskimage_is_full(struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->diskimage_is_full, -LTFS_NULL_ARG);

	return priv->ops->diskimage_is_full();
}

int dcache_get_advisory_lock(const char *name, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(name, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->get_advisory_lock, -LTFS_NULL_ARG);

	return priv->ops->get_advisory_lock(name, priv->backend_handle);
}

int dcache_put_advisory_lock(const char *name, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(name, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->put_advisory_lock, -LTFS_NULL_ARG);

	return priv->ops->put_advisory_lock(name, priv->backend_handle);
}

int dcache_open(const char *path, struct dentry **d, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(path, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->open, -LTFS_NULL_ARG);

	return priv->ops->open(path, d, priv->backend_handle);
}

int dcache_close(struct dentry *d, bool lock_meta, bool descend, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->close, -LTFS_NULL_ARG);

	return priv->ops->close(d, lock_meta, descend, priv->backend_handle);
}

int dcache_create(const char *path, struct dentry *d, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(path, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->create, -LTFS_NULL_ARG);

	return priv->ops->create(path, d, priv->backend_handle);
}

int dcache_unlink(const char *path, struct dentry *d, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(path, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->unlink, -LTFS_NULL_ARG);

	return priv->ops->unlink(path, d, priv->backend_handle);
}

int dcache_rename(const char *oldpath, const char *newpath, struct dentry **old_dentry,
	struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(oldpath, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(newpath, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(old_dentry, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->rename, -LTFS_NULL_ARG);

	return priv->ops->rename(oldpath, newpath, old_dentry, priv->backend_handle);
}

int dcache_flush(struct dentry *d, enum dcache_flush_flags flags, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->flush, -LTFS_NULL_ARG);

	if (! d) {
		/* The I/O scheduler handles NULL dentries in a special case. We just need to ignore them. */
		return 0;
	}
	return priv->ops->flush(d, flags, priv->backend_handle);
}

int dcache_readdir(struct dentry *d, bool dentries, void ***result, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(result, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->readdir, -LTFS_NULL_ARG);

	return priv->ops->readdir(d, dentries, result, priv->backend_handle);
}

int dcache_read_direntry(struct dentry *d, struct ltfs_direntry *dirent, unsigned long index,  struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dirent, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->read_direntry, -LTFS_NULL_ARG);

	return priv->ops->read_direntry(d, dirent, index, priv->backend_handle);
}

int dcache_get_dentry(struct dentry *d, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->get_dentry, -LTFS_NULL_ARG);

	return priv->ops->get_dentry(d, priv->backend_handle);
}

int dcache_put_dentry(struct dentry *d, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->put_dentry, -LTFS_NULL_ARG);

	return priv->ops->put_dentry(d, priv->backend_handle);
}

int dcache_openat(const char *parent_path, struct dentry *parent, const char *name,
	struct dentry **result, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(parent_path, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(parent, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(name, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(result, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->openat, -LTFS_NULL_ARG);

	return priv->ops->openat(parent_path, parent, name, result, priv->backend_handle);
}

int dcache_setxattr(const char *path, struct dentry *d, const char *xattr, const char *value,
	size_t size, int flags, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(path, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(xattr, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(value, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->setxattr, -LTFS_NULL_ARG);

	return priv->ops->setxattr(path, d, xattr, value, size, flags, priv->backend_handle);
}

int dcache_removexattr(const char *path, struct dentry *d, const char *xattr,
	struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(path, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(xattr, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->removexattr, -LTFS_NULL_ARG);

	return priv->ops->removexattr(path, d, xattr, priv->backend_handle);
}

int dcache_listxattr(const char *path, struct dentry *d, char *list, size_t size,
	struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(path, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->listxattr, -LTFS_NULL_ARG);

	return priv->ops->listxattr(path, d, list, size, priv->backend_handle);
}

int dcache_getxattr(const char *path, struct dentry *d, const char *name,
		void *value, size_t size, struct ltfs_volume *vol)
{
	struct dcache_priv *priv = (struct dcache_priv *) vol ? vol->dcache_handle : NULL;

	CHECK_ARG_NULL(path, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(d, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(name, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(vol, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(priv->ops->getxattr, -LTFS_NULL_ARG);

	return priv->ops->getxattr(path, d, name, value, size, priv->backend_handle);
}
