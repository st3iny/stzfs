#ifndef STZFS_ALLOC_H
#define STZFS_ALLOC_H

#include "inode.h"
#include "types.h"

// alloc in bitmap
blockptr_t alloc_bitmap(const char* title, blockptr_t bitmap_offset, blockptr_t bitmap_length);
int alloc_block(blockptr_t* blockptr, const void* block);
inodeptr_t alloc_inode(const inode_t* new_inode);
blockptr_t alloc_inode_data_block(inode_t* inode, const void* block);
int alloc_dir_entry(inode_t* inode, const char* name, inodeptr_t target_inodeptr);

#endif // STZFS_ALLOC_H

