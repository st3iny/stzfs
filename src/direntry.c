#include "direntry.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "blocks.h"
#include "error.h"
#include "helpers.h"
#include "inode.h"
#include "log.h"
#include "types.h"

// alloc a new entry in a directory inode
stzfs_error_t direntry_alloc(inode_t* inode, const char* name, int64_t target_inodeptr) {
    if (strlen(name) > MAX_FILENAME_LENGTH) {
        LOG("filename too long");
        return ERROR;
    } else if (inode->link_count >= DIRECTORY_MAX_LINK_COUNT - 1) {
        // link count will be increased by one if a directory is inserted
        // FIXME: implement this in a better way
        LOG("max link count reached");
        return ERROR;
    } else if (!M_IS_DIR(inode->mode)) {
        LOG("not a directory");
        return ERROR;
    }

    dir_block block;
    dir_block_entry* entry;

    // get next free dir block
    const int64_t block_offset = inode->atom_count / DIR_BLOCK_ENTRIES;
    const int next_free_entry = inode->atom_count % DIR_BLOCK_ENTRIES;

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
    if (next_free_entry == 0) {
        inode_alloc_data_block(inode, &block);
    } else {
        inode_write_data_block(inode, block_offset, &block);
    }

    return SUCCESS;
}

// free entry from directory
stzfs_error_t direntry_free(inode_t* inode, const char* name) {
    if (!M_IS_DIR(inode->mode)) {
        LOG("not a directory");
        return ERROR;
    } else if (strcmp(name, ".") == 0) {
        LOG("can't free protected entry .");
        return ERROR;
    } else if (strcmp(name, "..") == 0) {
        LOG("can't free protected entry ..");
        return ERROR;
    }

    // search name in directory
    bool entry_found = false;
    size_t free_entry;
    int64_t free_entry_offset;
    dir_block free_entry_block;
    for (int64_t offset = 0; offset < inode->block_count && !entry_found; offset++) {
        inode_read_data_block(inode, offset, &free_entry_block, NULL);

        // search current directory block
        const size_t remaining_entries = inode->atom_count - offset * DIR_BLOCK_ENTRIES;
        const size_t entries = MIN(DIR_BLOCK_ENTRIES, remaining_entries);
        for (size_t entry = 0; entry < entries; entry++) {
            if (strcmp((const char*)free_entry_block.entries[entry].name, name) == 0) {
                free_entry = entry;
                free_entry_offset = offset;
                entry_found = true;
                break;
            }
        }
    }

    if (!entry_found) {
        LOG("name does not exist in directory");
        return ERROR;
    }

    // get last entry offsets
    const size_t last_entry = (inode->atom_count - 1) % DIR_BLOCK_ENTRIES;
    const int64_t last_entry_offset = inode->block_count - 1;

    // copy last entry to removed entry if they are not the same
    if (free_entry != last_entry || free_entry_offset != last_entry_offset) {
        dir_block_entry entry;
        if (free_entry_offset == last_entry_offset) {
            entry = free_entry_block.entries[last_entry];
        } else {
            dir_block last_block;
            inode_read_data_block(inode, last_entry_offset, &last_block, NULL);
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
    return SUCCESS;
}

// replace inodeptr of name in directory
// TODO: create a direntry search method to remove code duplication (also see direntry_free)
stzfs_error_t direntry_write(inode_t* inode, const char* name, int64_t target_inodeptr) {
    if (!M_IS_DIR(inode->mode)) {
        LOG("not a directory");
        return ERROR;
    }

    // search and replace name in directory
    for (int64_t offset = 0; offset < inode->block_count; offset++) {
        dir_block block;
        inode_read_data_block(inode, offset, &block, NULL);

        const size_t remaining_entries = inode->atom_count - offset * DIR_BLOCK_ENTRIES;
        const size_t entries = MIN(DIR_BLOCK_ENTRIES, remaining_entries);
        for (size_t entry = 0; entry < entries; entry++) {
            if (strcmp((const char*)block.entries[entry].name, name) == 0) {
                block.entries[entry].inode = target_inodeptr;
                inode_write_data_block(inode, offset, &block);
                return SUCCESS;
            }
        }
    }

    LOG("name does not exist in directory");
    return ERROR;
}
