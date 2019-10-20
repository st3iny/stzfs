#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "bitmap_cache.h"
#include "vm.h"

void bitmap_cache_create(bitmap_cache_t* cache, blockptr_t blockptr, blockptr_t length) {
    cache->length = (size_t)length * BLOCK_SIZE;
    cache->bitmap = mmap(NULL, cache->length, PROT_READ | PROT_WRITE, MAP_SHARED, vm_get_fd(),
                        (off_t)blockptr * BLOCK_SIZE);
    cache->next = 0;
}

void bitmap_cache_dispose(bitmap_cache_t* cache) {
    munmap(cache->bitmap, cache->length);
}
