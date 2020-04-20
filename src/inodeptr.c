#include "inodeptr.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bitmap_cache.h"
#include "helpers.h"
#include "log.h"
#include "super_block_cache.h"
#include "types.h"

// true, if given inodeptr is a valid (readable and writable) inode pointer
bool inodeptr_is_valid(int64_t inodeptr) {
    return inodeptr > 0 &&
           inodeptr <= INODEPTR_MAX(&super_block_cache) &&
           inodeptr != INODEPTR_ERROR;
}

// true, if the given inodeptr can not be freed
bool inodeptr_is_protected(int64_t inodeptr) {
    return !inodeptr_is_valid(inodeptr) || inodeptr == ROOT_INODEPTR;
}

// true, if the inodeptr is currently allocated in the inode bitmap
// TODO: create merged method for both inodeptrs and blockptrs
bool inodeptr_is_allocated(int64_t inodeptr) {
    if (!inodeptr_is_valid(inodeptr)) {
        LOG("invalid inodeptr given");
        return false;
    }

    size_t entry_index = inodeptr / (sizeof(bitmap_entry_t) * 8);
    size_t inner_index = inodeptr % (sizeof(bitmap_entry_t) * 8);
    bitmap_entry_t entry = ((bitmap_entry_t*)inode_bitmap_cache.bitmap)[entry_index];

    return (entry & ((bitmap_entry_t)1 << inner_index)) != 0;
}
