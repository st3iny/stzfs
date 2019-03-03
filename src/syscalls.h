#ifndef FILESYSTEM_SYSCALLS_H
#define FILESYSTEM_SYSCALLS_H

#include "types.h"

blockptr_t sys_makefs(inodeptr_t inode_count);

fd_t sys_open(const filename_t* path);
int sys_close(fd_t fd);
unsigned int sys_write(fd_t fd, buffer_t* buffer, fileptr_t offset, fileptr_t length);
int sys_read(fd_t fd, const buffer_t* buffer, fileptr_t offset, fileptr_t length);

int sys_create(const filename_t* path);
int sys_rename(const filename_t* src, const filename_t* dst);
int sys_unlink(const filename_t* path);

#endif // FILESYSTEM_SYSCALLS_H