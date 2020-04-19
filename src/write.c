#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "alloc.h"
#include "find.h"
#include "helpers.h"
#include "disk.h"
#include "read.h"
#include "super_block_cache.h"
#include "write.h"

// write inode to disk
void write_inode(inodeptr_t inodeptr, const inode_t* inode) {
    if (inodeptr == 0) {
        printf("write_inode: can't write protected inode 0\n");
        return;
    }

    const super_block* sb = super_block_cache;

    blockptr_t table_block_offset = inodeptr / INODE_BLOCK_ENTRIES;
    if (table_block_offset > sb->inode_table_length) {
        printf("write_inode: inode index out of bounds\n");
        return;
    }

    // place inode in table and write back table block
    blockptr_t table_blockptr = sb->inode_table + table_block_offset;
    inode_block table_block;
    block_read(table_blockptr, &table_block);
    table_block.inodes[inodeptr % INODE_BLOCK_ENTRIES] = *inode;
    block_write(table_blockptr, &table_block);
}

// read inode data block with relative offset
blockptr_t write_inode_data_block(inode_t* inode, blockptr_t offset, const void* block) {
    blockptr_t blockptr = find_inode_data_blockptr(inode, offset, ALLOC_SPARSE_YES);
    if (blockptr == 0) {
        printf("write_inode_data_block: could not write inode data block\n");
        return 0;
    }

    block_write(blockptr, block);
    return blockptr;
}

// write existing or allocate an new inode data block
int write_or_alloc_inode_data_block(inode_t* inode, blockptr_t blockptr, const void* block) {
    if (blockptr < inode->block_count) {
        return write_inode_data_block(inode, blockptr, block);
    } else if (blockptr == inode->block_count) {
        return alloc_inode_data_block(inode, block);
    } else {
        printf("write_or_alloc_inode_data_block: blockptr out of bounds\n");
        return -EPERM;
    }
}

// replace inodeptr of name in directory
int write_dir_entry(inode_t* inode, const char* name, inodeptr_t target_inodeptr) {
    if (!M_IS_DIR(inode->mode)) {
        printf("write_dir_entry: not a directory\n");
        return -ENOTDIR;
    }

    // search and replace name in directory
    for (blockptr_t offset = 0; offset < inode->block_count; offset++) {
        dir_block block;
        const blockptr_t blockptr = read_inode_data_block(inode, offset, &block);
        if (blockptr == 0) {
            printf("write_dir_entry: can't read directory block\n");
            return -EFAULT;
        }

        const size_t remaining_entries = inode->atom_count - offset * DIR_BLOCK_ENTRIES;
        const size_t entries = MIN(DIR_BLOCK_ENTRIES, remaining_entries);
        for (size_t entry = 0; entry < entries; entry++) {
            if (strcmp((const char*)block.entries[entry].name, name) == 0) {
                block.entries[entry].inode = target_inodeptr;
                write_inode_data_block(inode, offset, &block);
                return 0;
            }
        }
    }

    printf("write_dir_entry: name does not exist in directory\n");
    return -ENOENT;
}
