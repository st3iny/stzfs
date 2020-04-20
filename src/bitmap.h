#ifndef STZFS_BITMAP_H
#define STZFS_BITMAP_H

#include <stdint.h>

#include "bitmap_cache.h"
#include "error.h"

stzfs_error_t bitmap_alloc(int64_t* ptr, bitmap_cache_t* cache);
stzfs_error_t bitmap_free(bitmap_cache_t* cache, int64_t ptr);

#endif // STZFS_BITMAP_H
