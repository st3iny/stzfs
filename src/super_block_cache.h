#ifndef FILESYSTEM_SUPER_BLOCK_CACHE_H
#define FILESYSTEM_SUPER_BLOCK_CACHE_H

#include "blocks.h"

extern super_block* super_block_cache;

int super_block_cache_init(void);
int super_block_cache_dispose(void);
int super_block_cache_sync(void);

#endif // FILESYSTEM_SUPER_BLOCK_CACHE_H
