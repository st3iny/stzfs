#ifndef STZFS_BITMAP_H
#define STZFS_BITMAP_H

#include <stdbool.h>
#include <stdint.h>

#include "error.h"

bool bitmap_is_block_allocated(int64_t blockptr);
bool bitmap_is_inode_allocated(int64_t inodeptr);
stzfs_error_t bitmap_alloc_block(int64_t* blockptr);
stzfs_error_t bitmap_alloc_inode(int64_t* inodeptr);
stzfs_error_t bitmap_free_block(int64_t blockptr);
stzfs_error_t bitmap_free_inode(int64_t inodeptr);

#endif // STZFS_BITMAP_H
