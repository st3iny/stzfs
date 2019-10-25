#ifndef STZFS_ALLOC_H
#define STZFS_ALLOC_H

#include "bitmap_cache.h"
#include "inode.h"
#include "types.h"

// alloc in bitmap
int alloc_inodeptr(inodeptr_t* inodeptr);
int alloc_inode(inodeptr_t* inodeptr, const inode_t* inode);
int alloc_blockptr(blockptr_t* blockptr);
int alloc_block(blockptr_t* blockptr, const void* block);
blockptr_t alloc_inode_data_block(inode_t* inode, const void* block);
int alloc_dir_entry(inode_t* inode, const char* name, inodeptr_t target_inodeptr);

#endif // STZFS_ALLOC_H

