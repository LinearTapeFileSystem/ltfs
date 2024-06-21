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
** FILE NAME:       inc_journal.h
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

#include "queue.h"

#ifndef __inc_journal_h__
#define __inc_journal_h__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Enumeration of reasons for an entry
 */
enum journal_reason {
	CREATE = 0,        /**< Newly created, need to be 0 for debug  */
	MODIFY,            /**< Modified */
	DELETE_FILE,       /**< File is deleted */
	DELETE_DIRECTORY,  /**< Directory is deleted */
};

/**
 * Identifier of journal entry for handling multiple changes in one session
 */
struct journal_id {
	char     *full_path; /**< Full path name of the target */
	uint64_t uid;        /**< i-node number of the target */
};

/**
 * Journal entry
 */
struct jentry {
	struct journal_id     id;           /**< ID of the journal entry (key of the hash table) */
	enum   journal_reason reason;       /**< Reason of the entry */
	struct dentry         *dentry;      /**< Target dentry if required */
	struct ltfs_name      name;         /**< Name of entry for delete */
	UT_hash_handle        hh;
};

/**
 * Created directory list
 */
struct jcreated_entry {
	TAILQ_ENTRY(jcreated_entry) list; /**< Pointers for linked list of requests */
	char *path;
};

/**
 *
 */
struct incj_path_element {
	struct incj_path_element *prev;
	struct incj_path_element *next;
	char* name;
	struct dentry *d;
};

struct incj_path_helper {
	struct incj_path_element *head;
	struct incj_path_element *tail;
	struct ltfs_volume *vol;
	unsigned int elems;
};

int incj_create(char *ppath, struct dentry *d, struct ltfs_volume *vol);
int incj_modify(char *path, struct dentry *d, struct ltfs_volume *vol);
int incj_rmfile(char *path, struct dentry *d, struct ltfs_volume *vol);
int incj_rmdir(char *path, struct dentry *d, struct ltfs_volume *vol);
int incj_dispose_jentry(struct jentry *ent);
int incj_clear(struct ltfs_volume *vol);
void incj_sort(struct ltfs_volume *vol);
void incj_dump(struct ltfs_volume *vol);

int incj_create_path_helper(const char *path, struct incj_path_helper **pm, struct ltfs_volume *vol);
int incj_destroy_path_helper(struct incj_path_helper *pm);
int incj_push_directory(char *name, struct incj_path_helper *pm);
int incj_pop_directory(struct incj_path_helper *pm);
int incj_compare_path(struct incj_path_helper *p1, struct incj_path_helper *p2,
					  int *matches, int *pops, bool *perfect_match);
char* incj_get_path(struct incj_path_helper *pm);

#ifdef __cplusplus
}
#endif

#endif /* __inc_journal_h__ */
