#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "alloc.h"
#include "block.h"
#include "inode.h"
#include "bitmap_cache.h"
#include "blocks.h"
#include "super_block_cache.h"
#include "write.h"

// alloc an entry in a directory inode
int alloc_dir_entry(inode_t* inode, const char* name, inodeptr_t target_inodeptr) {
    // set max_link_count to 0xffff - 1 to prevent a deadlock
    if (strlen(name) > MAX_FILENAME_LENGTH) {
        printf("alloc_dir_entry: filename too long\n");
        return -ENAMETOOLONG;
    } else if (inode->link_count >= 0xfffe) {
        printf("alloc_dir_entry: max link count reached\n");
        return -EMLINK;
    } else if (!M_IS_DIR(inode->mode)) {
        printf("alloc_dir_entry: not a directory\n");
        return -ENOTDIR;
    }

    dir_block block;
    dir_block_entry* entry;

    // get next free dir block
    blockptr_t block_offset = inode->atom_count / DIR_BLOCK_ENTRIES;
    int next_free_entry = inode->atom_count % DIR_BLOCK_ENTRIES;

    if (next_free_entry == 0) {
        // allocate a new dir block
        memset(&block, 0, STZFS_BLOCK_SIZE);
    } else {
        inode_read_data_block(inode, block_offset, &block, NULL);
    }

    // create entry
    entry = &block.entries[next_free_entry];
    entry->inode = target_inodeptr;
    strcpy(entry->name, name);
    inode->atom_count++;

    // write back dir block
    if (next_free_entry > 0) {
        inode_write_data_block(inode, block_offset, &block);
    } else {
        inode_alloc_data_block(inode, &block);
    }
    return 0;
}
