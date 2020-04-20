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
