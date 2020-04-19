#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "block.h"
#include "find.h"
#include "read.h"
#include "super_block_cache.h"
#include "disk.h"

// read inode from disk
void read_inode(inodeptr_t inodeptr, inode_t* inode) {
    const super_block* sb = super_block_cache;

    blockptr_t inode_table_block_offset = inodeptr / (STZFS_BLOCK_SIZE / sizeof(inode_t));
    if (inode_table_block_offset >= sb->inode_table_length) {
        printf("read_inode: out of bounds while trying to read inode\n");
        return;
    }

    // get inode table block
    blockptr_t inode_table_blockptr = sb->inode_table + inode_table_block_offset;
    inode_block inode_table_block;
    block_read(inode_table_blockptr, &inode_table_block);

    // read inode from inode table block
    *inode = inode_table_block.inodes[inodeptr % (STZFS_BLOCK_SIZE / sizeof(inode_t))];
}

// read inode data block with relative offset
blockptr_t read_inode_data_block(inode_t* inode, blockptr_t offset, void* block) {
    blockptr_t blockptr = find_inode_data_blockptr(inode, offset, ALLOC_SPARSE_NO);
    if (blockptr == 0) {
        printf("read_inode_data_block: could not read inode data block\n");
        return 0;
    }

    block_read(blockptr, block);
    return blockptr;
}

// read data blocks of an inode and store them to a buffer
int read_inode_data_blocks(inode_t* inode, void* data_block_array, blockptr_t length,
                           blockptr_t offset) {
    blockptr_t blockptrs[length];
    int err = find_inode_data_blockptrs(inode, blockptrs, length, offset);
    if (err) return err;

    block_readall(blockptrs, data_block_array, length);

    return 0;
}
