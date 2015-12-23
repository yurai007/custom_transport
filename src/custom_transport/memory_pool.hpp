#ifndef MEMORY_POOL_HPP
#define MEMORY_POOL_HPP

#include <cstddef>

/*
 * In C++ there is need for casting from malloc against compilation errors (-fpermissive
   In C we shouldn't cast.

 * char bytes[1] instead char *bytes. Now after malloc big_chunk 'bytes' field has address.
   But *char had 0. Why?

 * malloc internals in glibc/malloc
 * hooks.c/free_check is implementation of free.
 * inside mem2chunk_check there is interesting snippet:

   p = mem2chunk (mem);
   sz = chunksize (p);

   According source size of chunk for pointer mem is taken from
   chunk_tmp = (char*)mem - 2*8
   chunk =
   (chunk_tmp->size & ~(SIZE_BITS)) =
   align_to_8(*((char*)chunk_tmp + 8))

 * for long, char and int malloced there is 33B chunk allocated.
   For 128B -> 145B allocated and for 300B --> 321B allocated

 * so trick won't work because there is no history for allocated data
   but only for whole chunks. Finally conclusion is that I can't get information about
   memory size allocated by malloc and destroyed by free to my use for reallocate.
   I have to store extra info in my chunks (small_chunk/big_chunk).
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

