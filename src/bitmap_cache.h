#ifndef FILESYSTEM_BITMAP_CACHE_H
#define FILESYSTEM_BITMAP_CACHE_H

#include <stddef.h>

typedef struct bitmap_cache_t {
    void* bitmap;
    size_t length;
    size_t next;
} bitmap_cache_t;

extern bitmap_cache_t inode_bitmap_cache;
extern bitmap_cache_t block_bitmap_cache;

int bitmap_cache_init(void);
int bitmap_cache_dispose(void);

#endif // FILESYSTEM_BITMAP_CACHE_H
