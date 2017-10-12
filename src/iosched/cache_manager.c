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
** FILE NAME:       cache_manager.c
**
** DESCRIPTION:     Implements a generic cache manager.
**
** AUTHOR:          Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
*************************************************************************************
*/
#include "libltfs/ltfs.h"
#include "cache_manager.h"

/**
 * cache_pool structure.
 * Holds objects of size @object_size.
 *
 * This data structure must be initialized by cache_manager_init() and freed by
 * cache_manager_destroy(). Individual objects can be allocated and freed using
 * the functions cache_manager_allocate_object() and cache_manager_free_object(),
 * respectivelly.
 *
 * The internal counters such as @current_capacity indicate how many objects
 * have been allocated through this cache pool; it doesn't necessarily say how
 * many available objects are in the pool.
 */
struct cache_pool {
	size_t object_size;       /**< The size of each object in this pool */
	size_t initial_capacity;  /**< Low water mark. Defines the initial capacity of the pool */
	size_t max_capacity;      /**< High water mark. Defines the maximum capacity of the pool */
	size_t current_capacity;  /**< How many objects are currently allocated */
	TAILQ_HEAD(objects_struct, cache_object) pool; /**< Pool of cached objects */
};

/**
 * cache_object structure.
 * Holds objects of size @object_size, as stored in the cache_pool structure.
 */
struct cache_object {
	uint32_t refcount;              /**< Reference count */
	ltfs_mutex_t lock;           /**< Lock to protect access to the refcount */
	void *data;                     /**< Cached data. Must be the first element in the structure */
	struct cache_pool *pool;        /**< Backpointer to the cache pool this object is part of */
	TAILQ_ENTRY(cache_object) list; /**< Pointers to next and previous cache objects */
};

/**
 * Private helper.
 * Creates a new object and adds it to the cache pool on success, returning NULL if out of memory.
 * @param pool cache pool to create the object in.
 * @return a pointer to the cache_object or NULL on failure.
 */
struct cache_object *_cache_manager_create_object(struct cache_pool *pool)
{
	int ret;
	struct cache_object *object = calloc(1, sizeof(struct cache_object));
	if (! object) {
		ltfsmsg(LTFS_ERR, "10001E", "cache manager: object");
		return NULL;
	}
	object->data = calloc(1, pool->object_size + LTFS_CRC_SIZE); /* Allocate extra 4-bytes for SCSI logical block protection */
	if (! object->data) {
		ltfsmsg(LTFS_ERR, "10001E", "cache manager: object data");
		free(object);
		return NULL;
	}
	object->pool = pool;
	object->refcount = 1;
	ret = ltfs_mutex_init(&object->lock);
	if (ret) {
		ltfsmsg(LTFS_ERR, "10002E", ret);
		free(object->data);
		free(object);
		return NULL;
	}
	TAILQ_INSERT_TAIL(&pool->pool, object, list);
	return object;
}

/**
 * Create a new cache pool.
 * @param object_size size of the objects to store in the pool.
 * @param initial_capacity how many objects to allocate in advance when creating the pool.
 * @param max_capacity how many objects to keep at most in the pool.
 * @return an opaque pointer to the cache pool on success or NULL if out of memory.
 */
void *cache_manager_init(size_t object_size, size_t initial_capacity, size_t max_capacity)
{
	struct cache_pool *pool;
	size_t i;

	pool = (struct cache_pool *) calloc(1, sizeof (struct cache_pool));
	if (! pool) {
		ltfsmsg(LTFS_ERR, "10001E", "cache manager: pool");
		return NULL;
	}
	pool->object_size = object_size;
	pool->initial_capacity = initial_capacity;
	pool->max_capacity = max_capacity;
	pool->current_capacity = initial_capacity;
	TAILQ_INIT(&pool->pool);

	for (i=0; i<initial_capacity; ++i) {
		struct cache_object *object = _cache_manager_create_object(pool);
		if (! object) {
			ltfsmsg(LTFS_ERR, "11114E");
			cache_manager_destroy(pool);
			return NULL;
		}
	}

	return (void *) pool;
}

/**
 * Destroy a cache pool.
 * @param cache cache pool to destroy, as returned from cache_manager_init().
 */
void cache_manager_destroy(void *cache)
{
	struct cache_object *object, *aux;
	struct cache_pool *pool = (struct cache_pool *) cache;
	if (! pool) {
		ltfsmsg(LTFS_WARN, "10006W", "pool", __FUNCTION__);
		return;
	}

	TAILQ_FOREACH_SAFE(object, &pool->pool, list, aux) {
		TAILQ_REMOVE(&pool->pool, object, list);
		ltfs_mutex_destroy(&object->lock);
		if (object->data)
			free(object->data);
		free(object);
	}

	free(pool);
}

/**
 * Tells if a given pool is full or if it contains room for allocating more elements.
 * @param cache cache to check.
 * @retval true if pool has room for allocating more elements, false if not.
 */
