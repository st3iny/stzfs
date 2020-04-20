#include "bitmap.h"

#include <stddef.h>
#include <stdint.h>

#include "bitmap_cache.h"
#include "error.h"
#include "log.h"
#include "types.h"

// alloc entry in given bitmap
// TODO: improve me -> split to separate methods for blocks and inodes
// FIXME: change param order
stzfs_error_t bitmap_alloc(int64_t* ptr, bitmap_cache_t* cache) {
    if (ptr < 0 || *ptr >= cache->length * 8) {
        LOG("bitmap index out of bounds");
        return ERROR;
    }

    bitmap_entry_t* bitmap = (bitmap_entry_t*)cache->bitmap;
    const size_t bitmap_length = cache->length / sizeof(bitmap_entry_t);

    int64_t next_free = -1;
    for (size_t i = cache->next; i < bitmap_length && next_free == -1; i++) {
        bitmap_entry_t* entry = &bitmap[i];
        if (~*entry == 0) {
            // skip if there is no free entry available
            continue;
        }

        cache->next = i;

        // find bit index of free entry
        bitmap_entry_t data = *entry;
        int offset = 0;
        while (offset < sizeof(bitmap_entry_t) * 8 && (data & 1) != 0) {
            data >>= 1;
            offset++;
        }

        // mark alloc in bitmap
        bitmap_entry_t new_entry = 1;
        *entry |= new_entry << offset;
        next_free = i * sizeof(bitmap_entry_t) * 8 + offset;
    }

    if (next_free == -1) {
        LOG("could not allocate entry in bitmap");
        return ERROR;
    }

    *ptr = next_free;
    return SUCCESS;
}

// free entry in bitmap
// TODO: improve me -> split to separate methods for blocks and inodes
stzfs_error_t bitmap_free(bitmap_cache_t* cache, int64_t ptr) {
    if (ptr < 0 || ptr >= cache->length * 8) {
        LOG("bitmap index out of bounds");
        return ERROR;
    }

    const size_t entry_offset = ptr / (sizeof(bitmap_entry_t) * 8);
    const size_t inner_offset = ptr % (sizeof(bitmap_entry_t) * 8);

    bitmap_entry_t* entry = &((bitmap_entry_t*)cache->bitmap)[entry_offset];
    *entry ^= (bitmap_entry_t)1 << inner_offset;

    if (entry_offset < cache->next) {
        cache->next = entry_offset;
    }

    return SUCCESS;
}
