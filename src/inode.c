#include "inode.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bitmap.h"
#include "block.h"
#include "blockptr.h"
#include "error.h"
#include "find.h"
#include "inodeptr.h"
#include "log.h"
#include "super_block_cache.h"

// allocate new inodeptr only
stzfs_error_t inode_allocptr(int64_t* inodeptr) {
    super_block* sb = super_block_cache;
    stzfs_error_t error = SUCCESS;

    if (sb->free_inodes == 0) {
        LOG("no free inode available");
        *inodeptr = INODEPTR_ERROR;
        return ERROR;
    }

    bitmap_alloc_inode(inodeptr);

    // update superblock
    sb->free_inodes--;
    super_block_cache_sync();

    return SUCCESS;
}

// allocate and write a new inode in place
stzfs_error_t inode_alloc(int64_t* inodeptr, const inode_t* inode) {
    const super_block* sb = super_block_cache;

    if (sb->free_inodes == 0) {
        LOG("no free inode available");
        *inodeptr = INODEPTR_ERROR;
        return ERROR;
    }

    // get next free inode
    inode_allocptr(inodeptr);

    // get inode table block
    const int64_t inode_table_offset = *inodeptr / INODE_BLOCK_ENTRIES;
    const int64_t inode_table_blockptr = sb->inode_table + inode_table_offset;
    inode_block inode_table_block;
    if (block_read(inode_table_blockptr, &inode_table_block)) {
        LOG("could not read inode table block");
        return ERROR;
    }

    // place inode into table and write table block
    inode_table_block.inodes[*inodeptr % INODE_BLOCK_ENTRIES] = *inode;
    if (block_write(inode_table_blockptr, &inode_table_block)) {
        LOG("could not write inode table block");
        return ERROR;
    }

    return SUCCESS;
}

// append blockptr to inode block list
stzfs_error_t inode_append_data_blockptr(inode_t* inode, int64_t blockptr) {
    if (!blockptr_is_valid(blockptr) && blockptr != NULL_BLOCKPTR) {
        LOG("invalid blockptr given");
        return ERROR;
    }

    if (inode->block_count >= INODE_MAX_BLOCKS) {
        LOG("inode is at full capacity")
        return ERROR;
    }

    // level struct for indirection convenience
    typedef struct level {
        blockptr_t* blockptr;
        indirect_block block;
        bool changed;
    } level;

    int64_t offset = inode->block_count;
    if (offset < INODE_DIRECT_BLOCKS) {
        inode->data_direct[offset] = blockptr;
    } else if ((offset -= INODE_DIRECT_BLOCKS) < INODE_SINGLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = &inode->data_single_indirect};
        if (offset == 0) {
            int64_t new_blockptr;
            block_alloc(&new_blockptr, &level1.block);
            *level1.blockptr = new_blockptr;
        } else {
            block_read(*level1.blockptr, &level1.block);
        }

        level1.block.blocks[offset] = blockptr;

        block_write(*level1.blockptr, &level1.block);
    } else if ((offset -= INODE_SINGLE_INDIRECT_BLOCKS) < INODE_DOUBLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = &inode->data_double_indirect};
        if (offset == 0) {
            int64_t new_blockptr;
            block_alloc(&new_blockptr, &level1.block);
            *level1.blockptr = new_blockptr;
        } else {
            block_read(*level1.blockptr, &level1.block);
        }

        level level2 = {.blockptr = &level1.block.blocks[offset / INODE_SINGLE_INDIRECT_BLOCKS]};
        if (offset % INODE_SINGLE_INDIRECT_BLOCKS == 0) {
            level1.changed = true;
            int64_t new_blockptr;
            block_alloc(&new_blockptr, &level2.block);
            *level2.blockptr = new_blockptr;
        } else {
            block_read(*level2.blockptr, &level2.block);
        }

        level2.block.blocks[offset % INDIRECT_BLOCK_ENTRIES] = blockptr;

        if (level1.changed) block_write(*level1.blockptr, &level1.block);
        block_write(*level2.blockptr, &level2.block);
    } else if ((offset -= INODE_DOUBLE_INDIRECT_BLOCKS) < INODE_TRIPLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = &inode->data_triple_indirect};
        if (offset == 0) {
            int64_t new_blockptr;
            block_alloc(&new_blockptr, &level1.block);
            *level1.blockptr = new_blockptr;
        } else {
            block_read(*level1.blockptr, &level1.block);
        }

        level level2 = {.blockptr = &level1.block.blocks[offset / INODE_DOUBLE_INDIRECT_BLOCKS]};
        if (offset % INODE_DOUBLE_INDIRECT_BLOCKS == 0) {
            level1.changed = true;
            int64_t new_blockptr;
            block_alloc(&new_blockptr, &level2.block);
            *level2.blockptr = new_blockptr;
        } else {
            block_read(*level2.blockptr, &level2.block);
        }

        level level3 = {.blockptr = &level2.block.blocks[(offset % INODE_DOUBLE_INDIRECT_BLOCKS) / INODE_SINGLE_INDIRECT_BLOCKS]};
        if (offset % INODE_SINGLE_INDIRECT_BLOCKS == 0) {
            level2.changed = true;
            int64_t new_blockptr;
            block_alloc(&new_blockptr, &level3.block);
            *level3.blockptr = new_blockptr;
        } else {
            block_read(*level3.blockptr, &level3.block);
        }

        level3.block.blocks[offset % INDIRECT_BLOCK_ENTRIES] = blockptr;

        if (level1.changed) block_write(*level1.blockptr, &level1.block);
        if (level2.changed) block_write(*level2.blockptr, &level2.block);
        block_write(*level3.blockptr, &level3.block);
    }

    inode->block_count++;
    return SUCCESS;
}

