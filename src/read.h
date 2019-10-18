#ifndef STZFS_READ_H
#define STZFS_READ_H

#include "blocks.h"
#include "inode.h"
#include "types.h"

// read from filesystem
void read_block(blockptr_t blockptr, void* block);
void read_blocks(const blockptr_t* blockptrs, void* blocks, blockptr_t length);
void read_super_block(super_block* block);
void read_inode(inodeptr_t inodeptr, inode_t* inode);
blockptr_t read_inode_data_block(const inode_t* inode, blockptr_t offset, void* block);
int read_inode_data_blocks(const inode_t* inode, void* data_block_array, blockptr_t length,
                           blockptr_t offset);

#endif // STZFS_READ_H
