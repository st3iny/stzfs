#ifndef FILESYSTEM_BLOCKS_H
#define FILESYSTEM_BLOCKS_H

#include <bits/types/time_t.h>
#include <stdint.h>

#include "types.h"

typedef struct super_block {
    blockptr_t block_count;
    blockptr_t free_blocks;
    blockptr_t block_bitmap;
    blockptr_t block_bitmap_length;
    blockptr_t inode_bitmap;
    blockptr_t inode_bitmap_length;
    blockptr_t inode_table;
    blockptr_t inode_table_length;
    inodeptr_t inode_count;

    int8_t padding[BLOCK_SIZE - sizeof(blockptr_t) * 8 - sizeof(inodeptr_t)];
} __attribute__ ((packed)) super_block;

// 128 bytes
typedef struct inode {
    int16_t mode;
    int16_t uid;
    int16_t gid;
    time_t crtime;
    time_t mtime;
    uint16_t link_count;
    uint64_t byte_length;
    uint32_t block_length;
    // 36 bytes
    blockptr_t data_direct[20];
    blockptr_t data_single_indirect;
    blockptr_t data_double_indirect;
    blockptr_t data_triple_indirect;
} __attribute__ ((packed)) inode;

#define INODE_SIZE sizeof(inode)

typedef struct inode_block {
    inode inodes[BLOCK_SIZE / sizeof(inode)];
} __attribute__ ((packed)) inode_block;

// 256 bytes
typedef struct dir_block_entry {
    filename_t name[MAX_FILENAME_LENGTH];
    inodeptr_t inode;
} __attribute__ ((packed)) dir_block_entry;

typedef struct dir_block {
    uint8_t length;
    dir_block_entry entries[BLOCK_SIZE / sizeof(dir_block_entry) - 1];

    int8_t padding[sizeof(dir_block_entry) - sizeof(uint8_t)];
} __attribute__ ((packed)) dir_block;

typedef struct indirect_block {
    blockptr_t blocks[BLOCK_SIZE / sizeof(blockptr_t)];
} __attribute__ ((packed)) indirect_block;

typedef struct bitmap_block {
    uint64_t bitmap[BLOCK_SIZE / 8];
} __attribute__ ((packed)) bitmap_block;

typedef struct data_block {
    uint8_t data[BLOCK_SIZE];
} __attribute__ ((packed)) data_block;

#endif // FILESYSTEM_BLOCKS_H
