#ifndef STZFS_INODE_H
#define STZFS_INODE_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "error.h"
#include "types.h"

// blocks each direction/indirection level can hold
#define INODE_DIRECT_BLOCKS (12)
#define INODE_SINGLE_INDIRECT_BLOCKS (INDIRECT_BLOCK_ENTRIES)
#define INODE_DOUBLE_INDIRECT_BLOCKS (INDIRECT_BLOCK_ENTRIES * INDIRECT_BLOCK_ENTRIES)
#define INODE_TRIPLE_INDIRECT_BLOCKS (INODE_DOUBLE_INDIRECT_BLOCKS * INDIRECT_BLOCK_ENTRIES)
#define INODE_MAX_BLOCKS (INODE_DIRECT_BLOCKS + INODE_SINGLE_INDIRECT_BLOCKS + \
                          INODE_DOUBLE_INDIRECT_BLOCKS + INODE_TRIPLE_INDIRECT_BLOCKS)

// relative inode offset of the first data block
#define INODE_SINGLE_INDIRECT_OFFSET (INODE_DIRECT_BLOCKS)
#define INODE_DOUBLE_INDIRECT_OFFSET (INODE_SINGLE_INDIRECT_OFFSET + INODE_SINGLE_INDIRECT_BLOCKS)
#define INODE_TRIPLE_INDIRECT_OFFSET (INODE_DOUBLE_INDIRECT_OFFSET + INODE_DOUBLE_INDIRECT_BLOCKS)

// 128 bytes
typedef struct inode_t {
    stzfs_mode_t mode;
    int16_t uid;
    int16_t gid;
    uint16_t link_count;
    struct timespec atime;
    struct timespec mtime;
    struct timespec ctime;
    uint64_t atom_count;
    uint32_t block_count;
    blockptr_t data_direct[INODE_DIRECT_BLOCKS];
    blockptr_t data_single_indirect;
    blockptr_t data_double_indirect;
    blockptr_t data_triple_indirect;
} inode_t;

#define INODE_SIZE (sizeof(inode_t))
#define INODE_BLOCK_ENTRIES (STZFS_BLOCK_SIZE / sizeof(inode_t))

// group inode and inodeptr for convenience
typedef struct file {
    int64_t inodeptr;
    inode_t inode;
} file;

// sparse alloc enum helper
typedef bool alloc_sparse_t;
enum { ALLOC_SPARSE_NO = false, ALLOC_SPARSE_YES = true };

// functions
stzfs_error_t inode_allocptr(int64_t* inodeptr);
stzfs_error_t inode_alloc(int64_t* inodeptr, const inode_t* inode);
stzfs_error_t inode_append_data_blockptr(inode_t* inode, int64_t blockptr);
stzfs_error_t inode_alloc_data_block(inode_t* inode, const void* block);
stzfs_error_t inode_append_null_blocks(inode_t* inode, int64_t block_count);
stzfs_error_t inode_free(int64_t inodeptr, inode_t* inode);
stzfs_error_t inode_truncate(inode_t* inode, int64_t offset);
stzfs_error_t inode_free_last_data_block(inode_t* inode);
stzfs_error_t inode_read(int64_t inodeptr, inode_t* inode);
stzfs_error_t inode_read_data_block(inode_t* inode, int64_t offset, void* block, int64_t* blockptr_out);
stzfs_error_t inode_read_data_blocks(inode_t* inode, void* block_arr, size_t length, int64_t offset);
stzfs_error_t inode_write(int64_t inodeptr, const inode_t* inode);
stzfs_error_t inode_write_data_block(inode_t* inode, int64_t offset, const void* block);
stzfs_error_t inode_write_or_alloc_data_block(inode_t* inode, int64_t offset, const void* block);
stzfs_error_t inode_find_data_blockptr(inode_t* inode, int64_t offset, alloc_sparse_t alloc_sparse, int64_t* blockptr_out);
stzfs_error_t inode_find_data_blockptrs(inode_t* inode, int64_t offset, int64_t* blockptr_arr, size_t length);

#endif // STZFS_INODE_H
