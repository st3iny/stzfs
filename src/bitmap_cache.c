#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "bitmap_cache.h"
#include "blocks.h"
#include "helpers.h"
#include "super_block_cache.h"
#include "types.h"
#include "disk.h"

static int create_cache(bitmap_cache_t* cache, int64_t blockptr, int64_t length);
static int dispose_cache(bitmap_cache_t* cache);

bitmap_cache_t block_bitmap_cache;
bitmap_cache_t inode_bitmap_cache;

int bitmap_cache_init(void) {
    const super_block* sb = super_block_cache;

    TRY(create_cache(&block_bitmap_cache, sb->block_bitmap, sb->block_bitmap_length),
        printf("bitmap_cache_init: could not create block bitmap cache\n"));
    TRY(create_cache(&inode_bitmap_cache, sb->inode_bitmap, sb->inode_bitmap_length),
        printf("bitmap_cache_init: could not create inode bitmap cache\n"));

    return 0;
}

int bitmap_cache_dispose(void) {
    TRY(dispose_cache(&block_bitmap_cache),
        printf("bitmap_cache_dispose: could not dispose block bitmap cache\n"));
    TRY(dispose_cache(&inode_bitmap_cache),
        printf("bitmap_cache_dispose: could not dispose inode bitmap cache\n"));

    return 0;
}

static int create_cache(bitmap_cache_t* cache, int64_t blockptr, int64_t length) {
    cache->length = (size_t)length * STZFS_BLOCK_SIZE;
    cache->bitmap = mmap(NULL, cache->length, PROT_READ | PROT_WRITE, MAP_SHARED, disk_get_fd(),
                         (off_t)blockptr * STZFS_BLOCK_SIZE);
    cache->next = 0;

    if (cache->bitmap == MAP_FAILED) {
        return -errno;
    }

    return 0;
}

static int dispose_cache(bitmap_cache_t* cache) {
    if (munmap(cache->bitmap, cache->length)) {
        return -errno;
    }

    return 0;
}
