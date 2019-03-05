#ifndef FILESYSTEM_SYSCALLS_H
#define FILESYSTEM_SYSCALLS_H

#define FUSE_USE_VERSION 34
#include <fuse3/fuse.h>

#include "types.h"

blockptr_t sys_makefs(inodeptr_t inode_count);

int sys_open(const char* file_path, struct fuse_file_info* file_info);
int sys_release(const char* file_path, struct fuse_file_info* file_info);

int sys_read(const char* file_path, char* buffer, size_t length, off_t offset,
             struct fuse_file_info* file_info);
int sys_write(const char* file_path, const char* buffer, size_t length, off_t offset,
              struct fuse_file_info* file_info);

int sys_create(const char* file_path, mode_t mode, struct fuse_file_info* file_info);
int sys_rename(const filename_t* src, const filename_t* dst);
int sys_unlink(const filename_t* path);

#endif // FILESYSTEM_SYSCALLS_H
