#ifndef MEMORY_POOL_HPP
#define MEMORY_POOL_HPP

#include <cstddef>

/*
 * In C++ there is need for casting from malloc against compilation errors (-fpermissive
   In C we shouldn't cast.

 * char bytes[1] instead char *bytes. Now after malloc big_chunk 'bytes' field has address.
   But *char had 0. Why?
 */

constexpr static int page_size = 4096;

struct small_chunk
{
	small_chunk *next;
	size_t offset;
	char bytes[page_size - sizeof(void*) * 2];
};

struct big_chunk
{
	big_chunk *next;
	char bytes[1];
};

struct memory_pool
{
	small_chunk *small_list;
	big_chunk *big_list;
};

extern void init_pool(memory_pool *pool);
extern void destroy_pool(memory_pool *pool);
extern bool destroy_chunk(memory_pool *pool, void *ptr);
extern void *allocate(memory_pool *pool, size_t request_size);
extern void *callocate(memory_pool *pool, size_t request_size);
extern void *reallocate(memory_pool *pool, size_t request_size,
						size_t old_request_size, void *ptr);

#endif // MEMORY_POOL_HPP

