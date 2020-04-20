#ifndef STZFS_ALLOC_H
#define STZFS_ALLOC_H

#include "bitmap_cache.h"
#include "block.h"
#include "inode.h"
#include "types.h"

// alloc in bitmap
int alloc_bitmap(objptr_t* index, bitmap_cache_t* cache);
int alloc_dir_entry(inode_t* inode, const char* name, inodeptr_t target_inodeptr);

#endif // STZFS_ALLOC_H

