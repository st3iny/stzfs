#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blocks.h"
#include "find.h"
#include "helpers.h"
#include "read.h"

// find the inode linked to given path
int find_file_inode(const char* file_path, inodeptr_t* inodeptr, inode_t* inode,
                    inodeptr_t* parent_inodeptr, inode_t* parent_inode, char* last_name) {
    super_block sb;
    read_block(0, &sb);

    // start at root inode
    *inodeptr = 1;
    read_inode(*inodeptr, inode);

    if (strcmp(file_path, "/") == 0 ) {
        if (parent_inodeptr) *parent_inodeptr = 0;
        if (parent_inode) memset(parent_inode, 0, sizeof(inode_t));
        if (last_name) last_name = strdup("/");
        return 0;
    }

    int not_existing = 0;
    char full_name[2048];
    strcpy(full_name, file_path);
    char* name = strtok(full_name, "/");
    do {
        // printf("Searching for %s\n", name);

        if (not_existing) {
            // there is a non existing directory in path
            printf("find_file_inode: no such file or directory\n");
            return -ENOENT;
        } else if ((inode->mode & M_DIR) == 0) {
            // a file is being treated like a directory
            printf("find_file_inode: expected directory, got file in path\n");
            return -ENOTDIR;
        } else {
            // traverse directory and find name in it
            if (parent_inodeptr) *parent_inodeptr = *inodeptr;
            if (parent_inode) *parent_inode = *inode;
            if (last_name) strcpy(last_name, name);

            inodeptr_t found_inodeptr;
            int err = find_name(name, inode, &found_inodeptr);
            if (err) return err;

            if (found_inodeptr) {
                // go to the next level of path
                *inodeptr = found_inodeptr;
                read_inode(*inodeptr, inode);
            } else {
                // if this is the last level a new file is allowed
                not_existing = 1;
            }
        }
    } while((name = strtok(NULL, "/")) != NULL);

    if (not_existing) {
        *inodeptr = 0;
        memset(inode, 0, sizeof(inode_t));
    }

    return 0;
}

// find file inode and store data in file struct
int find_file_inode2(const char* file_path, file* f, file* parent, char* last_name) {
    if (parent) {
        return find_file_inode(file_path, &f->inodeptr, &f->inode, &parent->inodeptr,
                               &parent->inode, last_name);
    } else {
        return find_file_inode(file_path, &f->inodeptr, &f->inode, NULL, NULL, last_name);
    }
}

// find name in directory inode
int find_name(const char* name, const inode_t* inode, inodeptr_t* found_inodeptr) {
    if (inode->mode & M_DIR == 0) {
        printf("find_name: inode is not a directory\n");
        return -ENOTDIR;
    }

    // read directory blocks and search them
    uint64_t absolute_entry = 0;
    dir_block dir_blocks[inode->block_count];
    read_inode_data_blocks(inode, &dir_blocks);
    for (int dir_block_index = 0; dir_block_index < inode->block_count; dir_block_index++) {
        const dir_block* cur_dir_block = &dir_blocks[dir_block_index];
        for (int entry = 0; entry < DIR_BLOCK_ENTRIES && absolute_entry < inode->atom_count; entry++) {
            if (strcmp((const char*)&cur_dir_block->entries[entry].name, name) == 0) {
                // found name in directory
                *found_inodeptr = cur_dir_block->entries[entry].inode;
                return 0;
            }
            absolute_entry++;
        }
    }

    // file not found in directory
    *found_inodeptr = 0;
    return 0;
}

