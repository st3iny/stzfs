#ifndef FILESYSTEM_SYSCALLS_H
#define FILESYSTEM_SYSCALLS_H

#include "fuse.h"
#include "types.h"

blockptr_t stzfs_makefs(inodeptr_t inode_count);

void* stzfs_init(struct fuse_conn_info* conn, struct fuse_config* cfg);

int stzfs_open(const char* file_path, struct fuse_file_info* file_info);

int stzfs_read(const char* file_path, char* buffer, size_t length, off_t offset,
               struct fuse_file_info* file_info);
int stzfs_write(const char* file_path, const char* buffer, size_t length, off_t offset,
                struct fuse_file_info* file_info);

int stzfs_getattr(const char* path, struct stat* st, struct fuse_file_info* file_info);
int stzfs_create(const char* file_path, mode_t mode, struct fuse_file_info* file_info);
int stzfs_rename(const char* src, const char* dst, unsigned int flags);
int stzfs_unlink(const char* path);

int stzfs_mkdir(const char* path, mode_t mode);
int stzfs_rmdir(const char* path);
int stzfs_readdir(const char* path, void* buffer, fuse_fill_dir_t filler, off_t offset,
                  struct fuse_file_info* file_info, enum fuse_readdir_flags flags);

int stzfs_statfs(const char* path, struct statvfs* stat);

extern struct fuse_operations stzfs_ops;

#endif // FILESYSTEM_SYSCALLS_H
