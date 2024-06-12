/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2024 IBM Corp. All rights reserved.
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
** FILE NAME:       inc_journal.c
**
** DESCRIPTION:     Journal handling for incremental index
**
** AUTHORS:         Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
** ORIGINAL LOGIC:  David Pease
**                  pease@coati.com
**
*************************************************************************************
*/

#include "ltfs.h"
#include "fs.h"
#include "inc_journal.h"

static int _allocate_jentry(struct jentry **e, char *path, struct dentry* d)
{
	struct jentry *ent = NULL;

	*e = NULL;

	ent = calloc(1, sizeof(struct jentry));
	if (!ent) {
		ltfsmsg(LTFS_ERR, 10001E, "allocating a jentry");
		return -LTFS_NO_MEMORY;
	}

	ent->id.full_path = path;
	ent->id.uid       = d->uid;

	*e = ent;

	return 0;
}

static inline int _dispose_jentry(struct jentry *ent)
{
	if (ent) {
		if (ent->id.full_path)
			free(ent->id.full_path);
		free(ent);
	}

	return 0;
}

/**
 *  Handle created object in the tree.
 *
 *  Caller need to grab vol->index->dirty_lock outside of this function.
 *
 *   @param ppath parent path name of the object
 *   @param d dentry created
 *   @param vol pointer to the LTFS volume
 */
int incj_create(char *ppath, struct dentry *d, struct ltfs_volume *vol)
{
	int ret = -1, len = -1;
	char *full_path = NULL;
	struct jentry *ent = NULL;
	struct jcreated_entry *jdir = NULL, *jd = NULL;

	/* Skip journal modification because of an error */
	if (vol->journal_err) {
		return 0;
	}

	/* Skip if an ancestor is already created in this session */
	TAILQ_FOREACH(jd, &vol->created_dirs, list) {
		char* cp = jd->path;
		if (strstr(ppath, cp) == ppath) {
			return 0;
		}
	}

	/* Create full path of created object and jentry */
	len = asprintf(&full_path, "%s/%s", ppath, d->name.name);
	if (len < 0) {
		ltfsmsg(LTFS_ERR, 10001E, "full path of a jentry");
		vol->journal_err = true;
		return -LTFS_NO_MEMORY;
	}

	ret = _allocate_jentry(&ent, full_path, d);
	if (ret < 0) {
		vol->journal_err = true;
		free(full_path);
		return ret;
	}

	ent->reason = CREATE;
	ent->dentry = d;

	HASH_ADD(hh, vol->journal, id, sizeof(struct jentry), ent);

	if (d->isdir) {
		jdir = calloc(1, sizeof(struct jcreated_entry));
		if (!jdir) {
			ltfsmsg(LTFS_ERR, 10001E, "allocating a jcreated_entry");
			return -LTFS_NO_MEMORY;
		}

		/* NOTE: Use same pointer of ent because it's life is same */
		jdir->path = ent->id.full_path;

		TAILQ_INSERT_TAIL(&vol->created_dirs, jdir, list);
	}

	return 0;
}

/**
 *  Handle modified file in the tree.
 *
 *  Caller need to grab vol->index->dirty_lock outside of this function.
 *
 *   @param path path name of the object
 *   @param d dentry to be modified (for recording uid)
 *   @param vol pointer to the LTFS volume
 */
int incj_modify(char *path, struct dentry *d, struct ltfs_volume *vol)
{
	int ret = -1;
	struct jentry *ent = NULL;
	struct jcreated_entry *jd = NULL;

	/* Skip journal modification because of an error */
	if (vol->journal_err) {
		return 0;
	}

	/* Skip journal modification because it is already existed */
	HASH_FIND(hh, vol->journal, &ent->id, sizeof(struct jentry), ent);
	if (ent) {
		return 0;
	}

	/* Skip if an ancestor is already created in this session */
	TAILQ_FOREACH(jd, &vol->created_dirs, list) {
		char *cp = jd->path;
		if (strstr(path, cp) == path) {
			return 0;
		}
	}

	ret = _allocate_jentry(&ent, path, d);
	if (ret < 0) {
		vol->journal_err = true;
		return ret;
	}

	ent->reason = MODIFY;
	ent->dentry = d;

	HASH_ADD(hh, vol->journal, id, sizeof(struct jentry), ent);

	return 0;
}

/**
 *  Handle deleted file in the tree.
 *
 *  Caller need to grab vol->index->dirty_lock outside of this function.
 *
 *   @param path path name of the object
 *   @param d dentry to be removed (for recording uid)
 *   @param vol pointer to the LTFS volume
 */
