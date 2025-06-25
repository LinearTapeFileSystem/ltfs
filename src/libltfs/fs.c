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
** FILE NAME:       fs.c
**
** DESCRIPTION:     Implements facilities to deal with the file system tree.
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

#include <sched.h>
#include <string.h>

#include "libltfs/ltfslogging.h"
#include "pathname.h"
#include "arch/filename_handling.h"
#include "dcache.h"
#include "fs.h"

#define TRUNCATE_STRING(end) do { if ((end)) *(end) = '\0'; } while(0)
#define RESTORE_STRING(end)  do { if ((end)) *(end) =  '/'; } while(0)

int fs_hash_sort_by_uid(struct name_list *a, struct name_list *b)
{
	return (a->uid - b->uid);
}

static char* generate_hash_key_name(const char *src_str, int *rc)
{
	char *key_name;

#ifdef mingw_PLATFORM
	UChar *uchar_name;

	*rc =  pathname_prepare_caseless(src_str, &uchar_name, true);	// malloc is called in this function
	if (*rc == 0) {
		*rc = _pathname_utf16_to_utf8_icu(uchar_name, &key_name);	// malloc is called in this funcation
	}

	if (*rc != 0) {
		key_name = NULL;
	} else
		free(uchar_name);
#else
	/* Checkup not needed here, memory error handling happens after function call. */
	key_name = strdup(src_str);
	*rc = 0;
#endif

	return key_name;
}

struct name_list* fs_add_key_to_hash_table(struct name_list *list, struct dentry *add_entry, int *rc)
{
	struct name_list *new_list = NULL;

	new_list = (struct name_list *) malloc(sizeof(struct name_list));
	if (!new_list) {
		ltfsmsg(LTFS_ERR, 10001E, "fs_add_key_to_hash_table: new list");
		*rc = -LTFS_NO_MEMORY;
		return list;
	}

	new_list->name = generate_hash_key_name(add_entry->platform_safe_name, rc);

	if (*rc == 0 && new_list->name != NULL)	{
		errno = 0;
		new_list->d = add_entry;
		new_list->uid = add_entry->uid;
		HASH_ADD_KEYPTR(hh, list, new_list->name, strlen(new_list->name), new_list);
		if (errno == ENOMEM) {
			ltfsmsg(LTFS_ERR, 10001E, "fs_add_key_to_hash_table: add key");
			*rc = -LTFS_NO_MEMORY;
		}
	}

	return list;
}

struct name_list* fs_find_key_from_hash_table(struct name_list *list, const char *name, int *rc)
{
	struct name_list *result;
	char *key_name;

	key_name = generate_hash_key_name(name, rc);
	if (key_name != NULL) {
		HASH_FIND_STR(list, key_name, result);
	}
	else  {
		result = NULL;
	}

	free(key_name);

	return result;
}

/**
 * Increment the filesystem file count
 *
 * @param idx ltfs index structure
 */
void fs_increment_file_count(struct ltfs_index *idx)
{
	ltfs_mutex_lock(&idx->dirty_lock);
	idx->file_count++;
	ltfs_mutex_unlock(&idx->dirty_lock);
}

/**
 * Decrement the filesystem file count
 *
 * @param idx ltfs index structure
 */
void fs_decrement_file_count(struct ltfs_index *idx)
{
	ltfs_mutex_lock(&idx->dirty_lock);
	idx->file_count--;
	ltfs_mutex_unlock(&idx->dirty_lock);
}

/**
 * Initialize inode number structure
 *
 */
static ltfs_mutex_t inode_mutex;
static ino_t inode_number = 0;

int fs_init_inode(void)
{
	int ret;

	ret = ltfs_mutex_init(&inode_mutex);
	if (ret)
		ltfsmsg(LTFS_ERR, 10002E, ret);

	return ret;
}

/**
 * Check given string requires percent encoded or not in XML
 */
bool fs_is_percent_encode_required(const char *name)
{
	int len, i;
	bool need_encode = false;

	if (name) {
		len = strlen(name);

		for (i=0; i<len; i++) {
			if (name[i] == 0x3A
			 || (name[i]>=0 && name[i]<=0x1F && name[i]!=0x09 && name[i]!=0x0A && name[i]!=0x0D)) {
				need_encode = true;
				break;
			}
		}
	}

	return need_encode;
}

/**
 * Clear name type structure
 */
