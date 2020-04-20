#ifndef STZFS_INODEPTR_H
#define STZFS_INODEPTR_H

#include <stdbool.h>
#include <stdint.h>

bool inodeptr_is_valid(int64_t inodeptr);
bool inodeptr_is_protected(int64_t inodeptr);

#endif // STZFS_INODEPTR_H
