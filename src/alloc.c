#include <errno.h>
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
    // get next free block
    bitmap_block ba;
    blockptr_t current_bitmap_block = bitmap_offset;
    blockptr_t next_free = 0;
    while (next_free == 0 && current_bitmap_block < bitmap_offset + bitmap_length) {
        vm_read(current_bitmap_block * BLOCK_SIZE, &ba, BLOCK_SIZE);

        for (int i = 0; i < BLOCK_SIZE / 8; i++) {
            uint64_t data = ba.bitmap[i];
            if (data == 0xffffffffffffffff) {
                continue;
            }

            // there is at least one free alloc available
            int offset = 0;
            while ((data & 1) != 0) {
                data >>= 1;
                offset++;
            }

            // mark alloc in bitmap
            ba.bitmap[i] |= 1UL << offset;
            next_free = (current_bitmap_block - bitmap_offset) * BLOCK_SIZE * 8 + i * 64 + offset;
            break;
        }

        current_bitmap_block++;
    }

    if (next_free == 0) {
        printf("alloc_bitmap: could not allocate a free %s\n", title);
        return 0;
    }

    // write bitmap
    vm_write((current_bitmap_block - 1) * BLOCK_SIZE, &ba, BLOCK_SIZE);

    return next_free;
}

// write a block in place (BLOCK_SIZE bytes of given object)
blockptr_t alloc_block(const void* new_block) {
    super_block sb;
    read_block(0, &sb);

    // get next free block and write data block
    blockptr_t next_free_block = alloc_bitmap("block", sb.block_bitmap, sb.block_bitmap_length);
    if (next_free_block == 0) {
        printf("alloc_block: could not allocate block\n");
        return -ENOSPC;
    }
    write_block(next_free_block, new_block);

    // update superblock
    sb.free_blocks--;
    write_block(0, &sb);

    return next_free_block;
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
        int changed;
    } level;

    blockptr_t offset = inode->block_count;
    blockptr_t blockptr = alloc_block(block);
    if (offset < INODE_DIRECT_BLOCKS) {
        inode->data_direct[offset] = blockptr;
    } else if ((offset -= INODE_DIRECT_BLOCKS) < INODE_SINGLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = &inode->data_single_indirect};
        read_or_alloc_block(level1.blockptr, &level1.block);

        level1.block.blocks[offset] = blockptr;

        write_block(*level1.blockptr, &level1.block);
    } else if ((offset -= INODE_SINGLE_INDIRECT_BLOCKS) < INODE_DOUBLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = &inode->data_double_indirect};
        if (read_or_alloc_block(level1.blockptr, &level1.block)) level1.changed = 1;

        level level2 = {.blockptr = &level1.block.blocks[offset / INODE_SINGLE_INDIRECT_BLOCKS]};
        if (read_or_alloc_block(level2.blockptr, &level2.block)) level1.changed = 1;

        level2.block.blocks[offset % INODE_SINGLE_INDIRECT_BLOCKS] = blockptr;

        if (level1.changed) write_block(*level1.blockptr, &level1.block);
        write_block(*level2.blockptr, &level2.block);
    } else if ((offset -= INODE_DOUBLE_INDIRECT_BLOCKS) < INODE_TRIPLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = &inode->data_triple_indirect};
        if (read_or_alloc_block(level1.blockptr, &level1.block)) level1.changed = 1;

        level level2 = {.blockptr = &level1.block.blocks[offset / INODE_DOUBLE_INDIRECT_BLOCKS]};
        if (read_or_alloc_block(level2.blockptr, &level2.block)) {
            level1.changed = 1;
            level2.changed = 1;
        }

        level level3 = {.blockptr = &level2.block.blocks[offset % INODE_DOUBLE_INDIRECT_BLOCKS]};
        if (read_or_alloc_block(level3.blockptr, &level3.block)) level2.changed = 1;

        level3.block.blocks[offset % INODE_SINGLE_INDIRECT_BLOCKS] = blockptr;

        if (level1.changed) write_block(*level1.blockptr, &level1.block);
        if (level2.changed) write_block(*level2.blockptr, &level2.block);
        write_block(*level3.blockptr, &level3.block);
    }

    // one block is allocated in each case
    inode->block_count++;
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