void fs_clear_nametype(struct ltfs_name *name)
{
	if (name->name)
		free(name->name);

	name->percent_encode = false;
	name->name = NULL;
}

/**
 * Set name type structure
 */
void fs_set_nametype(struct ltfs_name *name, char *str)
{
	if (name) {
		fs_clear_nametype(name);
		name->name = str;
		name->percent_encode = fs_is_percent_encode_required(str);
	}
}

/**
 * Allocate a new dentry object
 *
 * The caller must have a write lock held on @parent if it's not NULL.
 *
 * @param parent a pointer to the parent's dentry structure.
 * @param name object's name in the Tape FileSystem.
 * @param mode object's permission bits.
 * @Param increment_parent_handles if true, descend the tree incrementing each parent's dentry
 *  usage counter until the root dentry is found.
 * @param priv private data.
 * @return the new allocated object, or NULL on failure. If a previous entry has been already
 * allocated for this object, a pointer to that object is returned instead.
 */
struct dentry * fs_allocate_dentry(struct dentry *parent, const char *name, const char *platform_safe_name,
	bool isdir, bool readonly, bool allocate_uid, struct ltfs_index *idx)
{
	int ret;
	struct dentry *d = NULL;

	d = (struct dentry *) malloc(sizeof(struct dentry));
	if (! d) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return NULL;
	}

	memset(d, 0, sizeof(struct dentry));
	d->parent = parent;
	if (!name && !platform_safe_name) {
		d->name.name = NULL;
		d->platform_safe_name = NULL;
	} else if (name && !platform_safe_name) {
		d->name.name = strdup(name);
		update_platform_safe_name(d, false, idx);
		if (! d->name.name || ! d->platform_safe_name) {
			ltfsmsg(LTFS_ERR, 10001E, "fs_allocate_dentry: name");
			if (d->name.name)
				free(d->name.name);
			if (d->platform_safe_name)
				free(d->platform_safe_name);
			free(d);
			return NULL;
		}
	} else if(!name && platform_safe_name) {
		d->name.name = strdup(platform_safe_name);
		d->platform_safe_name = strdup(platform_safe_name);
		if (! d->name.name || ! d->platform_safe_name) {
			ltfsmsg(LTFS_ERR, 10001E, "fs_allocate_dentry: name");
			if (d->name.name)
				free(d->name.name);
			if (d->platform_safe_name)
				free(d->platform_safe_name);
			free(d);
			return NULL;
		}
	} else {
		/* Currently, it can be assumed that one of these names should
		   be NULL. The codes below are just in case. */
		d->name.name = strdup(name);
		d->platform_safe_name = strdup(platform_safe_name);
		if (! d->name.name || ! d->platform_safe_name) {
			ltfsmsg(LTFS_ERR, 10001E, "fs_allocate_dentry: name");
			if (d->name.name)
				free(d->name.name);
			if (d->platform_safe_name)
				free(d->platform_safe_name);
			free(d);
			return NULL;
		}
	}
	d->isdir = isdir;
	d->readonly = readonly;
	d->numhandles = 1;
	d->link_count = 0;
	d->name.percent_encode = fs_is_percent_encode_required(d->name.name);

	if (isdir)
		++d->link_count;
	ltfs_mutex_lock(&inode_mutex);
	d->ino = ++inode_number;
	ltfs_mutex_unlock(&inode_mutex);
	if (allocate_uid)
		d->uid = fs_allocate_uid(idx);
	else
		d->uid = 1; /* When allocating root directory, use default UID */
	if (d->uid == 0) {
		/* UID allocation failed because the UID space overflowed. Refuse to create a new file. */
		if (d->name.name)
			free(d->name.name);
		if (d->platform_safe_name)
			free(d->platform_safe_name);
		free(d);
		return NULL;
	}
	ret = init_mrsw(&d->contents_lock);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 10002E, ret);
		if (d->name.name)
			free(d->name.name);
		if (d->platform_safe_name)
			free(d->platform_safe_name);
		free(d);
		return NULL;
	}
	ret = init_mrsw(&d->meta_lock);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 10002E, ret);
		destroy_mrsw(&d->contents_lock);
		if (d->name.name)
			free(d->name.name);
		if (d->platform_safe_name)
			free(d->platform_safe_name);
		free(d);
		return NULL;
	}
	d->child_list=NULL;
	TAILQ_INIT(&d->extentlist);
	TAILQ_INIT(&d->xattrlist);

	ret = ltfs_mutex_init(&d->iosched_lock);
	if (ret) {
		ltfsmsg(LTFS_ERR, 10002E, ret);
		destroy_mrsw(&d->contents_lock);
		destroy_mrsw(&d->meta_lock);
		if (d->name.name)
			free(d->name.name);
		if (d->platform_safe_name)
			free(d->platform_safe_name);
		free(d);
		return NULL;
	}

	d->tag_count = 0;
	d->preserved_tags = NULL;

	if (parent) {
		acquirewrite_mrsw(&parent->contents_lock);
		acquirewrite_mrsw(&parent->meta_lock);
		if (d->platform_safe_name != NULL) {
			parent->child_list = fs_add_key_to_hash_table(parent->child_list, d, &ret);
			if (ret != 0) {
				ltfsmsg(LTFS_ERR, 11319E, "fs_allocate_dentry", ret);
				releasewrite_mrsw(&parent->meta_lock);
				releasewrite_mrsw(&parent->contents_lock);
				if (d->name.name)
					free(d->name.name);
				if (d->platform_safe_name)
					free(d->platform_safe_name);
				free(d);
				return NULL;
			}
		}
		/* The volume initialization assumes that the parent data has been set before */
		d->vol = parent->vol;
		d->link_count++;
		if (isdir)
			parent->link_count++;
		releasewrite_mrsw(&parent->meta_lock);
		releasewrite_mrsw(&parent->contents_lock);
		if (! isdir)
			fs_increment_file_count(idx);
	}

	return d;
}

