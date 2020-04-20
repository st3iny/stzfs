#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "bitmap_cache.h"
#include "block.h"
#include "blocks.h"
#include "free.h"
#include "helpers.h"
#include "inode.h"
#include "super_block_cache.h"
#include "write.h"

// free entry in bitmap
int free_bitmap(bitmap_cache_t* cache, objptr_t index) {
    if (index >= cache->length * 8) {
        printf("free_bitmap: bitmap index out of bounds\n");
        return -EINVAL;
    }

    const size_t entry_offset = index / (sizeof(bitmap_entry_t) * 8);
    const size_t inner_offset = index % (sizeof(bitmap_entry_t) * 8);
    bitmap_entry_t* entry = (bitmap_entry_t*)(cache->bitmap + entry_offset);
    *entry ^= (bitmap_entry_t)1 << inner_offset;

    if (entry_offset < cache->next) {
        cache->next = entry_offset;
    }

    return 0;
}

// free entry from directory
int free_dir_entry(inode_t* inode, const char* name) {
    if (!M_IS_DIR(inode->mode)) {
        printf("free_dir_entry: not a directory\n");
        return -ENOTDIR;
    } else if (strcmp(name, ".") == 0) {
        printf("free_dir_entry: can't free protected entry .\n");
        return -EPERM;
    } else if (strcmp(name, "..") == 0) {
        printf("free_dir_entry: can't free protected entry ..\n");
        return -EPERM;
    }

    // search name in directory
    int entry_found = 0;
    size_t free_entry;
    blockptr_t free_entry_offset;
    dir_block free_entry_block;
    for (blockptr_t offset = 0; offset < inode->block_count && !entry_found; offset++) {
        blockptr_t blockptr;
        inode_read_data_block(inode, offset, &free_entry_block, &blockptr);
        if (blockptr == 0) {
            printf("free_dir_entry: can't read directory block\n");
            return -EFAULT;
        }

        // search current directory block
        const size_t remaining_entries = inode->atom_count - offset * DIR_BLOCK_ENTRIES;
        const size_t entries = MIN(DIR_BLOCK_ENTRIES, remaining_entries);
        for (size_t entry = 0; entry < entries; entry++) {
            if (strcmp((const char*)free_entry_block.entries[entry].name, name) == 0) {
                free_entry = entry;
                free_entry_offset = offset;
                entry_found = 1;
                break;
            }
        }
    }

    if (!entry_found) {
        printf("free_dir_entry: name does not exist in directory\n");
        return -ENOENT;
    }

    // get last entry offsets
    const size_t last_entry = (inode->atom_count - 1) % DIR_BLOCK_ENTRIES;
    const blockptr_t last_entry_offset = inode->block_count - 1;

    // copy last entry to removed entry if they are not the same
    if (free_entry != last_entry || free_entry_offset != last_entry_offset) {
        dir_block_entry entry;
        if (free_entry_offset == last_entry_offset) {
            entry = free_entry_block.entries[last_entry];
        } else {
            dir_block last_block;
            blockptr_t read_directory_blockptr;
            inode_read_data_block(inode, last_entry_offset, &last_block, &read_directory_blockptr);
            if (read_directory_blockptr == 0) {
                printf("free_dir_entry: can't read directory block\n");
                return -EFAULT;
            }
            entry = last_block.entries[last_entry];
        }

        free_entry_block.entries[free_entry] = entry;
        inode_write_data_block(inode, free_entry_offset, &free_entry_block);
    }

    if (last_entry == 0) {
        // the whole last block can be deleted
        inode_free_last_data_block(inode);
    }

    inode->atom_count--;
    return 0;
}
