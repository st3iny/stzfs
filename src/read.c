#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "alloc.h"
#include "blocks.h"
#include "find.h"
#include "read.h"
#include "vm.h"

// read block from disk
void read_block(blockptr_t blockptr, void* block) {
    vm_read(blockptr * BLOCK_SIZE, block, BLOCK_SIZE);
}

// read block from blockptr if not zero or init the block with zeroes
int read_or_alloc_block(blockptr_t* blockptr, void* block) {
    if (*blockptr) {
        read_block(*blockptr, block);
        return 0;
    } else {
        memset(block, 0, BLOCK_SIZE);
        *blockptr = alloc_block(block);
        return 1;
    }
}

// read inode from disk
void read_inode(inodeptr_t inodeptr, inode_t* inode) {
    super_block sb;
    read_block(0, &sb);

    blockptr_t inode_table_block_offset = inodeptr / (BLOCK_SIZE / sizeof(inode_t));
    if (inode_table_block_offset >= sb.inode_table_length) {
        printf("read_inode: out of bounds while trying to read inode\n");
        return;
    }

    // get inode table block
    blockptr_t inode_table_blockptr = sb.inode_table + inode_table_block_offset;
    inode_block inode_table_block;
    read_block(inode_table_blockptr, &inode_table_block);

    // read inode from inode table block
    *inode = inode_table_block.inodes[inodeptr % (BLOCK_SIZE / sizeof(inode_t))];
}

// read inode data block with relative offset
blockptr_t read_inode_data_block(const inode_t* inode, blockptr_t offset, void* block) {
    blockptr_t blockptr = find_inode_data_blockptr(inode, offset);
    if (blockptr == 0) {
        printf("read_inode_data_block: could not read inode data block\n");
        return 0;
    }

    read_block(blockptr, block);
    return blockptr;
}

// read all data blocks of an inode
int read_inode_data_blocks(const inode_t* inode, void* data_block_array) {
    blockptr_t blockptrs[inode->block_count];
    int err = find_inode_data_blockptrs(inode, blockptrs);
    if (err) return err;

    for (size_t offset = 0; offset < inode->block_count; offset++) {
        read_block(blockptrs[offset], &((data_block*)data_block_array)[offset]);
    }

    return 0;
}
