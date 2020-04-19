#ifndef STZFS_ALLOC_H
#define STZFS_ALLOC_H

#include "bitmap_cache.h"
#include "block.h"
#include "inode.h"
#include "types.h"

// alloc in bitmap
int alloc_bitmap(objptr_t* index, bitmap_cache_t* cache);
int alloc_inodeptr(inodeptr_t* inodeptr);
int alloc_inode(inodeptr_t* inodeptr, const inode_t* inode);
blockptr_t alloc_inode_data_block(inode_t* inode, const void* block);
int alloc_inode_null_blocks(inode_t* inode, blockptr_t block_count);
int alloc_dir_entry(inode_t* inode, const char* name, inodeptr_t target_inodeptr);

#endif // STZFS_ALLOC_H