// translate relative inode data block offset to absolute blockptr
blockptr_t find_inode_data_blockptr(const inode_t* inode, blockptr_t offset) {
    if (offset >= inode->block_count) {
        printf("find_inode_data_blockptr: relative data block offset out of bounds\n");
        return 0;
    } else if (offset >= INODE_MAX_BLOCKS) {
        printf("find_inode_data_blockptr: inode has too many data blocks\n");
        return 0;
    }

    blockptr_t absolute_blockptr;
    if (offset < INODE_DIRECT_BLOCKS) {
        absolute_blockptr = inode->data_direct[offset];
    } else if ((offset -= INODE_DIRECT_BLOCKS) < INODE_SINGLE_INDIRECT_BLOCKS) {
        indirect_block ind_block;
        read_block(inode->data_single_indirect, &ind_block);
        absolute_blockptr = ind_block.blocks[offset];
    } else if ((offset -= INODE_SINGLE_INDIRECT_BLOCKS) < INODE_DOUBLE_INDIRECT_BLOCKS) {
        indirect_block level1;
        read_block(inode->data_double_indirect, &level1);
        blockptr_t level2_blockptr= level1.blocks[offset / INODE_SINGLE_INDIRECT_BLOCKS];

        indirect_block level2;
        read_block(level2_blockptr, &level2);
        absolute_blockptr = level2.blocks[offset % INODE_SINGLE_INDIRECT_BLOCKS];
    } else if ((offset -= INODE_DOUBLE_INDIRECT_BLOCKS) < INODE_TRIPLE_INDIRECT_BLOCKS) {
        indirect_block level1;
        read_block(inode->data_triple_indirect, &level1);
        blockptr_t level2_blockptr = level1.blocks[offset / INODE_DOUBLE_INDIRECT_BLOCKS];

        indirect_block level2;
        read_block(level2_blockptr, &level2);
        blockptr_t level3_blockptr = level2.blocks[offset % INODE_DOUBLE_INDIRECT_BLOCKS];

        indirect_block level3;
        read_block(level3_blockptr, &level3);
        absolute_blockptr = level3.blocks[offset % INODE_SINGLE_INDIRECT_BLOCKS];
    }

    return absolute_blockptr;
}

// find all inode blockptrs and store them in the given array
int find_inode_data_blockptrs(const inode_t* inode, blockptr_t* blockptrs) {
    const int size = sizeof(blockptr_t);
    long long blocks_left = inode->block_count;
    blockptr_t offset = 0;

    typedef struct level {
        blockptr_t blockptr;
        indirect_block block;
    } level;

    if (blocks_left <= 0) return 0;

    // direct
    memcpy_min(blockptrs, inode->data_direct, size, blocks_left, INODE_DIRECT_BLOCKS);
    blocks_left -= INODE_DIRECT_BLOCKS;
    offset += INODE_DIRECT_BLOCKS;
    if (blocks_left <= 0) return 0;

    // single indirect
    indirect_block level1;
    read_block(inode->data_single_indirect, &level1);
    memcpy_min(&blockptrs[offset], &level1, size, blocks_left, INDIRECT_BLOCK_ENTRIES);
    blocks_left -= INODE_SINGLE_INDIRECT_BLOCKS;
    offset += INODE_SINGLE_INDIRECT_BLOCKS;
    if (blocks_left <= 0) return 0;

    // double indirect
    read_block(inode->data_double_indirect, &level1);

    for (blockptr_t level1_offset = 0; level1_offset < INDIRECT_BLOCK_ENTRIES; level1_offset++) {
        level level2 = {.blockptr = level1.blocks[level1_offset]};
        read_block(level2.blockptr, &level2.block);
        memcpy_min(&blockptrs[offset], &level2.block, size, blocks_left, INDIRECT_BLOCK_ENTRIES);
        blocks_left -= INDIRECT_BLOCK_ENTRIES;
        offset += INDIRECT_BLOCK_ENTRIES;
        if (blocks_left <= 0) return 0;
    }

    // triple indirect
    read_block(inode->data_triple_indirect, &level1);

    for (blockptr_t level1_offset = 0; level1_offset < INDIRECT_BLOCK_ENTRIES; level1_offset++) {
        level level2 = {.blockptr = level1.blocks[level1_offset]};
        read_block(level2.blockptr, &level2.block);

        blockptr_t level2_offset;
        for (level2_offset = 0; level2_offset < INDIRECT_BLOCK_ENTRIES; level2_offset++) {
            level level3 = {.blockptr = level2.block.blocks[level2_offset]};
            read_block(level3.blockptr, &level3.block);
            memcpy_min(&blockptrs[offset], &level3.block, size, blocks_left,
                       INDIRECT_BLOCK_ENTRIES);
            blocks_left -= INDIRECT_BLOCK_ENTRIES;
            offset += INDIRECT_BLOCK_ENTRIES;
            if (blocks_left <= 0) return 0;
        }
    }

    return 0;
}