int incj_rmfile(char *path, struct dentry *d, struct ltfs_volume *vol)
{
	int ret = -1;
	char *full_path = NULL;
	struct journal_id id;
	struct jentry *ent = NULL;
	struct jcreated_entry *jd = NULL;

	/* Skip journal modification because of an error */
	if (vol->journal_err) {
		return 0;
	}

	id.full_path = path;
	id.uid       = d->uid;
	HASH_FIND(hh, vol->journal, &id, sizeof(struct jentry), ent);
	if (ent) {
		if (ent->reason == CREATE) {
			/*
			 * Remove the entry because this file is newly created and deleted
			 * in one incremental index session
			 */
			HASH_DEL(vol->journal, ent);
			return 0;
		} else if (ent->reason == MODIFY) {
			/*
			 * Override the existing entry to DELETE_FILE record.
			 */
			ent->reason = DELETE_FILE;
			ent->dentry = NULL;
			return 0;
		}
	}

	/* Skip if an ancestor is already created in this session */
	TAILQ_FOREACH(jd, &vol->created_dirs, list) {
		char *cp = jd->path;
		if (strstr(path, cp) == path) {
			return 0;
		}
	}

	/* Create full path of deleted object and jentry */
	full_path = strdup(path);
	if (!full_path) {
		ltfsmsg(LTFS_ERR, 10001E, "duplicating a path for deleted file");
		vol->journal_err = true;
		return -LTFS_NO_MEMORY;
	}

	ret = _allocate_jentry(&ent, full_path, d);
	if (ret < 0) {
		vol->journal_err = true;
		return ret;
	}

	ent->reason = DELETE_FILE;
	ent->name.percent_encode = d->name.percent_encode;
	ent->name.name = strdup(d->name.name);
	if (!ent->name.name) {
		ltfsmsg(LTFS_ERR, 10001E, "duplicating a name of deleted file");
		vol->journal_err = true;
		return -LTFS_NO_MEMORY;
	}

	HASH_ADD(hh, vol->journal, id, sizeof(struct jentry), ent);

	return 0;
}

/**
 *  Handle deleted directory in the tree.
 *
 *  Caller need to grab vol->index->dirty_lock outside of this function.
 *
 *   @param path path name of the object
 *   @param d dentry to be removed (for recording uid)
 *   @param vol pointer to the LTFS volume
 */
int incj_rmdir(char *path, struct dentry *d, struct ltfs_volume *vol)
{
	int ret = -1;
	char *full_path = NULL;
	struct jentry *ent = NULL, *je = NULL, *tmp = NULL;
	struct jcreated_entry *jd = NULL, *dtmp = NULL;

	/* Skip journal modification because of an error */
	if (vol->journal_err) {
		return 0;
	}

	/*
	 * 1. Remove entry from created_dirs if created directory is removed in a same session
	 * 2. Skip if an ancestor is already created in this session
	 */
	TAILQ_FOREACH_SAFE(jd, &vol->created_dirs, list, dtmp) {
		char *cp = jd->path;
		if (strstr(path, cp) == path) {
			if (!strcmp(path, cp)) {
				TAILQ_REMOVE(&vol->created_dirs, jd, list);
				/*
				 * NOTE:
				 * Do not free jd->path because it shall be freed into _dispose_jentry.
				 * jentry::id.full_path and jd->path points the same address
				 */
			} else {
				return 0;
			}
		}
	}

	/* Need to find existing children under this directory */
	HASH_ITER(hh, vol->journal, je, tmp) {
		if (strstr(je->id.full_path, path) == je->id.full_path) {
			HASH_DEL(vol->journal, je);
			_dispose_jentry(je);
		}
	}

	/* Create full path of created object and jentry */
	full_path = strdup(path);
	if (!full_path) {
		ltfsmsg(LTFS_ERR, 10001E, "duplicating a path of deleted directory");
		vol->journal_err = true;
		return -LTFS_NO_MEMORY;
	}

	ret = _allocate_jentry(&ent, full_path, d);
	if (ret < 0) {
		vol->journal_err = true;
		return ret;
	}

	ent->reason = DELETE_DIRECTORY;
	ent->name.percent_encode = d->name.percent_encode;
	ent->name.name = strdup(d->name.name);
	if (!ent->name.name) {
		ltfsmsg(LTFS_ERR, 10001E, "duplicating a name of deleted directory");
		vol->journal_err = true;
		return -LTFS_NO_MEMORY;
	}

	HASH_ADD(hh, vol->journal, id, sizeof(struct jentry), ent);

	return 0;
}

