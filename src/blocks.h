#ifndef FILESYSTEM_BLOCKS_H
#define FILESYSTEM_BLOCKS_H

#include <stdint.h>

#include "types.h"
#include "inode.h"

#define SUPER_BLOCKPTR (0)

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

    int8_t padding[STZFS_BLOCK_SIZE - sizeof(blockptr_t) * 8 - sizeof(inodeptr_t) * 2];
} super_block;

typedef struct inode_block {
    inode_t inodes[INODE_BLOCK_ENTRIES];
} inode_block;

// 256 bytes
typedef struct dir_block_entry {
    filename_t name[MAX_FILENAME_LENGTH];
    inodeptr_t inode;
} dir_block_entry;

#define DIR_BLOCK_ENTRIES (STZFS_BLOCK_SIZE / sizeof(dir_block_entry))

typedef struct dir_block {
    dir_block_entry entries[DIR_BLOCK_ENTRIES];
} dir_block;

#define INDIRECT_BLOCK_ENTRIES (STZFS_BLOCK_SIZE / sizeof(blockptr_t))

typedef struct indirect_block {
    blockptr_t blocks[INDIRECT_BLOCK_ENTRIES];
} indirect_block;

#define BITMAP_BLOCK_ENTRIES (STZFS_BLOCK_SIZE / sizeof(bitmap_entry_t))

typedef struct bitmap_block {
    bitmap_entry_t bitmap[BITMAP_BLOCK_ENTRIES];
} bitmap_block;

typedef struct data_block {
    uint8_t data[STZFS_BLOCK_SIZE];
} data_block;

#endif // FILESYSTEM_BLOCKS_H
