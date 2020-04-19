#ifndef STZFS_BLOCK_H
#define STZFS_BLOCK_H

#include <stddef.h>
#include <stdint.h>

#include "error.h"

stzfs_error_t block_read(int64_t blockptr, void* block);
stzfs_error_t block_readall(const int64_t* blockptr_arr, void* blocks, size_t length);
stzfs_error_t block_write(int64_t blockptr, const void* block);
stzfs_error_t block_allocptr(int64_t* blockptr);
stzfs_error_t block_alloc(int64_t* blockptr, const void* block);
stzfs_error_t block_free(const int64_t* blockptr_arr, size_t length);

#endif // STZFS_BLOCK_H
