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
** FILE NAME:       tape_drivers/open_factor.c
**
** DESCRIPTION:     Open facor value for load balancing tape devices
**
** AUTHOR:          Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/

#ifdef DEBUG
#include <stdio.h>
#define ltfsmsg(a, b, c)
#endif

#include <errno.h>
#include <stdlib.h>

#include "libltfs/ltfslogging.h"
#include "libltfs/ltfs_locking.h"
#include "libltfs/uthash.h"
#include "tape_drivers/open_factor.h"

struct openfactor_channel {
	int channel;       /* ID of the channel */
	int count;         /* Open count through this channel */
	UT_hash_handle hh; /* Hash handle */
};

struct openfactor_host {
	int host;                           /* ID of the host (HBA) */
	int count;                          /* Open count through this host */
	struct openfactor_channel *channel; /* Link to the corresponded channel table */
	UT_hash_handle hh;                  /* Hash handle */
};

static struct openfactor_host *openfactor_table = NULL; /* The open factor table */
static ltfs_mutex_t table_lock;

void init_openfactor(void)
{
	ltfs_mutex_init(&table_lock);
}

void destroy_openfactor(void)
{
	struct openfactor_host    *he = NULL, *tmph; /* Pointer to host entry    */
	struct openfactor_channel *ce = NULL, *tmpc; /* Pointer to channel entry */

	HASH_ITER(hh, openfactor_table, he, tmph) {
		HASH_DEL(openfactor_table, he);

		HASH_ITER(hh, he->channel, ce, tmpc) {
			HASH_DEL(he->channel, ce);
			free(ce);
		}

		free(he);
	}

	ltfs_mutex_destroy(&table_lock);
}

void increment_openfactor(int host, int channel)
{
	struct openfactor_host    *he = NULL; /* Pointer to host entry    */
	struct openfactor_channel *ce = NULL; /* Pointer to channel entry */

	ltfs_mutex_lock(&table_lock);

	HASH_FIND_INT(openfactor_table, &host, he);
	if (he) {
		HASH_FIND_INT(he->channel, &channel, ce);
		if (!ce) {
			ce = calloc(1, sizeof(struct openfactor_channel));
			if (!ce) {
				ltfs_mutex_unlock(&table_lock);
				/* memory allocation error, print error and return */
				ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
				return;
			}

			ce->channel = channel;
			ce->count   = 1;
			HASH_ADD_INT(he->channel, channel, ce);

			he->count++;
		} else {
			ce->count++;
			he->count++;
		}
	} else {
		he = calloc(1, sizeof(struct openfactor_host));
		if (!he) {
			ltfs_mutex_unlock(&table_lock);
			/* memory allocation error, print error and return */
			ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
			return;
		}

		ce = calloc(1, sizeof(struct openfactor_channel));
		if (!ce) {
			ltfs_mutex_unlock(&table_lock);
			/* memory allocation error, print error and return */
			free(he);
			ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
			return;
		}

		he->host    = host;
		he->count   = 1;

		ce->channel = channel;
		ce->count   = 1;
		HASH_ADD_INT(he->channel, channel, ce);
		HASH_ADD_INT(openfactor_table, host, he);
	}

	ltfs_mutex_unlock(&table_lock);
}

void decrement_openfactor(int host, int channel)
{
	struct openfactor_host    *he = NULL; /* Pointer to host entry    */
	struct openfactor_channel *ce = NULL; /* Pointer to channel entry */

	ltfs_mutex_lock(&table_lock);

	HASH_FIND_INT(openfactor_table, &host, he);
	if (he) {
		HASH_FIND_INT(he->channel, &channel, ce);
		if (ce) {
			if (he->count) he->count--;
			if (ce->count) ce->count--;
		}
	}

	ltfs_mutex_unlock(&table_lock);
}

int get_openfactor(int host, int channel)
{
	struct openfactor_host    *he = NULL; /* Pointer to host entry    */
	struct openfactor_channel *ce = NULL; /* Pointer to channel entry */
	int ret = 0;

	ltfs_mutex_lock(&table_lock);

	HASH_FIND_INT(openfactor_table, &host, he);
	if (he) {
		HASH_FIND_INT(he->channel, &channel, ce);
		if (ce)
			ret =  ((he->count << 16) | ce->count);
	}

	ltfs_mutex_unlock(&table_lock);

	return ret;
}

#ifdef DEBUG
int main(int argc, char **argv)
{
	init_openfactor();

	increment_openfactor(0, 0);

	increment_openfactor(0, 1);
	increment_openfactor(0, 1);

	increment_openfactor(0, 2);
	increment_openfactor(0, 2);
	increment_openfactor(0, 2);

	increment_openfactor(1, 2);
	increment_openfactor(1, 2);

	increment_openfactor(1, 1);
	increment_openfactor(1, 1);
	increment_openfactor(1, 1);
	increment_openfactor(1, 1);

	increment_openfactor(1, 0);
	increment_openfactor(1, 0);
	increment_openfactor(1, 0);
	increment_openfactor(1, 0);
	increment_openfactor(1, 0);
	increment_openfactor(1, 0);


	for (int i = 0; i < 2; i++) {
		for (int j = 0; j < 4; j++) {
			printf("(%d, %d) = %x\n", i, j, get_openfactor(i, j));
		}
	}

	destroy_openfactor();
}
#endif
