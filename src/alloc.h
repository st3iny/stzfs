#ifndef STZFS_ALLOC_H
#define STZFS_ALLOC_H

#include "bitmap_cache.h"
#include "inode.h"
#include "types.h"

// alloc in bitmap
blockptr_t alloc_bitmap(bitmap_cache_t* cache);
int alloc_block(blockptr_t* blockptr, const void* block);
inodeptr_t alloc_inode(const inode_t* new_inode);
blockptr_t alloc_inode_data_block(inode_t* inode, const void* block);
int alloc_dir_entry(inode_t* inode, const char* name, inodeptr_t target_inodeptr);

#endif // STZFS_ALLOC_H