int incj_dispose_jentry(struct jentry *ent)
{
	return (_dispose_jentry(ent));
}

/**
 * Clear all entries into the incremental journal
 */
int incj_clear(struct ltfs_volume *vol)
{
	struct jentry *je = NULL, *tmp = NULL;
	struct jcreated_entry *jd = NULL, *dtmp = NULL;

	TAILQ_FOREACH_SAFE(jd, &vol->created_dirs, list, dtmp) {
		TAILQ_REMOVE(&vol->created_dirs, jd, list);
	}

	HASH_ITER(hh, vol->journal, je, tmp) {
		HASH_DEL(vol->journal, je);
		_dispose_jentry(je);
	}

	return 0;
}

/**
 * Sort function for dump incremental journal
 */
static int _by_path(const struct jentry *a, const struct jentry *b)
{
	int ret = 0;

	ret = strcmp(a->id.full_path, b->id.full_path);
	if (!ret) {
		if (a->id.uid > b->id.uid)
			ret = 1;
		else
			ret = -1;
	}

	return ret;
}

static inline int dig_path(char *p, struct ltfs_index *idx)
{
	int ret = 0;
	char *path;

	path = strdup(p);
	if (! path) {
		ltfsmsg(LTFS_ERR, 10001E, "dig_path: path");
		return -LTFS_NO_MEMORY;
	}

	ret = fs_path_clean(path, idx);

	free(path);

	return ret;
}

void incj_sort(struct ltfs_volume *vol)
{
	HASH_SORT(vol->journal, _by_path);
}

/**
 *  This is a function for debug. Print contents of the journal and the created
 *  directory list to stdout.
 */
void incj_dump(struct ltfs_volume *vol)
{
	char *prev_parent = NULL, *parent, *filename;
	struct jcreated_entry *jd  = NULL, *dtmp = NULL;
	struct jentry         *ent = NULL, *tmp = NULL;
	char *reason[] = { "CREATE", "MODIFY", "DELFILE", "DELDIR" };

	printf("===============================================================================\n");
	TAILQ_FOREACH_SAFE(jd, &vol->created_dirs, list, dtmp) {
		printf("CREATED_DIR: %s\n", jd->path);
		TAILQ_REMOVE(&vol->created_dirs, jd, list);
	}

	printf("--------------------------------------------------------------------------------\n");
	incj_sort(vol);
	HASH_ITER(hh, vol->journal, ent, tmp) {
		printf("JOURNAL: %s, %llu, %s, ", ent->id.full_path, (unsigned long long)ent->id.uid, reason[ent->reason]);
		if (!ent->dentry)
			printf("no-dentry\n");
		else {
			if (ent->dentry->isdir) {
				printf("dir\n");
				if (ent->reason == CREATE)
					fs_dir_clean(ent->dentry);
			} else
				printf("file\n");

			parent = strdup(ent->id.full_path);
			fs_split_path(parent, &filename, strlen(parent) + 1);

			if (prev_parent) {
				if (strcmp(prev_parent, parent)) {
					dig_path(parent, vol->index);
				}
				free(prev_parent);
			} else {
				dig_path(parent, vol->index);
			}
			prev_parent = parent;
			ent->dentry->dirty = false;
		}

		HASH_DEL(vol->journal, ent);
		_dispose_jentry(ent);
	}

	if (prev_parent) free(prev_parent);

	return;
}

int incj_create_path_helper(const char *dpath, struct incj_path_helper **pm, struct ltfs_volume *vol)
{
	struct incj_path_helper *ipm;
	char *wp = NULL, *tmp = NULL, *dname = NULL;
	int ret = 0;

	*pm = NULL;

	ipm = calloc(1, sizeof(struct incj_path_helper));
	if (!ipm) {
		ltfsmsg(LTFS_ERR, 10001E, "allocating a path helper");
		return -LTFS_NO_MEMORY;
	}

	if (dpath[0] != '/') {
		/* Provided path must be a absolute path */
		ltfsmsg(LTFS_ERR, 17302E, dpath);
		free(ipm);
		return -LTFS_INVALID_PATH;
	}

	ipm->vol = vol;

	if (strcmp(dpath, "/") == 0) {
		/* Provided path is the root, return good */
		*pm = ipm;
		return 0;
	}

	wp = strdup(dpath);
	if (!wp) {
		ltfsmsg(LTFS_ERR, 10001E, "duplicating a directory path for path helper");
		free(ipm);
		return -LTFS_NO_MEMORY;
	}

	for (dname = strtok_r(wp, "/", &tmp); dname != NULL; dname = strtok_r(NULL, "/", &tmp)) {
		ret = incj_push_directory(dname, ipm);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 17305E);
			free(wp);
			incj_destroy_path_helper(ipm);
			return ret;
		}
	}

	free(wp);
	*pm = ipm;

	return 0;
}

