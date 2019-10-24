#ifndef FILESYSTEM_TYPES_H
#define FILESYSTEM_TYPES_H

#include <stdint.h>

// defs
#define EOF (-1)
#define STZFS_BLOCK_SIZE_BITS (12) // 4 KiB
#define STZFS_BLOCK_SIZE (1 << STZFS_BLOCK_SIZE_BITS)
#define MAX_FILENAME_LENGTH (256 - sizeof(inodeptr_t)) // 251 characters

// file modes
#define M_RU     (0b1000000000000000) // user read
#define M_WU     (0b0100000000000000) // user write
#define M_XU     (0b0010000000000000) // user execute
#define M_RG     (0b0001000000000000) // group read
#define M_WG     (0b0000100000000000) // group write
#define M_XG     (0b0000010000000000) // group execute
#define M_RO     (0b0000001000000000) // others read
#define M_WO     (0b0000000100000000) // others write
#define M_XO     (0b0000000010000000) // others execute
#define M_SETUID (0b0000000001000000) // set uid
#define M_SETGID (0b0000000000100000) // set gid
#define M_STICKY (0b0000000000010000) // sticky

// last 2 bits decide over file type
#define M_TYPE_MASK (0b11)
#define M_REG    (0b0000000000000000) // regular file
#define M_LNK    (0b0000000000000001) // symbolic link
#define M_DIR    (0b0000000000000010) // directory

// convenience file type checker macros
#define M_IS_REG(mode) (((mode) & M_TYPE_MASK) == M_REG)
#define M_IS_LNK(mode) (((mode) & M_TYPE_MASK) == M_LNK)
#define M_IS_DIR(mode) (((mode) & M_TYPE_MASK) == M_DIR)

// types
typedef int8_t   filename_t;
typedef int16_t  stzfs_mode_t;
typedef uint32_t inodeptr_t;
typedef uint32_t blockptr_t;
typedef uint64_t bitmap_entry_t;

// can safely store inode and block pointers
typedef uint32_t objptr_t;

#endif // FILESYSTEM_TYPES_H
