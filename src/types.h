#ifndef FILESYSTEM_TYPES_H
#define FILESYSTEM_TYPES_H

#include <stdint.h>

#define EOF (-1)
#define BLOCK_SIZE (4096) // 4 KiB
#define MAX_FILENAME_LENGTH (256 - sizeof(inodeptr_t)) // 251 characters

// file modes
#define M_RU     0b1000000000000 // user read
#define M_WU     0b0100000000000 // user write
#define M_XU     0b0010000000000 // user execute
#define M_RG     0b0001000000000 // group read
#define M_WG     0b0000100000000 // group write
#define M_XG     0b0000010000000 // group execute
#define M_RO     0b0000001000000 // others read
#define M_WO     0b0000000100000 // others write
#define M_XO     0b0000000010000 // others execute
#define M_DIR    0b0000000001000 // directory
#define M_SETUID 0b0000000000100 // set uid
#define M_SETGID 0b0000000000010 // set gid
#define M_STICKY 0b0000000000001 // sticky

typedef int8_t   filename_t;
typedef int16_t  stzfs_mode_t;
typedef uint32_t inodeptr_t;
typedef uint32_t blockptr_t;

#endif // FILESYSTEM_TYPES_H
