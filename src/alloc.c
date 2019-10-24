#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "alloc.h"
#include "bitmap_cache.h"
#include "blocks.h"
#include "read.h"
#include "vm.h"
#include "write.h"

// alloc entry in bitmap
blockptr_t alloc_bitmap(bitmap_cache_t* cache) {
    blockptr_t next_free = 0;
    for (size_t i = cache->next; i < cache->length && !next_free; i += sizeof(bitmap_entry_t)) {
        bitmap_entry_t* entry = (bitmap_entry_t*)(cache->bitmap + i);
        if (~*entry == 0) {
            continue;
        }

        cache->next = i;

        // there is at least one free alloc available
        bitmap_entry_t data = *entry;
        int offset = 0;
        while (offset < sizeof(bitmap_entry_t) * 8 && (data & 1) != 0) {
            data >>= 1;
            offset++;
        }

        // mark alloc in bitmap
        bitmap_entry_t new_entry = 1;
        *entry |= new_entry << offset;
        next_free = i * 8 + offset;
    }

    if (!next_free) {
        printf("alloc_entry: could not allocate entry in bitmap\n");
    }
    return next_free;
}

// allocates and writes a new block in place
int alloc_block(blockptr_t* blockptr, const void* block) {
    super_block sb;
    read_super_block(&sb);

    // get next free block and write data block
    const blockptr_t free_block = alloc_bitmap(&block_bitmap_cache);
    if (free_block == 0) {
        printf("alloc_block: could not allocate block\n");
        return -ENOSPC;
    }
    write_block(free_block, block);

    // update superblock
    sb.free_blocks--;
    write_super_block(&sb);

    *blockptr = free_block;
    return 0;
}

// allocate a new inodeptr only
// TODO: check free inodes (implement free inode counter in superblock)
inodeptr_t alloc_inodeptr(void) {
    super_block sb;
    read_super_block(&sb);

    const inodeptr_t next_free_inode = alloc_bitmap(&inode_bitmap_cache);

    if (!next_free_inode) {
        printf("alloc_inodeptr: could not allocate inodeptr\n");
    }
    return next_free_inode;
}

// allocate and write a new inode in place
inodeptr_t alloc_inode(const inode_t* new_inode) {
    super_block sb;
    read_super_block(&sb);

    // get next free inode
    const inodeptr_t next_free_inode = alloc_inodeptr();
    if (!next_free_inode) {
        printf("alloc_inode: could not allocate inode\n");
        return 0;
    }

    // get inode table block
    blockptr_t inode_table_blockptr = sb.inode_table + next_free_inode / (STZFS_BLOCK_SIZE / sizeof(inode_t));
    inode_block inode_table_block;
    vm_read(inode_table_blockptr * STZFS_BLOCK_SIZE, &inode_table_block, STZFS_BLOCK_SIZE);

    // place inode into table and write table block
    inode_table_block.inodes[next_free_inode % (STZFS_BLOCK_SIZE / sizeof(inode_t))] = *new_inode;
    vm_write(inode_table_blockptr * STZFS_BLOCK_SIZE, &inode_table_block, STZFS_BLOCK_SIZE);

    return next_free_inode;
}

// alloc a new data block for an inode
blockptr_t alloc_inode_data_block(inode_t* inode, const void* block) {
    if (inode->block_count >= INODE_MAX_BLOCKS) {
        printf("alloc_inode_data_block: inode has reached max block count\n");
        return 0;
    }

    // level struct for indirection convenience
    typedef struct level {
        blockptr_t* blockptr;
        indirect_block block;
        bool changed;
    } level;

    // alloc data block
    blockptr_t blockptr;
    alloc_block(&blockptr, block);
    inode->block_count++;

    // handle indirection
    blockptr_t offset = inode->block_count - 1;
    if (offset < INODE_DIRECT_BLOCKS) {
        inode->data_direct[offset] = blockptr;
    } else if ((offset -= INODE_DIRECT_BLOCKS) < INODE_SINGLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = &inode->data_single_indirect};
        if (offset == 0) {
            alloc_block(level1.blockptr, &level1.block);
        } else {
            read_block(*level1.blockptr, &level1.block);
        }

        level1.block.blocks[offset] = blockptr;

        write_block(*level1.blockptr, &level1.block);
    } else if ((offset -= INODE_SINGLE_INDIRECT_BLOCKS) < INODE_DOUBLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = &inode->data_double_indirect};
        if (offset == 0) {
            alloc_block(level1.blockptr, &level1.block);
        } else {
            read_block(*level1.blockptr, &level1.block);
        }

        level level2 = {.blockptr = &level1.block.blocks[offset / INODE_SINGLE_INDIRECT_BLOCKS]};
        if (offset % INODE_SINGLE_INDIRECT_BLOCKS == 0) {
            level1.changed = true;
            alloc_block(level2.blockptr, &level2.block);
        } else {
            read_block(*level2.blockptr, &level2.block);
        }

        level2.block.blocks[offset % INDIRECT_BLOCK_ENTRIES] = blockptr;

        if (level1.changed) write_block(*level1.blockptr, &level1.block);
        write_block(*level2.blockptr, &level2.block);
    } else if ((offset -= INODE_DOUBLE_INDIRECT_BLOCKS) < INODE_TRIPLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = &inode->data_triple_indirect};
        if (offset == 0) {
            alloc_block(level1.blockptr, &level1.block);
        } else {
            read_block(*level1.blockptr, &level1.block);
        }

        level level2 = {.blockptr = &level1.block.blocks[offset / INODE_DOUBLE_INDIRECT_BLOCKS]};
        if (offset % INODE_DOUBLE_INDIRECT_BLOCKS == 0) {
            level1.changed = true;
            alloc_block(level2.blockptr, &level2.block);
        } else {
            read_block(*level2.blockptr, &level2.block);
        }

        level level3 = {.blockptr = &level2.block.blocks[(offset % INODE_DOUBLE_INDIRECT_BLOCKS) / INODE_SINGLE_INDIRECT_BLOCKS]};
        if (offset % INODE_SINGLE_INDIRECT_BLOCKS == 0) {
            level2.changed = true;
            alloc_block(level3.blockptr, &level3.block);
        } else {
            read_block(*level3.blockptr, &level3.block);
        }

        level3.block.blocks[offset % INDIRECT_BLOCK_ENTRIES] = blockptr;

        if (level1.changed) write_block(*level1.blockptr, &level1.block);
        if (level2.changed) write_block(*level2.blockptr, &level2.block);
        write_block(*level3.blockptr, &level3.block);
    }

    return blockptr;
}

// alloc an entry in a directory inode
int alloc_dir_entry(inode_t* inode, const char* name, inodeptr_t target_inodeptr) {
    // set max_link_count to 0xffff - 1 to prevent a deadlock
    if (strlen(name) > MAX_FILENAME_LENGTH) {
        printf("alloc_dir_entry: filename too long\n");
        return -ENAMETOOLONG;
    } else if (inode->link_count >= 0xfffe) {
        printf("alloc_dir_entry: max link count reached\n");
        return -EMLINK;
    } else if ((inode->mode & M_DIR) == 0) {
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
        read_inode_data_block(inode, block_offset, &block);
    }

    // create entry
    entry = &block.entries[next_free_entry];
    entry->inode = target_inodeptr;
    strcpy(entry->name, name);
    inode->atom_count++;

    // write back dir block
    if (next_free_entry > 0) {
        write_inode_data_block(inode, block_offset, &block);
    } else {
        alloc_inode_data_block(inode, &block);
    }
    return 0;
}
