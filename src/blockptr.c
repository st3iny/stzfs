#include "blockptr.h"

#include <stdbool.h>
#include <stdint.h>

#include "types.h"

// check if the given blockptr is in bounds and writable
bool blockptr_is_valid(int64_t blockptr) {
    return blockptr != SUPER_BLOCKPTR &&
           blockptr != NULL_BLOCKPTR &&
           blockptr != BLOCKPTR_ERROR &&
           (blockptr >= 0 && blockptr <= BLOCKPTR_MAX);
}