// append a new data block to an inode
stzfs_error_t inode_alloc_data_block(inode_t* inode, const void* block) {
    if (inode->block_count >= INODE_MAX_BLOCKS) {
        LOG("inode has reached max block count");
        return ERROR;
    }

    if (super_block_cache->free_blocks <= 0) {
        LOG("no free block available");
        return ERROR;
    }

    int64_t blockptr;
    block_alloc(&blockptr, block);
    inode_append_data_blockptr(inode, blockptr);
    return SUCCESS;
}

// append null blocks to inode until inode block count matches given block count
stzfs_error_t inode_append_null_blocks(inode_t* inode, int64_t block_count) {
    if (block_count > BLOCKPTR_MAX) {
        LOG("new block count out of bounds");
        return ERROR;
    } else if (block_count < inode->block_count) {
        LOG("new block count smaller than current inode block count");
        return ERROR;
    }

    for (int64_t i = inode->block_count; i < block_count; i++) {
        inode_append_data_blockptr(inode, NULL_BLOCKPTR);
    }
    return SUCCESS;
}

// free inode and allocated data blocks
stzfs_error_t inode_free(int64_t inodeptr, inode_t* inode) {
    if (inodeptr_is_protected(inodeptr)) {
        LOG("trying to free protected inode");
        return ERROR;
    } else if (M_IS_DIR(inode->mode) && inode->atom_count > 2) {
        LOG("directory is not empty");
        return ERROR;
    } else if (M_IS_DIR(inode->mode) && inode->link_count > 1) {
        LOG("directory inode link count too high");
        return ERROR;
    } else if (!M_IS_DIR(inode->mode) && inode->link_count > 0) {
        LOG("file inode link count too high");
        return ERROR;
    }

    // dealloc inode first
    bitmap_free_inode(inodeptr);

    // free allocated data blocks in bitmap
    inode_truncate(inode, 0);

    return SUCCESS;
}

// free inode data blocks until its block count reaches the given offset
// FIXME: implement proper algorithm (merge with free_last_inode_data_block)
stzfs_error_t inode_truncate(inode_t* inode, int64_t offset) {
    if (offset < 0) {
        LOG("negative offsets are illegal");
        return ERROR;
    } else if (offset > inode->block_count) {
        LOG("new offset is greater than current inode block count");
        return ERROR;
    }

    for (int64_t block_count = inode->block_count; block_count > offset; block_count--) {
        inode_free_last_data_block(inode);
    }

    return SUCCESS;
}

// free the last data block of an inode
stzfs_error_t inode_free_last_data_block(inode_t* inode) {
    if (inode->block_count <= 0) {
        LOG("inode has no data blocks left");
        return ERROR;
    }

    // level struct for convenience
    typedef struct level {
        int64_t blockptr;
        indirect_block block;
    } level;

    inode->block_count--;
    int64_t offset = inode->block_count;
    int64_t absolute_blockptr = 0;
    if (offset < INODE_DIRECT_BLOCKS) {
        absolute_blockptr = inode->data_direct[offset];

        // FIXME: just for debugging
        inode->data_direct[offset] = 0;
    } else if ((offset -= INODE_DIRECT_BLOCKS) < INODE_SINGLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = inode->data_single_indirect};
        block_read(level1.blockptr, &level1.block);
        absolute_blockptr = level1.block.blocks[offset];

        if (offset == 0) {
            block_free(&level1.blockptr, 1);

            // FIXME: just for debugging
            inode->data_single_indirect = 0;
        }
    } else if ((offset -= INODE_SINGLE_INDIRECT_BLOCKS) < INODE_DOUBLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = inode->data_double_indirect};
        block_read(level1.blockptr, &level1.block);

        level level2 = {.blockptr = level1.block.blocks[offset / INODE_SINGLE_INDIRECT_BLOCKS]};
        block_read(level2.blockptr, &level2.block);
        absolute_blockptr = level2.block.blocks[offset % INODE_SINGLE_INDIRECT_BLOCKS];

        if (offset == 0) {
            block_free(&level1.blockptr, 1);

            // FIXME: just for debugging
            inode->data_double_indirect = 0;
        }
        if (offset % INODE_SINGLE_INDIRECT_BLOCKS == 0) block_free(&level2.blockptr, 1);
    } else if ((offset -= INODE_DOUBLE_INDIRECT_BLOCKS) < INODE_TRIPLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = inode->data_triple_indirect};
        block_read(level1.blockptr, &level1.block);

        level level2 = {.blockptr = level1.block.blocks[offset / INODE_DOUBLE_INDIRECT_BLOCKS]};
        block_read(level2.blockptr, &level2.block);

        level level3 = {.blockptr = level2.block.blocks[offset % INODE_DOUBLE_INDIRECT_BLOCKS]};
        block_read(level3.blockptr, &level3.block);
        absolute_blockptr = level3.block.blocks[offset % INODE_SINGLE_INDIRECT_BLOCKS];

        if (offset == 0) {
            block_free(&level1.blockptr, 1);

            // FIXME: just for debugging
            inode->data_triple_indirect = 0;
        }
        if (offset % INODE_DOUBLE_INDIRECT_BLOCKS == 0) block_free(&level2.blockptr, 1);
        if (offset % INODE_SINGLE_INDIRECT_BLOCKS == 0) block_free(&level3.blockptr, 1);
    } else {
        // this block should not be reached ever
        LOG("relative data offset out of bounds");
        return ERROR;
    }

    if (absolute_blockptr) block_free(&absolute_blockptr, 1);
    return SUCCESS;
}

