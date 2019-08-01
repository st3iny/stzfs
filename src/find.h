#ifndef STZFS_FIND_H
#define STZFS_FIND_H

#include "inode.h"
#include "types.h"

// find inodes and their data blocks
int find_file_inode(const char* file_path, inodeptr_t* inodeptr, inode_t* inode,
                    inodeptr_t* parent_inodeptr, inode_t* parent_inode, char* last_name);
int find_file_inode2(const char* file_path, file* f, file* parent, char* last_name);
int find_name(const char* name, const inode_t* inode, inodeptr_t* found_inodeptr);
blockptr_t find_inode_data_blockptr(const inode_t* inode, blockptr_t offset);
int find_inode_data_blockptrs(const inode_t* inode, blockptr_t* blockptrs);

#endif // STZFS_FIND_H
