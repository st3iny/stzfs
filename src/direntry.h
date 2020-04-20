#ifndef STZFS_DIRENTRY_H
#define STZFS_DIRENTRY_H

#include <stdint.h>

#include "error.h"
#include "inode.h"

stzfs_error_t direntry_alloc(inode_t* inode, const char* name, int64_t target_inodeptr);
stzfs_error_t direntry_free(inode_t* inode, const char* name);
stzfs_error_t direntry_write(inode_t* inode, const char* name, int64_t target_inodeptr);
stzfs_error_t direntry_find(inode_t* inode, const char* name, int64_t* found_inodeptr);

#endif // STZFS_DIRENTRY_H
