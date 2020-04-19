#ifndef STZFS_DISK_H
#define STZFS_DISK_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#include "error.h"

stzfs_error_t disk_create_file(const char* path, off_t size);
stzfs_error_t disk_set_file(const char* path);
stzfs_error_t disk_write(off_t addr, const void* buffer, size_t length);
stzfs_error_t disk_read(off_t addr, void* buffer, size_t length);
void disk_close(void);
off_t disk_get_size(void);
int disk_get_fd(void);

#endif // STZFS_DISK_H