int incj_destroy_path_helper(struct incj_path_helper *pm)
{
	struct incj_path_element *cur, *next;

	cur = pm->head;

	while (cur) {
		next = cur->next;
		if (cur->d)
			fs_release_dentry(cur->d);
		if (cur->name)
			free(cur->name);
		free(cur);
		cur = next;
	}

	free(pm);
	return 0;
}

int incj_push_directory(char *name, struct incj_path_helper *pm)
{
	int ret = 0;
	struct incj_path_element *ipelm = NULL, *cur_tail = NULL;
	struct dentry *parent = NULL;

	ipelm = calloc(1, sizeof(struct incj_path_element));
	if (!ipelm) {
		ltfsmsg(LTFS_ERR, 10001E, "allocating a path element on push");
		return -LTFS_NO_MEMORY;
	}

	/* Set name field of new path element */
	ipelm->name = strdup(name);
	if (!ipelm->name) {
		ltfsmsg(LTFS_ERR, 10001E, "duplicating a path of pushing directory");
		incj_destroy_path_helper(pm);
		return -LTFS_NO_MEMORY;
	}

	/* Set dentry field of new path element */
	if (pm->elems)
		parent = pm->tail->d;
	else
		parent = pm->vol->index->root;

	ret = fs_directory_lookup(parent, name, &ipelm->d);
	if (ret) {
		ltfsmsg(LTFS_ERR, 17306E, ret);
		free(ipelm->name);
		free(ipelm);
		incj_destroy_path_helper(pm);
		return -LTFS_INVALID_PATH;
	}

	/* Modify path chain and # of elements */
	if (!pm->elems) {
		pm->head = ipelm;
		pm->tail = ipelm;
	} else {
		cur_tail       = pm->tail;
		cur_tail->next = ipelm;
		ipelm->prev    = cur_tail;
		pm->tail       = ipelm;
	}

	pm->elems++;

	return 0;
}

int incj_pop_directory(struct incj_path_helper *pm)
{
	struct incj_path_element *cur_tail = NULL, *new_tail = NULL;

	if (!pm->elems) {
		/* Must have one or more elements */
		return -LTFS_UNEXPECTED_VALUE;
	}

	cur_tail = pm->tail;
	new_tail = cur_tail->prev;

	new_tail->next = NULL;
	pm->tail = new_tail;

	pm->elems--;
	if (!pm->elems) {
		pm->head = NULL;
	}

	if (cur_tail->d)
		fs_release_dentry(cur_tail->d);
	if (cur_tail->name)
		free(cur_tail->name);
	free(cur_tail);

	return 0;
}

int incj_compare_path(struct incj_path_helper *now, struct incj_path_helper *next,
					  int *matches, int *pops, bool *perfect_match)
{
	int ret = 0, matched = 0;
	struct incj_path_element *cur1 = NULL, *cur2 = NULL;

	*matches       = 0;
	*pops          = 0;
	*perfect_match = false;

	cur1 = now->head;
	cur2 = next->head;

	if (!cur1 && !cur2) {
		/* Both are root */
		*perfect_match = true;
		return 0;
	}

	while (cur1 && cur2) {
		if (cur1->d != cur2->d)
			break;
		matched++;
		cur1 = cur1->next;
		cur2 = cur2->next;
	}

	*matches = matched;

	if (!cur2) {
		/* now is equal or longer than next. -> zero for perfect match, other wise how many pops */
		*pops = now->elems - *matches;
		if (!cur1)
			*perfect_match = true;
	}

	return ret;
}

char* incj_get_path(struct incj_path_helper *pm)
{
	char *path = NULL, *path_old = NULL;
	struct incj_path_element *cur = NULL;
	int ret = 0;

	cur = pm->head;

	if (!cur) {
		/* Root directory */
		ret = asprintf(&path, "/");
		if (ret < 0) {
			/* memory allocation error */
			return NULL;
		}
		return path;
	}

	while (cur) {
		if (path) path_old = path;
		ret = asprintf(&path, "%s/%s", path_old, cur->name);
		if (ret < 0) {
			/* memory allocation error */
			free(path_old);
			return NULL;
		}
		free(path_old);

		cur++;
	}

	if (path) path_old = path;
	ret = asprintf(&path, "/%s", path_old);
	if (ret < 0) {
		/* memory allocation error */
		free(path_old);
		return NULL;
	}
	free(path_old);

	return path;
}
