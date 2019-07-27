#ifndef FILESYSTEM_BLOCKS_H
#define FILESYSTEM_BLOCKS_H

#include <bits/types/time_t.h>
#include <stdint.h>

#include "types.h"

typedef struct super_block {
    blockptr_t block_count;
    blockptr_t free_blocks;
    inodeptr_t free_inodes;
    blockptr_t block_bitmap;
    blockptr_t block_bitmap_length;
    blockptr_t inode_bitmap;
    blockptr_t inode_bitmap_length;
    blockptr_t inode_table;
    blockptr_t inode_table_length;
    inodeptr_t inode_count;

    int8_t padding[BLOCK_SIZE - sizeof(blockptr_t) * 8 - sizeof(inodeptr_t) * 2];
} __attribute__ ((packed)) super_block;

// blocks each direction/indirection level can hold
#define INODE_DIRECT_BLOCKS (20)
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
    time_t crtime;
    time_t mtime;
    uint64_t atom_count;
    uint32_t block_count;
    blockptr_t data_direct[INODE_DIRECT_BLOCKS];
    blockptr_t data_single_indirect;
    blockptr_t data_double_indirect;
    blockptr_t data_triple_indirect;
} inode_t;

#define INODE_SIZE (sizeof(inode_t))
#define INODE_BLOCK_ENTRIES (BLOCK_SIZE / sizeof(inode_t))

typedef struct inode_block {
    inode_t inodes[INODE_BLOCK_ENTRIES];
} inode_block;

// 256 bytes
typedef struct dir_block_entry {
    filename_t name[MAX_FILENAME_LENGTH];
    inodeptr_t inode;
} dir_block_entry;

#define DIR_BLOCK_ENTRIES (BLOCK_SIZE / sizeof(dir_block_entry))

typedef struct dir_block {
    dir_block_entry entries[DIR_BLOCK_ENTRIES];
} dir_block;

#define INDIRECT_BLOCK_ENTRIES (BLOCK_SIZE / sizeof(blockptr_t))

typedef struct indirect_block {
    blockptr_t blocks[INDIRECT_BLOCK_ENTRIES];
} indirect_block;

typedef struct bitmap_block {
    uint64_t bitmap[BLOCK_SIZE / 8];
} bitmap_block;

typedef struct data_block {
    uint8_t data[BLOCK_SIZE];
} data_block;

#endif // FILESYSTEM_BLOCKS_H
