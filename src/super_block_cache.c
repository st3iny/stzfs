#include <errno.h>
#include <stdio.h>
#include <sys/mman.h>

#include "super_block_cache.h"
#include "types.h"
#include "vm.h"

super_block* super_block_cache;

int super_block_cache_init(void) {
    super_block_cache = mmap(NULL, BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, vm_get_fd(),
                             SUPER_BLOCKPTR * BLOCK_SIZE);
    if (super_block_cache == MAP_FAILED) {
        printf("super_block_cache_init: could not create super block cache\n");
        return -errno;
    }

    return 0;
}

int super_block_cache_dispose(void) {
    if (munmap(super_block_cache, BLOCK_SIZE)) {
        printf("super_block_cache_dispose: could not dispose super block cache\n");
        return -errno;
    }

    return 0;
}

int super_block_cache_sync(void) {
    if (msync(super_block_cache, BLOCK_SIZE, MS_SYNC)) {
        printf("super_block_cache_sync: could not sync super block to disk\n");
        return -errno;
    }

    return 0;
}
