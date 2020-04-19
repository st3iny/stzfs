#include "block.h"

#include <stdint.h>
#include <string.h>

#include "alloc.h"
#include "bitmap_cache.h"
#include "disk.h"
#include "error.h"
#include "free.h"
#include "log.h"
#include "super_block_cache.h"
#include "types.h"

// read block from disk
stzfs_error_t block_read(int64_t blockptr, void* block) {
    LOG("read block %i", blockptr);
    if (blockptr == SUPER_BLOCKPTR) {
        LOG("trying to read protected super block");
        return ERROR;
    } else if (blockptr < 0 || (blockptr > BLOCKPTR_MAX && blockptr != NULL_BLOCKPTR)) {
        LOG("blockptr out of bounds");
        return ERROR;
    }

    if (blockptr == NULL_BLOCKPTR) {
        memset(block, 0, STZFS_BLOCK_SIZE);
    } else {
        if (disk_read((off_t)blockptr * STZFS_BLOCK_SIZE, block, STZFS_BLOCK_SIZE)) {
            LOG("could not read block from disk");
            return ERROR;
        }
    }

    return SUCCESS;
}

// read multiple blocks from disk
stzfs_error_t block_readall(const int64_t* blockptr_arr, void* blocks, size_t length) {
    for (size_t i = 0; i < length; i++) {
        blocks += STZFS_BLOCK_SIZE;
        if (block_read(blockptr_arr[i], blocks)) {
            LOG("could not read block");
            return ERROR;
        }
    }

    return SUCCESS;
}

// write block to disk
stzfs_error_t block_write(int64_t blockptr, const void* block) {
    if (blockptr == SUPER_BLOCKPTR) {
        LOG("trying to write protected super block");
        return ERROR;
    } else if (blockptr == NULL_BLOCKPTR) {
        LOG("trying to write null block");
        return ERROR;
    } else if (blockptr < 0 || blockptr > BLOCKPTR_MAX) {
        LOG("blockptr out of bounds");
        return ERROR;
    }

    disk_write((off_t)blockptr * STZFS_BLOCK_SIZE, block, STZFS_BLOCK_SIZE);
    return SUCCESS;
}

// allocate new blockptr only
stzfs_error_t block_allocptr(int64_t* blockptr) {
    super_block* sb = super_block_cache;
    stzfs_error_t error = SUCCESS;

    if (sb->free_blocks == 0) {
        LOG("no free block available");
        error = ERROR;
    }

    if (alloc_bitmap(blockptr, &block_bitmap_cache)) {
        LOG("could not allocate blockptr");
        error = ERROR;
    }

    if (error) {
        *blockptr = BLOCKPTR_ERROR;
    } else {
        // update superblock
        sb->free_blocks--;
        super_block_cache_sync();
    }

    return error;
}

// allocate and write new block in place
stzfs_error_t block_alloc(int64_t* blockptr, const void* block) {
    if (block_allocptr(blockptr)) {
        LOG("could not allocate new block");
        return ERROR;
    }

    block_write(*blockptr, block);
    return SUCCESS;
}

// free blocks in bitmap
stzfs_error_t block_free(const int64_t* blockptr_arr, size_t length) {
    super_block* sb = super_block_cache;
    stzfs_error_t error = SUCCESS;

    for (size_t offset = 0; offset < length; offset++) {
        sb->free_blocks--;
        if (free_bitmap(&block_bitmap_cache, blockptr_arr[offset])) {
            LOG("could not free block in block bitmap");
            error = ERROR;
            break;
        }
    }

    // update superblock
    super_block_cache_sync();

    return error;
}
