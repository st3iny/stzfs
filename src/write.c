#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "alloc.h"
#include "blocks.h"
#include "find.h"
#include "vm.h"
#include "read.h"
#include "write.h"

// write block to disk
void write_block(blockptr_t blockptr, const void* block) {
    vm_write(blockptr * BLOCK_SIZE, block, BLOCK_SIZE);
}

// write inode to disk
void write_inode(inodeptr_t inodeptr, const inode_t* inode) {
    if (inodeptr == 0) {
        printf("write_inode: can't write protected inode 0\n");
        return;
    }

    super_block sb;
    read_block(0, &sb);

    blockptr_t table_block_offset = inodeptr / INODE_BLOCK_ENTRIES;
    if (table_block_offset > sb.inode_table_length) {
        printf("write_inode: inode index out of bounds\n");
        return;
    }

    // place inode in table and write back table block
    blockptr_t table_blockptr = sb.inode_table + table_block_offset;
    inode_block table_block;
    read_block(table_blockptr, &table_block);
    table_block.inodes[inodeptr % INODE_BLOCK_ENTRIES] = *inode;
    write_block(table_blockptr, &table_block);
}

// read inode data block with relative offset
blockptr_t write_inode_data_block(const inode_t* inode, blockptr_t offset, const void* block) {
    blockptr_t blockptr = find_inode_data_blockptr(inode, offset);
    if (blockptr == 0) {
        printf("write_inode_data_block: could not write inode data block\n");
        return 0;
    }

    write_block(blockptr, block);
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
int write_dir_entry(const inode_t* inode, const char* name, inodeptr_t target_inodeptr) {
    if ((inode->mode & M_DIR) == 0) {
        printf("write_dir_entry: not a directory\n");
        return -ENOTDIR;
    }

    dir_block dir_blocks[inode->block_count];
    read_inode_data_blocks(inode, dir_blocks);

    // search and replace name in directory
    for (blockptr_t offset = 0; offset < inode->block_count; offset++) {
        for (size_t entry = 0; entry < DIR_BLOCK_ENTRIES; entry++) {
            dir_block_entry* entry_data = &dir_blocks[offset].entries[entry];
            if (strcmp(entry_data->name, name) == 0) {
                entry_data->inode = target_inodeptr;
                write_inode_data_block(inode, offset, &dir_blocks[offset]);
                return 0;
            }
        }
    }

    printf("write_dir_entry: name does not exist in directory\n");
    return -ENOENT;
}
