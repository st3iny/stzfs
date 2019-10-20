#include "types.h"

typedef struct bitmap_cache_t {
    void* bitmap;
    size_t length;
    size_t next;
} bitmap_cache_t;

// bitmap_cache_t* bitmap_cache_create(blockptr_t blockptr, blockptr_t length);
void bitmap_cache_create(bitmap_cache_t* cache, blockptr_t blockptr, blockptr_t length);
void bitmap_cache_dispose(bitmap_cache_t* cache);
// blockptr_t bitmap_cache_alloc(bitmap_cache_t* cache);
// blockptr_t bitmap_cache_free(bitmap_cache_t* cache, blockptr_t offset);
