#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "alloc.h"
#include "bitmap_cache.h"
#include "blocks.h"
#include "read.h"
#include "super_block_cache.h"
#include "write.h"

static int alloc_bitmap(objptr_t* index, bitmap_cache_t* cache);
static void alloc_inode_data_blockptr(inode_t* inode, blockptr_t blockptr);

// alloc entry in bitmap
static int alloc_bitmap(objptr_t* index, bitmap_cache_t* cache) {
    objptr_t next_free = 0;
    for (size_t i = cache->next; i < cache->length && !next_free; i += sizeof(bitmap_entry_t)) {
        bitmap_entry_t* entry = (bitmap_entry_t*)(cache->bitmap + i);
        if (~*entry == 0) {
            continue;
        }

        cache->next = i;

        // there is at least one free alloc available
        bitmap_entry_t data = *entry;
        int offset = 0;
        while (offset < sizeof(bitmap_entry_t) * 8 && (data & 1) != 0) {
            data >>= 1;
            offset++;
        }

        // mark alloc in bitmap
        bitmap_entry_t new_entry = 1;
        *entry |= new_entry << offset;
        next_free = i * 8 + offset;
    }

    if (!next_free) {
        printf("alloc_entry: could not allocate entry in bitmap\n");
        return -ENOSPC;
    }

    *index = next_free;
    return 0;
}

// allocate new blockptr only
int alloc_blockptr(blockptr_t* blockptr) {
    int return_code = 0;
    super_block* sb = super_block_cache;

    if (sb->free_blocks == 0) {
        printf("alloc_blockptr: no free block available\n");
        return_code = -ENOSPC;
        goto END;
    }

    const int err = alloc_bitmap((objptr_t*)blockptr, &block_bitmap_cache);
    if (err) {
        printf("alloc_blockptr: could not allocate blockptr\n");
        return_code = err;
        goto END;
    }

    // update superblock
    sb->free_blocks--;
    super_block_cache_sync();

END:
    if (return_code) {
        *blockptr = BLOCKPTR_ERROR;
    }
    return return_code;
}

// allocate and write new block in place
int alloc_block(blockptr_t* blockptr, const void* block) {
    const int err = alloc_blockptr(blockptr);
    if (err) {
        printf("alloc_block: could not allocate block\n");
        return err;
    }

    write_block(*blockptr, block);

    return 0;
}

// allocate new inodeptr only
int alloc_inodeptr(inodeptr_t* inodeptr) {
    int return_code = 0;
    super_block* sb = super_block_cache;

    if (sb->free_inodes == 0) {
        printf("alloc_inodeptr: no free inode available\n");
        return_code = -ENOSPC;
        goto END;
    }

    const int err = alloc_bitmap((objptr_t*)inodeptr, &inode_bitmap_cache);
    if (err) {
        printf("alloc_inodeptr: could not allocate inodeptr\n");
        return_code = err;
        goto END;
    }

    // update superblock
    sb->free_inodes--;
    super_block_cache_sync();

END:
    if (return_code) {
        *inodeptr = INODEPTR_ERROR;
    }
    return return_code;
}

// allocate and write a new inode in place
int alloc_inode(inodeptr_t* inodeptr, const inode_t* inode) {
    const super_block* sb = super_block_cache;

    // get next free inode
    const int err = alloc_inodeptr(inodeptr);
    if (err) {
        printf("alloc_inode: could not allocate inode\n");
        return err;
    }

    // get inode table block
    const blockptr_t inode_table_offset = *inodeptr / INODE_BLOCK_ENTRIES;
    const blockptr_t inode_table_blockptr = sb->inode_table + inode_table_offset;
    inode_block inode_table_block;
    read_block(inode_table_blockptr, &inode_table_block);

    // place inode into table and write table block
    inode_table_block.inodes[*inodeptr % INODE_BLOCK_ENTRIES] = *inode;
    write_block(inode_table_blockptr, &inode_table_block);

    return 0;
}

