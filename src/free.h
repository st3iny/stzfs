#ifndef STZFS_FREE_H
#define STZFS_FREE_H

#include <stddef.h>

#include "bitmap_cache.h"
#include "inode.h"
#include "types.h"

// free filesystem data structures
int free_dir_entry(inode_t* inode, const char* name);

#endif // STZFS_FREE_H
