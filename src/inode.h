#ifndef STZFS_INODE_H
#define STZFS_INODE_H

#include <stdint.h>
#include <time.h>

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
    inodeptr_t inodeptr;
    inode_t inode;
} file;

#endif // STZFS_INODE_H