// read inode from disk
stzfs_error_t inode_read(int64_t inodeptr, inode_t* inode) {
    if (!inodeptr_is_valid(inodeptr)) {
        LOG("invalid inodeptr given");
        return ERROR;
    } else if (!bitmap_is_inode_allocated(inodeptr)) {
        LOG("inodeptr is not allocated");
        return ERROR;
    }

    const super_block* sb = super_block_cache;

    int64_t inode_table_block_offset = inodeptr / (STZFS_BLOCK_SIZE / sizeof(inode_t));
    if (inode_table_block_offset >= sb->inode_table_length) {
        LOG("out of bounds while trying to read inode");
        return ERROR;
    }

    // get inode table block
    int64_t inode_table_blockptr = sb->inode_table + inode_table_block_offset;
    inode_block inode_table_block;
    block_read(inode_table_blockptr, &inode_table_block);

    // read inode from inode table block
    *inode = inode_table_block.inodes[inodeptr % (STZFS_BLOCK_SIZE / sizeof(inode_t))];

    return SUCCESS;
}

// read inode data block with relative offset
stzfs_error_t inode_read_data_block(inode_t* inode, int64_t offset, void* block, int64_t* blockptr_out) {
    if (offset < 0 || offset >= inode->block_count) {
        LOG("inode data block offset out of range");
        return ERROR;
    }

    int64_t blockptr = find_inode_data_blockptr(inode, offset, ALLOC_SPARSE_NO);
    block_read(blockptr, block);

    if (blockptr_out != NULL) {
        *blockptr_out = blockptr;
    }
    return SUCCESS;
}

// read data blocks of an inode and store them to a buffer
stzfs_error_t inode_read_data_blocks(inode_t* inode, void* block_arr, size_t length, int64_t offset) {
    if (offset < 0 || (offset + length) > inode->block_count) {
        LOG("inode data block offset out of range");
        return ERROR;
    }

    int64_t blockptr_arr[length];
    find_inode_data_blockptrs(inode, blockptr_arr, length, offset);
    block_readall(blockptr_arr, block_arr, length);
    return SUCCESS;
}

// write inode to disk
stzfs_error_t inode_write(int64_t inodeptr, const inode_t* inode) {
    if (!inodeptr_is_valid(inodeptr)) {
        LOG("illegal inodeptr given");
        return ERROR;
    } else if (!bitmap_is_inode_allocated(inodeptr)) {
        LOG("inodeptr is not allocated");
        return ERROR;
    }

    const super_block* sb = super_block_cache;

    int64_t table_block_offset = inodeptr / INODE_BLOCK_ENTRIES;

    // place inode in table and write back table block
    int64_t table_blockptr = sb->inode_table + table_block_offset;
    inode_block table_block;
    block_read(table_blockptr, &table_block);
    table_block.inodes[inodeptr % INODE_BLOCK_ENTRIES] = *inode;
    block_write(table_blockptr, &table_block);

    return SUCCESS;
}

// read inode data block with relative offset
stzfs_error_t inode_write_data_block(inode_t* inode, int64_t offset, const void* block) {
    if (offset < 0 || offset > inode->block_count) {
        LOG("inode data block offset out of bounds");
        return ERROR;
    }

    int64_t blockptr = find_inode_data_blockptr(inode, offset, ALLOC_SPARSE_YES);
    block_write(blockptr, block);
    return SUCCESS;
}

// write existing or allocate an new inode data block
// FIXME: is this function neccessary?
stzfs_error_t inode_write_or_alloc_data_block(inode_t* inode, int64_t offset, const void* block) {
    if (offset < 0) {
        LOG("inode data block offset out of bounds");
        return ERROR;
    }

    if (offset < inode->block_count) {
        inode_write_data_block(inode, offset, block);
    } else {
        inode_alloc_data_block(inode, block);
    }

    return SUCCESS;
}
