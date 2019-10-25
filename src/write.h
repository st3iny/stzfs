#ifndef STZFS_WRITE_H
#define STZFS_WRITE_H

#include "blocks.h"
#include "inode.h"
#include "types.h"

// write to filesystem
void write_block(blockptr_t blockptr, const void* block);
void write_inode(inodeptr_t inodeptr, const inode_t* inode);
blockptr_t write_inode_data_block(const inode_t* inode, blockptr_t offset, const void* block);
int write_or_alloc_inode_data_block(inode_t* inode, blockptr_t blockptr, const void* block);
int write_dir_entry(const inode_t* inode, const char* name, inodeptr_t target_inodeptr);

#endif // STZFS_WRITE_H
