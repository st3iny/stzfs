#ifndef FILESYSTEM_TYPES_H
#define FILESYSTEM_TYPES_H

#include <stdint.h>

#define EOF -1
#define BLOCK_SIZE 4096 // 4 KiB
#define MAX_FILENAME_LENGTH 256 - sizeof(inodeptr_t) // 251 characters

// file modes
#define M_RU 0b100000000 // user read
#define M_WU 0b010000000 // user write
#define M_XU 0b001000000 // user execute
#define M_RG 0b000100000 // group read
#define M_WG 0b000010000 // group write
#define M_XG 0b000001000 // group execute
#define M_RO 0b000000100 // others read
#define M_WO 0b000000010 // others write
#define M_XO 0b000000001 // others execute

typedef int      buffer_t;
typedef int8_t   filename_t;
typedef uint16_t fd_t;
typedef uint32_t inodeptr_t;
typedef uint32_t blockptr_t;
typedef uint64_t fileptr_t;

#endif // FILESYSTEM_TYPES_H
