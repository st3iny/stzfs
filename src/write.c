#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "alloc.h"
#include "find.h"
#include "helpers.h"
#include "inode.h"
#include "disk.h"
#include "super_block_cache.h"
#include "write.h"

// replace inodeptr of name in directory
int write_dir_entry(inode_t* inode, const char* name, inodeptr_t target_inodeptr) {
    if (!M_IS_DIR(inode->mode)) {
        printf("write_dir_entry: not a directory\n");
        return -ENOTDIR;
    }

    // search and replace name in directory
    for (blockptr_t offset = 0; offset < inode->block_count; offset++) {
        dir_block block;
        blockptr_t blockptr;
        inode_read_data_block(inode, offset, &block, &blockptr);
        if (blockptr == 0) {
            printf("write_dir_entry: can't read directory block\n");
            return -EFAULT;
        }

        const size_t remaining_entries = inode->atom_count - offset * DIR_BLOCK_ENTRIES;
        const size_t entries = MIN(DIR_BLOCK_ENTRIES, remaining_entries);
        for (size_t entry = 0; entry < entries; entry++) {
            if (strcmp((const char*)block.entries[entry].name, name) == 0) {
                block.entries[entry].inode = target_inodeptr;
                inode_write_data_block(inode, offset, &block);
                return 0;
            }
        }
    }

    printf("write_dir_entry: name does not exist in directory\n");
    return -ENOENT;
}
