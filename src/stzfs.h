#ifndef STZFS_STZFS_H
#define STZFS_STZFS_H

#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "fuse.h"
#include "types.h"

blockptr_t stzfs_makefs(inodeptr_t inode_count);

void* stzfs_fuse_init(struct fuse_conn_info* conn, struct fuse_config* cfg);
void stzfs_init(void);
void stzfs_destroy(void* private_data);

int stzfs_open(const char* file_path, struct fuse_file_info* file_info);

int stzfs_read(const char* file_path, char* buffer, size_t length, off_t offset,
               struct fuse_file_info* file_info);
int stzfs_write(const char* file_path, const char* buffer, size_t length, off_t offset,
                struct fuse_file_info* file_info);

int stzfs_create(const char* file_path, mode_t mode, struct fuse_file_info* file_info);
int stzfs_rename(const char* src, const char* dst, unsigned int flags);
int stzfs_link(const char* src, const char* dest);
int stzfs_unlink(const char* path);
int stzfs_truncate(const char* path, off_t offset, struct fuse_file_info* fi);
int stzfs_symlink(const char* target, const char* link_name);
int stzfs_readlink(const char* path, char* buffer, size_t length);

int stzfs_mkdir(const char* path, mode_t mode);
int stzfs_rmdir(const char* path);
int stzfs_readdir(const char* path, void* buffer, fuse_fill_dir_t filler, off_t offset,
                  struct fuse_file_info* file_info, enum fuse_readdir_flags flags);

int stzfs_statfs(const char* path, struct statvfs* stat);

int stzfs_getattr(const char* path, struct stat* st, struct fuse_file_info* file_info);
int stzfs_chown(const char* path, uid_t uid, gid_t gid, struct fuse_file_info* fi);
int stzfs_chmod(const char* path, mode_t mode, struct fuse_file_info* fi);
int stzfs_utimens(const char* path, const struct timespec tv[2], struct fuse_file_info* fi);

extern struct fuse_operations stzfs_ops;

#endif // STZFS_STZFS_H
