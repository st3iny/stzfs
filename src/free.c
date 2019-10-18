#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "blocks.h"
#include "free.h"
#include "helpers.h"
#include "read.h"
#include "write.h"

// free blocks in bitmap
// TODO: improve me
int free_blocks(const blockptr_t* blockptrs, size_t length) {
    super_block sb;
    read_block(0, &sb);

    for (size_t offset = 0; offset < length; offset++) {
        int err = free_bitmap(blockptrs[offset], sb.block_bitmap, sb.block_bitmap_length);
        if (err) return err;
    }

    // update superblock
    sb.free_blocks += length;
    write_block(0, &sb);

    return 0;
}

// free inode and allocated data blocks
int free_inode(inodeptr_t inodeptr, inode_t* inode) {
    if (inodeptr <= 1) {
        printf("free_inode: trying to free protected inode\n");
        return -EFAULT;
    } else if ((inode->mode & M_DIR) && inode->atom_count > 2) {
        printf("free_inode: directory is not empty\n");
        return -ENOTEMPTY;
    } else if ((inode->mode & M_DIR) && inode->link_count > 1) {
        printf("free_inode: directory inode link count too high\n");
        return -EPERM;
    } else if ((inode->mode & M_DIR) == 0 && inode->link_count > 0) {
        printf("free_inode: file inode link count too high\n");
        return -EPERM;
    }

    super_block sb;
    read_block(0, &sb);

    // dealloc inode first
    free_bitmap(inodeptr, sb.inode_bitmap, sb.inode_bitmap_length);

    // free allocated data blocks in bitmap
    free_inode_data_blocks(inode, 0);

    return 0;
}

// truncate inode data blocks to given offset
int free_inode_data_blocks(inode_t* inode, blockptr_t offset) {
    // FIXME: implement proper algorithm (merge with free_last_inode_data_block)
    for (blockptr_t block_count = inode->block_count; block_count > offset; block_count--) {
        free_last_inode_data_block(inode);
    }

    return 0;
}

// free the last data block of an inode
int free_last_inode_data_block(inode_t* inode) {
    if (inode->block_count <= 0) {
        return 0;
    }

    typedef struct level {
        blockptr_t blockptr;
        indirect_block block;
    } level;

    inode->block_count--;
    blockptr_t offset = inode->block_count;
    blockptr_t absolute_blockptr = 0;
    if (offset < INODE_DIRECT_BLOCKS) {
        absolute_blockptr = inode->data_direct[offset];

        // FIXME: just for debugging
        inode->data_direct[offset] = 0;
    } else if ((offset -= INODE_DIRECT_BLOCKS) < INODE_SINGLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = inode->data_single_indirect};
        read_block(level1.blockptr, &level1.block);
        absolute_blockptr = level1.block.blocks[offset];

        if (offset == 0) {
            free_blocks(&level1.blockptr, 1);

            // FIXME: just for debugging
            inode->data_single_indirect = 0;
        }
    } else if ((offset -= INODE_SINGLE_INDIRECT_BLOCKS) < INODE_DOUBLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = inode->data_double_indirect};
        read_block(level1.blockptr, &level1.block);

        level level2 = {.blockptr = level1.block.blocks[offset / INODE_SINGLE_INDIRECT_BLOCKS]};
        read_block(level2.blockptr, &level2.block);
        absolute_blockptr = level2.block.blocks[offset % INODE_SINGLE_INDIRECT_BLOCKS];

        if (offset == 0) {
            free_blocks(&level1.blockptr, 1);

            // FIXME: just for debugging
            inode->data_double_indirect = 0;
        }
        if (offset % INODE_SINGLE_INDIRECT_BLOCKS == 0) free_blocks(&level2.blockptr, 1);
    } else if ((offset -= INODE_DOUBLE_INDIRECT_BLOCKS) < INODE_TRIPLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = inode->data_triple_indirect};
        read_block(level1.blockptr, &level1.block);

        level level2 = {.blockptr = level1.block.blocks[offset / INODE_DOUBLE_INDIRECT_BLOCKS]};
        read_block(level2.blockptr, &level2.block);

        level level3 = {.blockptr = level2.block.blocks[offset % INODE_DOUBLE_INDIRECT_BLOCKS]};
        read_block(level3.blockptr, &level3.block);
        absolute_blockptr = level3.block.blocks[offset % INODE_SINGLE_INDIRECT_BLOCKS];

        if (offset == 0) {
            free_blocks(&level1.blockptr, 1);

            // FIXME: just for debugging
            inode->data_triple_indirect = 0;
        }
        if (offset % INODE_DOUBLE_INDIRECT_BLOCKS == 0) free_blocks(&level2.blockptr, 1);
        if (offset % INODE_SINGLE_INDIRECT_BLOCKS == 0) free_blocks(&level3.blockptr, 1);
    } else {
        printf("free_last_inode_data_block: relative data offset out of bounds\n");
    }

    if (absolute_blockptr) free_blocks(&absolute_blockptr, 1);
    return 0;
}

// free entry in bitmap
int free_bitmap(blockptr_t index, blockptr_t bitmap_offset, blockptr_t bitmap_length) {
    blockptr_t offset = index / (BLOCK_SIZE * 8);
    if (offset > bitmap_length) {
        printf("free_bitmap: bitmap index out of bounds\n");
        return -EFAULT;
    }

    size_t inner_offset = index % 64;
    size_t entry = (index % (BLOCK_SIZE * 8)) / 64;

    blockptr_t blockptr = bitmap_offset + offset;
    bitmap_block ba;
    read_block(blockptr, &ba);
    ba.bitmap[entry] ^= 1UL << inner_offset;
    write_block(blockptr, &ba);

    return 0;
}

// free entry from directory
int free_dir_entry(inode_t* inode, const char* name) {
    if ((inode->mode & M_DIR) == 0) {
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
        const blockptr_t blockptr = read_inode_data_block(inode, offset, &free_entry_block);
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
            if (read_inode_data_block(inode, last_entry_offset, &last_block) == 0) {
                printf("free_dir_entry: can't read directory block\n");
                return -EFAULT;
            }
            entry = last_block.entries[last_entry];
        }

        free_entry_block.entries[free_entry] = entry;
        write_inode_data_block(inode, free_entry_offset, &free_entry_block);
    }

    if (last_entry == 0) {
        // the whole last block can be deleted
        free_last_inode_data_block(inode);
    }

    inode->atom_count--;
    return 0;
}
