#ifndef STZFS_WRITE_H
#define STZFS_WRITE_H

#include "blocks.h"
#include "inode.h"
#include "types.h"

// write to filesystem
int write_dir_entry(inode_t* inode, const char* name, inodeptr_t target_inodeptr);

#endif // STZFS_WRITE_H
