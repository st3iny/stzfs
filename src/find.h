#ifndef STZFS_FIND_H
#define STZFS_FIND_H

#include <stdbool.h>

#include "inode.h"
#include "types.h"

// find inodes and their data blocks
int find_file_inode(const char* file_path, int64_t* inodeptr, inode_t* inode,
                    int64_t* parent_inodeptr, inode_t* parent_inode, char* last_name);
int find_file_inode2(const char* file_path, file* f, file* parent, char* last_name);

#endif // STZFS_FIND_H