/**
 * Allocate a new uid
 *
 * @return the new allocated uid.
 */
uint64_t fs_allocate_uid(struct ltfs_index *idx)
{
	uint64_t uid;
	ltfs_mutex_lock(&idx->dirty_lock);
	if (idx->uid_number == 0)
		uid = 0;
	else {
		uid = ++idx->uid_number;
		if (uid == 0)
			ltfsmsg(LTFS_WARN, 11307W, idx->vol_uuid);
	}
	ltfs_mutex_unlock(&idx->dirty_lock);

	return uid;
}

int fs_dentry_lookup(struct dentry *dentry, char **name)
{
	char **dentry_names = NULL, *tmp_name = NULL;
	int i, names, namelen = 0, ret = 0;
	struct dentry *d, *parent;
	const char *lookup_name;

	CHECK_ARG_NULL(dentry, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(name, -LTFS_NULL_ARG);

	*name = NULL;

	d = dentry;
	for (names=0; d != NULL; ++names)
		d = d->parent;

	dentry_names = (char **) calloc(names+1, sizeof(char *));
	if (! dentry_names)  {
		ltfsmsg(LTFS_ERR, 10001E, "fs_dentry_lookup: dentry_names");
		return -LTFS_NO_MEMORY;
	}

	d = dentry;
	parent = d->parent;
	for (i=names-1; i>=0; --i) {
		if (parent)
			acquireread_mrsw(&parent->contents_lock);

		lookup_name = (const char *) d->platform_safe_name;
		if (! lookup_name) {
			if (d->deleted || d->parent) {
				ret = -LTFS_NO_DENTRY;
				goto out;
			}
			lookup_name = "/";
		}
		dentry_names[i] = strdup(lookup_name);
		if (! dentry_names[i]) {
			ltfsmsg(LTFS_ERR, 10001E, "fs_dentry_lookup: dentry_names member");
			goto out;
		}
		namelen += strlen(lookup_name);

		if (parent)
			releaseread_mrsw(&parent->contents_lock);

		d = parent;
		if (! d)
			break;
		parent = d->parent;
	}

	tmp_name = calloc(namelen + names, sizeof(char));
	if (! tmp_name) {
		ltfsmsg(LTFS_ERR, 10001E, "fs_dentry_lookup: tmp_name");
		ret = -LTFS_NO_MEMORY;
		goto out;
	}

	for (namelen=0, i=0; i<names; ++i) {
		strcat(tmp_name, dentry_names[i]);
		if (i > 0 && i < names-1)
			strcat(tmp_name, "/");
	}

	ret = 0;
	*name = tmp_name;

out:
	if (ret != 0 && tmp_name)
		free(tmp_name);
	if (dentry_names) { /* BEAM: constant condition - dentry_names has always non-zero value here. */
		while (--names >= 0)
			if (dentry_names[names])
				free(dentry_names[names]);
		free(dentry_names);
	}
	return ret;
}

int fs_directory_lookup(struct dentry *basedir, const char *name, struct dentry **dentry)
{
	struct name_list *namelist;
	int rc;

	CHECK_ARG_NULL(basedir, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(name, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dentry, -LTFS_NULL_ARG);

	*dentry = NULL;

	if (pathname_strlen(name) > LTFS_FILENAME_MAX)
		return -LTFS_NAMETOOLONG;

	if (HASH_COUNT(basedir->child_list) == 0)
		return 0;

	namelist = fs_find_key_from_hash_table(basedir->child_list, name, &rc);
	if (rc != 0) {
		/* Can only happen in a case-insensitive environment (Windows) */
        ltfsmsg(LTFS_ERR, 11320E, "fs_directory_lookup", rc);
		return rc;
	}

	if (namelist) {
		acquirewrite_mrsw(&namelist->d->meta_lock);
		++namelist->d->numhandles;
		releasewrite_mrsw(&namelist->d->meta_lock);
		*dentry = namelist->d;
		return 0;
	}

	/* No dentry found, return success with dentry set to NULL */
	return 0;
}

int fs_path_lookup(const char *path, int flags, struct dentry **dentry, struct ltfs_index *idx)
{
	int ret = 0;
	struct dentry *d = NULL, *parent = NULL;
	char *tmp_path, *start, *end;

	CHECK_ARG_NULL(path, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(dentry, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(idx, -LTFS_NULL_ARG);

	tmp_path = strdup(path);
	if (! tmp_path) {
		ltfsmsg(LTFS_ERR, 10001E, "fs_path_lookup: tmp_path");
		return -LTFS_NO_MEMORY;
	}

	/* Get a reference count on the root dentry. Either it will be returned immediately, or it
	 * will be disposed later after the first path lookup. */
	acquirewrite_mrsw(&idx->root->meta_lock);
	++idx->root->numhandles;
	releasewrite_mrsw(&idx->root->meta_lock);

	/* Did the caller ask for the root dentry? */
	if (*path == '\0' || ! strcmp(path, "/")) {
		d = idx->root;
		goto out;
	}

	start = tmp_path + 1;
	end = tmp_path;
	d = idx->root;

	while (end) {
		end = strstr(start, "/");
		if (end)
			*end = '\0';

		if (! end && (flags & LOCK_PARENT_CONTENTS_W))
			acquirewrite_mrsw(&d->contents_lock);
		else
			acquireread_mrsw(&d->contents_lock);

		if (parent)
			releaseread_mrsw(&parent->contents_lock);
		parent = d;
		d = NULL;

		ret = fs_directory_lookup(parent, start, &d);
		if (ret < 0 || ! d) {
			if (! end && (flags & LOCK_PARENT_CONTENTS_W))
				releasewrite_mrsw(&parent->contents_lock);
			else
				releaseread_mrsw(&parent->contents_lock);
			fs_release_dentry(parent);

			if (ret == 0)
				ret = -LTFS_NO_DENTRY;
			goto out;
		}

		/* Release the parent if we aren't keeping any locks on it.
		 * Since we know 'parent' has a child (d), it's guaranteed that parent is still linked
		 * into the file system tree. Therefore, fs_release_dentry is just a fancy way of
		 * decrementing the handle count... so do that. */
		if (end || ! (flags & (LOCK_PARENT_CONTENTS_W | LOCK_PARENT_CONTENTS_R
			| LOCK_PARENT_META_W | LOCK_PARENT_META_R))) {
			acquirewrite_mrsw(&parent->meta_lock);
			--parent->numhandles;
			releasewrite_mrsw(&parent->meta_lock);
		}

		if (end)
			start = end + 1;
	}

	if (! (flags & (LOCK_PARENT_CONTENTS_W | LOCK_PARENT_CONTENTS_R)))
		releaseread_mrsw(&parent->contents_lock);

out:
	free(tmp_path);

	if (ret == 0) {
		if (parent) {
			/* Parent contents_lock was already taken appropriately above */
			if (flags & LOCK_PARENT_META_W)
				acquirewrite_mrsw(&parent->meta_lock);
			else if (flags & LOCK_PARENT_META_R)
				acquireread_mrsw(&parent->meta_lock);
		}

		if (flags & LOCK_DENTRY_CONTENTS_W)
			acquirewrite_mrsw(&d->contents_lock);
		else if (flags & LOCK_DENTRY_CONTENTS_R)
			acquireread_mrsw(&d->contents_lock);
		if (flags & LOCK_DENTRY_META_W)
			acquirewrite_mrsw(&d->meta_lock);
		else if (flags & LOCK_DENTRY_META_R)
			acquireread_mrsw(&d->meta_lock);

		*dentry = d;
	}

	return ret;
}

void fs_split_path(char *path, char **filename, size_t len)
{
	char *ptr;
	for (ptr=&path[len-1]; ptr>=path; --ptr) {
		if (*ptr == '/') {
			*ptr = '\0';
			*filename = ptr + 1;
			return;
		}
	}
}

/**
 * Dispose a dentry and all resources used by it, including the struct dentry itself.
 * @param dentry dentry to dispose.
 * @param unlock true if function was called with write lock on dentry->meta_lock, false if not.
 * @param gc true for garbage collection. The reference counter
 *  		 numhandles is not decremeted.
 */
void _fs_dispose_dentry_contents(struct dentry *dentry, bool unlock, bool gc)
{
	size_t i;
	char *name;
	struct extent_info *ext_entry, *ext_aux;
	struct xattr_info *xattr_entry, *xattr_aux;
	struct name_list *namelist;
	int rc;

	if (HASH_COUNT(dentry->child_list) != 0) {
		struct name_list *child, *aux;
		HASH_ITER(hh, dentry->child_list, child, aux) {

			/* Remove child from the tree first */
			HASH_DEL(dentry->child_list, child);
			if (child->d->parent)
				child->d->parent = NULL;

			if (! gc) {
				if (child->d->numhandles != 1) {
					/* Unable to delete dentry '%s': it still has outstanding references */
					name = child->d->platform_safe_name ? child->d->platform_safe_name : "(null)";
					ltfsmsg(LTFS_WARN, 11998W, name);
				} else {
					child->d->numhandles--;
					_fs_dispose_dentry_contents(child->d, false, gc);
				}
			} else {
				if (child->d->numhandles != 0) {
					/* Unable to delete dentry '%s': it still has outstanding references */
					name = child->d->platform_safe_name ? child->d->platform_safe_name : "(null)";
					ltfsmsg(LTFS_WARN, 11998W, name);
				} else {
					_fs_dispose_dentry_contents(child->d, false, gc);
				}
			}

			/* Free up hash table structure */
			if (child) {
				free(child->name);
				free(child);
			}
		}
	}

	if (dentry->tag_count > 0) {
		for (i=0; i<dentry->tag_count; ++i)
			free(dentry->preserved_tags[i]);
		free(dentry->preserved_tags);
	}
	if (dentry->iosched_priv) {
		free(dentry->iosched_priv);
		dentry->iosched_priv = NULL;
	}
	if (! TAILQ_EMPTY(&dentry->extentlist)) {
		TAILQ_FOREACH_SAFE(ext_entry, &dentry->extentlist, list, ext_aux)
			free(ext_entry);
	}
	if (! TAILQ_EMPTY(&dentry->xattrlist)) {
		TAILQ_FOREACH_SAFE(xattr_entry, &dentry->xattrlist, list, xattr_aux) {
			free(xattr_entry->key.name);
			if (xattr_entry->value)
				free(xattr_entry->value);
			free(xattr_entry);
		}
	}
	if (dentry->parent) {
		namelist = fs_find_key_from_hash_table(dentry->parent->child_list, dentry->platform_safe_name, &rc);
		if (rc != 0) {
            ltfsmsg(LTFS_ERR, 11320E, "_fs_dispose_dentry_contents", rc);
		}
		if (namelist) {
			HASH_DEL(dentry->parent->child_list, namelist);
			free(namelist->name);
			free(namelist);
		}
		dentry->parent = NULL;
	}
	if (dentry->name.name) {
		free(dentry->name.name);
		dentry->name.name = NULL;
	}
	if (dentry->platform_safe_name) {
		free(dentry->platform_safe_name);
		dentry->platform_safe_name = NULL;
	}
	if (unlock)
		releasewrite_mrsw(&dentry->meta_lock);
	destroy_mrsw(&dentry->contents_lock);
	destroy_mrsw(&dentry->meta_lock);
	ltfs_mutex_destroy(&dentry->iosched_lock);
	HASH_CLEAR(hh, dentry->child_list);
	if (dentry->target.name) {
		free(dentry->target.name);
		dentry->target.name = NULL;
	}
	free(dentry);
}

void fs_release_dentry(struct dentry *d)
{
	if (! d) {
		ltfsmsg(LTFS_WARN, 10006W, "dentry", __FUNCTION__);
		return;
	}

	acquirewrite_mrsw(&d->meta_lock);
	fs_release_dentry_unlocked(d);
}

void fs_release_dentry_unlocked(struct dentry *d)
{
	--d->numhandles;
	if (d->numhandles != 0 || d->out_of_sync) {
		releasewrite_mrsw(&d->meta_lock);
		return;
	}

	_fs_dispose_dentry_contents(d, true, false);
}

void fs_gc_dentry(struct dentry *d)
{
	acquirewrite_mrsw(&d->meta_lock);
	if (d->numhandles == 0 && ! d->out_of_sync)
		_fs_dispose_dentry_contents(d, true, true);
	else {
		releasewrite_mrsw(&d->meta_lock);
		if (HASH_COUNT(d->child_list) != 0) {
			struct name_list *child, *aux;
			HASH_ITER(hh, d->child_list, child, aux) {
				fs_gc_dentry(child->d);
			}
		}
	}
}

/**
 * Update platform safe name for dentries in the specified
 * directory.
 * @param basedir a pointer to the base dir
 * @param handle_invalid_char Replace invalid chars in the name
 * if TRUE. otherwise the name is skipped withour updating
 * platform_safe_name field.
 */
static struct name_list* fs_update_platform_safe_names_and_hash_table(struct dentry* basedir, struct ltfs_index *idx, struct name_list *list, bool handle_dup_name, bool handle_invalid_char)
{
	struct name_list *same_name, *list_ptr, *list_tmp;
	int rc;

	HASH_ITER(hh, list, list_ptr, list_tmp) {

		if (!handle_dup_name) {
			same_name = fs_find_key_from_hash_table(basedir->child_list, list_ptr->name, &rc);
			if (rc != 0) {
				ltfsmsg(LTFS_ERR, 11320E, "fs_update_platform_safe_names_and_hash_table", rc);
			}
			if (same_name) {
				/* same name file exists. skip the operation. */
				continue;
			}
		}

		update_platform_safe_name(list_ptr->d, handle_invalid_char, idx);

		if (!list_ptr->d->platform_safe_name) {
			/* invalid char is included. skip the operation. */
			continue;
		}

		/* Add hash table whose key is upper case of platform safe name */
		basedir->child_list = fs_add_key_to_hash_table(basedir->child_list, list_ptr->d, &rc);
		if (rc != 0) {
			ltfsmsg(LTFS_ERR, 11319E, "fs_update_platform_safe_names_and_hash_table", rc);
		} else {
			/* delete the entry from the temporary list */
			idx->valid_blocks += list_ptr->d->used_blocks;
			HASH_DEL(list, list_ptr);
			free(list_ptr);
		}
	}

	return list;
}

int fs_update_platform_safe_names(struct dentry* basedir, struct ltfs_index *idx, struct name_list *list)
{
	struct name_list *list_ptr, *list_tmp;
	int ret = 0;

	list = fs_update_platform_safe_names_and_hash_table(basedir, idx, list, false, false);	// normal loop
	list = fs_update_platform_safe_names_and_hash_table(basedir, idx, list, true, false);	// add dup name
	list = fs_update_platform_safe_names_and_hash_table(basedir, idx, list, true, true);	// add invalid char

	/* clear list table */
	if (HASH_COUNT(list)!=0) {	// this situation should not occur. Just for fail-safe.
		HASH_ITER(hh, list, list_ptr, list_tmp) {
			HASH_DEL(list, list_ptr);
			free(list_ptr);
		}
		ret = -LTFS_SAFENAME_FAIL;
	}

	HASH_CLEAR(hh, list);

	return ret;
}

/**
 * Test if a dentry d1 is a predecessor of a dentry d2.
 * @param d1 Dentry one wants to check if is predecessor of d2
 * @param d2 Reference dentry one wants to verify to be a predecessor of d1
 * @return true if d1 is predecessor of d2, false otherwise.
 */
bool fs_is_predecessor(struct dentry *d1, struct dentry *d2)
{
	struct dentry *d = d2;
	if (d1 && d2) {
		while (d) {
			if (d == d1)
				return true;
			d = d->parent;
		}
	}
	return false;
}

/**
 * Calculate number of used blocks in the dentry
 * @param d dentry to calucate number of used blocks
 * @return number of used blocks
 */
uint64_t fs_get_used_blocks(struct dentry *d)
{
	uint64_t used = 0;
	struct extent_info *extent;

	TAILQ_FOREACH(extent, &d->extentlist, list) {
		used += ((extent->byteoffset + extent->bytecount) / d->vol->label->blocksize);
		if ((extent->byteoffset + extent->bytecount) % d->vol->label->blocksize)
			used++;
	}

	return used;
}

/**
 * Dump a single dentry. Doesn't recurse.
 * @param ptr dentry to dump
 * @param spaces how many spaces to print before the dentry name
 */
void _fs_dump_dentry(struct dentry *ptr, int spaces)
{
	int i, n = 0;
	struct xattr_info *xattr;
	struct extent_info *extent;

	for (i=0; i<spaces; ++i)
		printf(" ");

	/* Dentry data */
	printf("%s%s [%d] {size=%llu, realsize=%llu, readonly=%d, creation=%lld, change=%lld, modify=%lld, access=%lld%s}\n",
			ptr->name.name, ptr->isdir?"/":"", ptr->numhandles,
			(unsigned long long)ptr->size, (unsigned long long)ptr->realsize,
			ptr->readonly, (long long int) ptr->creation_time.tv_sec, (long long int) ptr->change_time.tv_sec,
			(long long int) ptr->modify_time.tv_sec, (long long int) ptr->access_time.tv_sec,
			ptr->deleted ? " (deleted)" : "");
	/* Extent data */
	TAILQ_FOREACH(extent, &ptr->extentlist, list) {
		int tab = spaces + strlen(ptr->name.name) + (ptr->isdir ? 1 : 0);
		for (i=0; i<tab+5; ++i)
			printf(" ");
		printf("{extent %d: partition=%d, startblock=%"PRIu64", blockoffset=%u, length=%"PRIu64", fileoffset=%"PRIu64"}\n",
				n++, extent->start.partition, extent->start.block,
				extent->byteoffset, extent->bytecount, extent->fileoffset);
	}
	/* Extended attributes data */
	TAILQ_FOREACH(xattr, &ptr->xattrlist, list) {
		int tab = spaces + strlen(ptr->name.name) + (ptr->isdir ? 1 : 0);
		for (i=0; i<tab+5; ++i)
			printf(" ");
		printf("{xattr key=%s, value=%.*s, size=%zu}\n",
			   xattr->key.name, (int)xattr->size, xattr->value, xattr->size);
	}
}

/**
 * fs_dump_tree implementation
 * @param root recursion starting point
 * @param spaces how many spaces to print before file and directory names
 */
void _fs_dump_tree(struct dentry *root, int spaces)
{
	struct dentry *ptr;
	struct name_list *namelist, *tmp;

	HASH_ITER(hh, root->child_list, namelist, tmp) {
		ptr = namelist->d;
		_fs_dump_dentry(ptr, spaces);
		if (ptr->isdir)
			_fs_dump_tree(ptr, spaces+3);
	}
}

/**
 * Dump the filesystem tree starting at @root.
 * @param root starting point.
 */
void fs_dump_tree(struct dentry *root)
{
	int i;
	struct xattr_info *xattr;

	if (! root->isdir) {
		_fs_dump_dentry(root, 0);
		return;
	}

	/* Dentry data */
	printf("%s [%d] {size=%"PRIu64", readonly=%d, creation=%lld, change=%lld, modify=%lld, access=%lld}\n",
			root->name.name, root->numhandles, root->size, root->readonly,
			(long long int) root->creation_time.tv_sec, (long long int) root->change_time.tv_sec,
			(long long int) root->modify_time.tv_sec, (long long int) root->access_time.tv_sec);

	/* Extended attributes data */
	TAILQ_FOREACH(xattr, &root->xattrlist, list) {
		int tab = strlen(root->name.name) + (root->isdir ? 1 : 0);
		for (i=0; i<tab+5; ++i)
			printf(" ");
		printf("{xattr key=%s, value=%.*s, size=%zu}\n",
			   xattr->key.name, (int)xattr->size, xattr->value, xattr->size);
	}

	return _fs_dump_tree(root, 3);
}