// append blockptr to inode block list
static void alloc_inode_data_blockptr(inode_t* inode, blockptr_t blockptr) {
    // level struct for indirection convenience
    typedef struct level {
        blockptr_t* blockptr;
        indirect_block block;
        bool changed;
    } level;

    blockptr_t offset = inode->block_count;
    if (offset < INODE_DIRECT_BLOCKS) {
        inode->data_direct[offset] = blockptr;
    } else if ((offset -= INODE_DIRECT_BLOCKS) < INODE_SINGLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = &inode->data_single_indirect};
        if (offset == 0) {
            alloc_block(level1.blockptr, &level1.block);
        } else {
            read_block(*level1.blockptr, &level1.block);
        }

        level1.block.blocks[offset] = blockptr;

        write_block(*level1.blockptr, &level1.block);
    } else if ((offset -= INODE_SINGLE_INDIRECT_BLOCKS) < INODE_DOUBLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = &inode->data_double_indirect};
        if (offset == 0) {
            alloc_block(level1.blockptr, &level1.block);
        } else {
            read_block(*level1.blockptr, &level1.block);
        }

        level level2 = {.blockptr = &level1.block.blocks[offset / INODE_SINGLE_INDIRECT_BLOCKS]};
        if (offset % INODE_SINGLE_INDIRECT_BLOCKS == 0) {
            level1.changed = true;
            alloc_block(level2.blockptr, &level2.block);
        } else {
            read_block(*level2.blockptr, &level2.block);
        }

        level2.block.blocks[offset % INDIRECT_BLOCK_ENTRIES] = blockptr;

        if (level1.changed) write_block(*level1.blockptr, &level1.block);
        write_block(*level2.blockptr, &level2.block);
    } else if ((offset -= INODE_DOUBLE_INDIRECT_BLOCKS) < INODE_TRIPLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = &inode->data_triple_indirect};
        if (offset == 0) {
            alloc_block(level1.blockptr, &level1.block);
        } else {
            read_block(*level1.blockptr, &level1.block);
        }

        level level2 = {.blockptr = &level1.block.blocks[offset / INODE_DOUBLE_INDIRECT_BLOCKS]};
        if (offset % INODE_DOUBLE_INDIRECT_BLOCKS == 0) {
            level1.changed = true;
            alloc_block(level2.blockptr, &level2.block);
        } else {
            read_block(*level2.blockptr, &level2.block);
        }

        level level3 = {.blockptr = &level2.block.blocks[(offset % INODE_DOUBLE_INDIRECT_BLOCKS) / INODE_SINGLE_INDIRECT_BLOCKS]};
        if (offset % INODE_SINGLE_INDIRECT_BLOCKS == 0) {
            level2.changed = true;
            alloc_block(level3.blockptr, &level3.block);
        } else {
            read_block(*level3.blockptr, &level3.block);
        }

        level3.block.blocks[offset % INDIRECT_BLOCK_ENTRIES] = blockptr;

        if (level1.changed) write_block(*level1.blockptr, &level1.block);
        if (level2.changed) write_block(*level2.blockptr, &level2.block);
        write_block(*level3.blockptr, &level3.block);
    }

    inode->block_count++;
}

// alloc a new data block for an inode
blockptr_t alloc_inode_data_block(inode_t* inode, const void* block) {
    if (inode->block_count >= INODE_MAX_BLOCKS) {
        printf("alloc_inode_data_block: inode has reached max block count\n");
        return 0;
    }

    // alloc data block
    blockptr_t blockptr;
    alloc_block(&blockptr, block);

    // append to inode block list
    alloc_inode_data_blockptr(inode, blockptr);

    return blockptr;
}

// alloc new inode null blocks and truncate inode to block_count
int alloc_inode_null_blocks(inode_t* inode, blockptr_t block_count) {
    if (block_count > BLOCKPTR_MAX) {
        printf("alloc_inode_null_blocks: new block count out of bounds\n");
        return -EFBIG;
    } else if (block_count < inode->block_count) {
        printf("alloc_inode_null_blocks: new block count smaller than current inode block count\n");
        return -EINVAL;
    }

    for (blockptr_t i = inode->block_count; i < block_count; i++) {
        alloc_inode_data_blockptr(inode, NULL_BLOCKPTR);
    }

    return 0;
}

// alloc an entry in a directory inode
int alloc_dir_entry(inode_t* inode, const char* name, inodeptr_t target_inodeptr) {
    // set max_link_count to 0xffff - 1 to prevent a deadlock
    if (strlen(name) > MAX_FILENAME_LENGTH) {
        printf("alloc_dir_entry: filename too long\n");
        return -ENAMETOOLONG;
    } else if (inode->link_count >= 0xfffe) {
        printf("alloc_dir_entry: max link count reached\n");
        return -EMLINK;
    } else if ((inode->mode & M_DIR) == 0) {
        printf("alloc_dir_entry: not a directory\n");
        return -ENOTDIR;
    }

    dir_block block;
    dir_block_entry* entry;

    // get next free dir block
    blockptr_t block_offset = inode->atom_count / DIR_BLOCK_ENTRIES;
    int next_free_entry = inode->atom_count % DIR_BLOCK_ENTRIES;

    if (next_free_entry == 0) {
        // allocate a new dir block
        memset(&block, 0, STZFS_BLOCK_SIZE);
    } else {
        read_inode_data_block(inode, block_offset, &block);
    }

    // create entry
    entry = &block.entries[next_free_entry];
    entry->inode = target_inodeptr;
    strcpy(entry->name, name);
    inode->atom_count++;

    // write back dir block
    if (next_free_entry > 0) {
        write_inode_data_block(inode, block_offset, &block);
    } else {
        alloc_inode_data_block(inode, &block);
    }
    return 0;
}
