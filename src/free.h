#ifndef STZFS_FREE_H
#define STZFS_FREE_H

#include <stddef.h>

#include "inode.h"
#include "types.h"

// free filesystem data structures
int free_bitmap(blockptr_t index, blockptr_t bitmap_offset, blockptr_t bitmap_length);
int free_blocks(const blockptr_t* blockptrs, size_t length);
int free_inode(inodeptr_t inodeptr, inode_t* inode);
int free_inode_data_blocks(inode_t* inode, blockptr_t offset);
int free_last_inode_data_block(inode_t* inode);
int free_dir_entry(inode_t* inode, const char* name);

#endif // STZFS_FREE_H