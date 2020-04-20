#include "bitmap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "inodeptr.h"
#include "bitmap_cache.h"
#include "blockptr.h"
#include "error.h"
#include "log.h"
#include "types.h"

// alloc entry in given bitmap
static stzfs_error_t bitmap_alloc(bitmap_cache_t* cache, int64_t* ptr) {
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
static stzfs_error_t bitmap_free(bitmap_cache_t* cache, int64_t ptr) {
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

// true, if the given ptr is allocated in the given bitmap
static bool bitmap_is_allocated(const bitmap_cache_t* cache, int64_t ptr) {
    if (ptr < 0 || ptr >= cache->length * 8) {
        LOG("bitmap index out of bounds");
        return false;
    }

    const size_t entry_index = ptr / (sizeof(bitmap_entry_t) * 8);
    const size_t inner_index = ptr % (sizeof(bitmap_entry_t) * 8);
    const bitmap_entry_t entry = ((bitmap_entry_t*)cache->bitmap)[entry_index];

    return (entry & ((bitmap_entry_t)1 << inner_index)) != 0;
}

// true, if the given blockptr is allocated in the block bitmap
bool bitmap_is_block_allocated(int64_t blockptr) {
    return blockptr_is_valid(blockptr) && bitmap_is_allocated(&block_bitmap_cache, blockptr);
}

// true, if the given inodeptr is allocated in the inode bitmap
bool bitmap_is_inode_allocated(int64_t inodeptr) {
    return inodeptr_is_valid(inodeptr) && bitmap_is_allocated(&inode_bitmap_cache, inodeptr);
}

// alloc new block in block bitmap
stzfs_error_t bitmap_alloc_block(int64_t* blockptr) {
    if (bitmap_is_block_allocated(*blockptr)) {
        LOG("blockptr is already allocated");
        return ERROR;
    }

    return bitmap_alloc(blockptr, &block_bitmap_cache);
}

// alloc new inode in inode bitmap
stzfs_error_t bitmap_alloc_inode(int64_t* inodeptr) {
    if (bitmap_is_inode_allocated(*inodeptr)) {
        LOG("inodeptr is already allocated");
        return ERROR;
    }

    return bitmap_alloc(inodeptr, &inode_bitmap_cache);
}

// free block in block bitmap
stzfs_error_t bitmap_free_block(int64_t blockptr) {
    if (!bitmap_is_block_allocated(blockptr)) {
        LOG("blockptr is not allocated");
        return ERROR;
    }

    return bitmap_free(blockptr, &block_bitmap_cache);
}

// free inode in inode bitmap
stzfs_error_t bitmap_free_inode(int64_t inodeptr) {
    if (!bitmap_is_inode_allocated(inodeptr)) {
        LOG("inodeptr is not allocated");
        return ERROR;
    }

    return bitmap_free(inodeptr, &inode_bitmap_cache);
}