bool cache_manager_has_room(void *cache)
{
	struct cache_pool *pool = (struct cache_pool *) cache;
	CHECK_ARG_NULL(pool, false);

	return ! (TAILQ_EMPTY(&pool->pool) && pool->current_capacity == pool->max_capacity);
}

/**
 * Allocate a new object in the cache pool.
 * The object data and the object size can be obtained through the functions
 * cache_manager_get_object_data() and cache_manager_get_object_size(),
 * respectively.
 *
 * @param cache cache pool to create the object in.
 * @retval a pointer to the new allocated object or NULL if out of memory
 *  or if the high water mark for this pool has been reached. The caller is
 *  responsible for dealing with the cache pressure in this case.
 */
void *cache_manager_allocate_object(void *cache)
{
	size_t i, new_size = 0;
	struct cache_object *object, *last_object = NULL;
	struct cache_pool *pool = (struct cache_pool *) cache;
	CHECK_ARG_NULL(pool, NULL);

	TAILQ_FOREACH(object, &pool->pool, list) {
		/* Return the first available object */
		TAILQ_REMOVE(&pool->pool, object, list);
		object->refcount = 1;
		return (void *) object;
	}

	/*
	 * If no available objects were found then we need to figure if we are allowed
	 * to grow the pool any further. If we're not then NULL is returned and the caller
	 * is in charge of flushing caches to overcome this situation.
	 */
	if (pool->current_capacity == pool->max_capacity)
		return NULL;

	else if ((pool->current_capacity * 2) < pool->max_capacity)
		if (pool->current_capacity)
			new_size = pool->current_capacity * 2;
		else
			new_size = pool->max_capacity / 2;
	else
		/* Expand until the maximum capacity */
		new_size = pool->max_capacity;

	for (i=pool->current_capacity; i<new_size; ++i) {
		struct cache_object *object = _cache_manager_create_object(pool);
		if (! object) {
			/* Luckily we might have increased the size of the cache by a few entries.. */
			ltfsmsg(LTFS_WARN, "11115W");
			break;
		}
		last_object = object;
		pool->current_capacity++;
	}

	if (! last_object) {
		/* If we couldn't grow the cache any further return failure */
		ltfsmsg(LTFS_ERR, "11116E");
		return NULL;
	}

	/* Remove @last_object from the pool and return it */
	TAILQ_REMOVE(&pool->pool, last_object, list);
	return last_object;
}

/**
 * Increment a reference count of a given cache object.
 * @param cache_object object to increment the reference count from
 * @return the same cache_object on success or NULL on invalid input.
 */
void *cache_manager_get_object(void *cache_object)
{
	struct cache_object *object = (struct cache_object *) cache_object;

	CHECK_ARG_NULL(cache_object, NULL);

	ltfs_mutex_lock(&object->lock);
	object->refcount++;
	ltfs_mutex_unlock(&object->lock);

	return cache_object;
}

/**
 * Dispose an object.
 * @param cache_object object to dispose, as returned from cache_manager_allocate_object()
 */
void cache_manager_free_object(void *cache_object, size_t count)
{
	struct cache_pool *pool;
	struct cache_object *object = (struct cache_object *) cache_object;
	if (! object) {
		ltfsmsg(LTFS_WARN, "10006W", "object", __FUNCTION__);
		return;
	}

	ltfs_mutex_lock(&object->lock);
	object->refcount--;
	if (object->refcount > 0) {
		ltfs_mutex_unlock(&object->lock);
		return;
	}
	ltfs_mutex_unlock(&object->lock);

	pool = object->pool;

	if (pool->current_capacity > pool->initial_capacity) {
		/* Shrink the cache */
		ltfs_mutex_destroy(&object->lock);
		free(object->data);
		free(object);
		pool->current_capacity--;
	} else {
		/* Add the object back to the pool of ready to be used */
		pool = object->pool;
		if (count)
			memset(object->data, 0, count);
		else
			memset(object->data, 0, pool->object_size);
		TAILQ_INSERT_TAIL(&pool->pool, object, list);
	}
}

/**
 * Get a pointer to an object's data.
 * @param cache_object cache object, as returned from cache_manager_allocate_object()
 * @retval a pointer to the object's data or NULL on invalid input.
 */
void *cache_manager_get_object_data(void *cache_object)
{
	struct cache_object *object = (struct cache_object *) cache_object;
	CHECK_ARG_NULL(object, NULL);
	return object->data;
}

/**
 * Get the size of an object's data.
 * @param cache_object cache object, as returned from cache_manager_allocate_object()
 * @retval the size of the object's data.
 */
size_t cache_manager_get_object_size(void *cache_object)
{
	struct cache_object *object = (struct cache_object *) cache_object;
	CHECK_ARG_NULL(object, 0);
	return object->pool->object_size;
}
