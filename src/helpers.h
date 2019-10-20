#ifndef STZFS_HELPERS_H
#define STZFS_HELPERS_H

#include <stddef.h>
#include <sys/stat.h>

#include "types.h"
#include "inode.h"

// file helpers
int file_exists(const char* path);

// update inode timestamps
void touch_atime(inode_t* inode);
void touch_mtime_and_ctime(inode_t* inode);
void touch_ctime(inode_t* inode);

// misc helpers
void memcpy_min(void* dest, const void* src, size_t mult, size_t a, size_t b);
int unlink_file_or_dir(const char* path, int allow_dir);
stzfs_mode_t mode_posix_to_stzfs(mode_t mode);
mode_t mode_stzfs_to_posix(stzfs_mode_t stzfs_mode);

// integer divide a / b and ceil if a % b > 0 (there is a remainder)
#define DIV_CEIL(a, b) (((a) / (b)) + (((a) % (b)) != 0))

// return minimum of a and b
#define MIN(a, b) ((a) < (b) ? (a) : (b))

// return maximum of a and b
#define MAX(a, b) ((a) > (b) ? (a) : (b))

// error handling convenience
#define TRY(try, catch) { int _err = try; if(_err) { catch; return _err; } }

#endif // STZFS_HELPERS_H
