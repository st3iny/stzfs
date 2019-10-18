#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "alloc.h"
#include "blocks.h"
#include "read.h"
#include "vm.h"
#include "write.h"

// alloc inode or block in given bitmap
blockptr_t alloc_bitmap(const char* title, blockptr_t bitmap_offset, blockptr_t bitmap_length) {
    bitmap_block ba;
    blockptr_t current_bitmap_block = bitmap_offset;
    blockptr_t next_free = 0;
    while (next_free == 0 && current_bitmap_block < bitmap_offset + bitmap_length) {
        read_block(current_bitmap_block, &ba);

        for (int i = 0; i < BLOCK_SIZE; i++) {
            int* ba_entry = (int*)((void*)&ba + i);
            int data = *ba_entry & 0xff;
            if (data == 0xff) {
                continue;
            }

            // there is at least one free alloc available
            int offset = 0;
            while (offset < 8 && (data & 1) != 0) {
                data >>= 1;
                offset++;
            }

            // mark alloc in bitmap
            // ba.bitmap[i] |= 1 << offset;
            *ba_entry = (*ba_entry & 0xff) | (1 << offset);
            next_free = (current_bitmap_block - bitmap_offset) * BLOCK_SIZE * 8 + i * 8 + offset;
            break;
        }

        current_bitmap_block++;
    }

    if (next_free == 0) {
        printf("alloc_bitmap: could not allocate a free %s\n", title);
        return 0;
    }

    write_block(current_bitmap_block - 1, &ba);

    return next_free;
}

// allocates and writes a new block in place
int alloc_block(blockptr_t* blockptr, const void* block) {
    super_block sb;
    read_super_block(&sb);

    // get next free block and write data block
    const blockptr_t free_block = alloc_bitmap("block", sb.block_bitmap, sb.block_bitmap_length);
    if (free_block == 0) {
        printf("alloc_block: could not allocate block\n");
        return -ENOSPC;
    }
    write_block(free_block, block);

    // update superblock
    sb.free_blocks--;
    write_super_block(&sb);

    *blockptr = free_block;
    return 0;
}

// write an inode in place
inodeptr_t alloc_inode(const inode_t* new_inode) {
    super_block sb;
    vm_read(0, &sb, BLOCK_SIZE);

    // get next free inode
    inodeptr_t next_free_inode = alloc_bitmap("inode", sb.inode_bitmap, sb.inode_bitmap_length);

    // get inode table block
    blockptr_t inode_table_blockptr = sb.inode_table + next_free_inode / (BLOCK_SIZE / sizeof(inode_t));
    inode_block inode_table_block;
    vm_read(inode_table_blockptr * BLOCK_SIZE, &inode_table_block, BLOCK_SIZE);

    // place inode into table and write table block
    inode_table_block.inodes[next_free_inode % (BLOCK_SIZE / sizeof(inode_t))] = *new_inode;
    vm_write(inode_table_blockptr * BLOCK_SIZE, &inode_table_block, BLOCK_SIZE);

    return next_free_inode;
}

// alloc a new data block for an inode
blockptr_t alloc_inode_data_block(inode_t* inode, const void* block) {
    if (inode->block_count >= INODE_MAX_BLOCKS) {
        printf("alloc_inode_data_block: inode has reached max block count\n");
        return 0;
    }

    // level struct for indirection convenience
    typedef struct level {
        blockptr_t* blockptr;
        indirect_block block;
        bool changed;
    } level;

    // alloc data block
    blockptr_t blockptr;
    alloc_block(&blockptr, block);
    inode->block_count++;

    // handle indirection
    blockptr_t offset = inode->block_count - 1;
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

    return blockptr;
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
        memset(&block, 0, BLOCK_SIZE);
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
