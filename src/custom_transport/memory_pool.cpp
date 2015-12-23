#include <cassert>
#include <cstdlib>
#include <cstring>
#include "memory_pool.hpp"

void init_pool(memory_pool *pool)
{
	pool->big_list = NULL;
	pool->small_list = NULL;
}

void destroy_pool(memory_pool *pool)
{
	assert(pool != NULL);
	small_chunk *previous_small = pool->small_list;
	while (pool->small_list != NULL)
	{
		previous_small = pool->small_list;
		pool->small_list = pool->small_list->next;
		free(previous_small);
	}

	big_chunk *previous_big = pool->big_list;
	while (pool->big_list != NULL)
	{
		previous_big = pool->big_list;
		pool->big_list = pool->big_list->next;
		free(previous_big);
	}
}

bool destroy_chunk(memory_pool *pool, void *ptr)
{
	big_chunk *previous_big = NULL;
	big_chunk *current_big = pool->big_list;
	while (current_big != NULL)
	{
		if (current_big == ptr)
		{
			if (previous_big != NULL)
				previous_big->next = current_big->next;
			free(current_big);
			return true;
		}

		previous_big = current_big;
		current_big = current_big->next;
	}
	return false;
}

void *allocate(memory_pool *pool, size_t request_size)
{
	if (request_size >= sizeof(pool->small_list->bytes) / 4)
	{
		// allocate large request and put on big_list
		size_t chunk_size = offsetof( big_chunk, bytes) + request_size;
		big_chunk *new_chunk =  ( big_chunk *) malloc(chunk_size);
		assert(new_chunk != NULL);

		new_chunk->next = pool->big_list;
		pool->big_list = new_chunk;
		return new_chunk->bytes;
	}
	else
		if (pool->small_list == NULL ||
				(sizeof(pool->small_list->bytes) < pool->small_list->offset + request_size))
		{
			// allocate small request and put on small_list
			size_t chunk_size = sizeof(small_chunk);
			small_chunk *new_chunk = ( small_chunk *) malloc(chunk_size);
			assert(new_chunk != NULL);
			new_chunk->next = pool->small_list;
			new_chunk->offset = 0;
			pool->small_list = new_chunk;
		}
	// return address from small_list
	void *result = pool->small_list->bytes + pool->small_list->offset;
	pool->small_list->offset += request_size;
	return result;
}

void *callocate(memory_pool *pool, size_t request_size)
{
    void *ptr = allocate(pool, request_size);
    memset(ptr, 0, request_size);
    return ptr;
}


void *reallocate(memory_pool *pool, size_t request_size, size_t old_request_size, void *ptr)
{
	void *new_ptr = allocate(pool, request_size);
	memcpy(new_ptr, ptr, old_request_size);

	if (old_request_size >= sizeof(pool->small_list->bytes) / 4)
	{
		destroy_chunk(pool, ptr);
	}
	return new_ptr;
}



