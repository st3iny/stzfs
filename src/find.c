#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "block.h"
#include "blocks.h"
#include "find.h"
#include "helpers.h"
#include "inode.h"
#include "super_block_cache.h"

// find the inode linked to given path
int find_file_inode(const char* file_path, int64_t* inodeptr, inode_t* inode,
                    int64_t* parent_inodeptr, inode_t* parent_inode, char* last_name) {
    const super_block* sb = super_block_cache;

    // start at root inode
    *inodeptr = 1;
    inode_read(*inodeptr, inode);

    if (strcmp(file_path, "/") == 0 ) {
        if (parent_inodeptr) *parent_inodeptr = 0;
        if (parent_inode) memset(parent_inode, 0, sizeof(inode_t));
        if (last_name) strcpy(last_name, "/");
        return 0;
    }

    bool not_existing = false;
    char full_name[2048];
    strcpy(full_name, file_path);
    char* name = strtok(full_name, "/");
    do {
        // printf("Searching for %s\n", name);

        if (not_existing) {
            // there is a non existing directory in path
            printf("find_file_inode: no such file or directory\n");
            return -ENOENT;
        } else if (!M_IS_DIR(inode->mode)) {
            // a file is being treated like a directory
            printf("find_file_inode: expected directory, got file in path\n");
            return -ENOTDIR;
        } else {
            // traverse directory and find name in it
            if (parent_inodeptr) *parent_inodeptr = *inodeptr;
            if (parent_inode) *parent_inode = *inode;
            if (last_name) strcpy(last_name, name);

            int64_t found_inodeptr;
            int err = find_name(name, inode, &found_inodeptr);
            if (err) return err;

            if (found_inodeptr) {
                // go to the next level of path
                *inodeptr = found_inodeptr;
                inode_read(*inodeptr, inode);
            } else {
                // if this is the last level a new file is allowed
                not_existing = true;
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
int find_name(const char* name, inode_t* inode, int64_t* found_inodeptr) {
    if (!M_IS_DIR(inode->mode)) {
        printf("find_name: inode is not a directory\n");
        return -ENOTDIR;
    }

    // read directory blocks and search them
    for (int64_t offset = 0; offset < inode->block_count; offset++) {
        dir_block block;
        int64_t blockptr;
        inode_read_data_block(inode, offset, &block, &blockptr);
        if (blockptr == 0) {
            printf("find_name: can't read directory block\n");
            return -EFAULT;
        }

        const size_t remaining_entries = inode->atom_count - offset * DIR_BLOCK_ENTRIES;
        const size_t entries = MIN(DIR_BLOCK_ENTRIES, remaining_entries);
        for (size_t entry = 0; entry < entries; entry++) {
            if (strcmp((const char*)block.entries[entry].name, name) == 0) {
                // found name in directory
                *found_inodeptr = block.entries[entry].inode;
                return 0;
            }
        }
    }

    // file not found in directory
    // printf("find_name: file not found in directory\n");
    *found_inodeptr = 0;
    return 0;
}

// translate relative inode data block offset to absolute blockptr
int64_t find_inode_data_blockptr(inode_t* inode, int64_t offset, bool alloc_sparse) {
    if (offset > inode->block_count) {
        printf("find_inode_data_blockptr: relative data block offset out of bounds\n");
        return 0;
    } else if (offset >= INODE_MAX_BLOCKS) {
        printf("find_inode_data_blockptr: relative data block offset out of absolute bounds\n");
        return 0;
    }

    typedef struct level {
        int64_t blockptr;
        indirect_block block;
    } level;

    level level1, level2, level3;
    level* last_level = NULL;

    blockptr_t* absolute_blockptr;
    if (offset < INODE_DIRECT_BLOCKS) {
        absolute_blockptr = &inode->data_direct[offset];
    } else if ((offset -= INODE_DIRECT_BLOCKS) < INODE_SINGLE_INDIRECT_BLOCKS) {
        level1.blockptr = inode->data_single_indirect;
        block_read(level1.blockptr, &level1.block);
        absolute_blockptr = &level1.block.blocks[offset];

        if (alloc_sparse) last_level = &level1;
    } else if ((offset -= INODE_SINGLE_INDIRECT_BLOCKS) < INODE_DOUBLE_INDIRECT_BLOCKS) {
        level1.blockptr = inode->data_double_indirect;
        block_read(level1.blockptr, &level1.block);

        level2.blockptr = level1.block.blocks[offset / INODE_SINGLE_INDIRECT_BLOCKS];
        block_read(level2.blockptr, &level2.block);
        absolute_blockptr = &level2.block.blocks[offset % INDIRECT_BLOCK_ENTRIES];

        if (alloc_sparse) last_level = &level2;
    } else if ((offset -= INODE_DOUBLE_INDIRECT_BLOCKS) < INODE_TRIPLE_INDIRECT_BLOCKS) {
        level1.blockptr = inode->data_triple_indirect;
        block_read(level1.blockptr, &level1.block);

        level2.blockptr = level1.block.blocks[offset / INODE_DOUBLE_INDIRECT_BLOCKS];
        block_read(level2.blockptr, &level2.block);

        level3.blockptr = level2.block.blocks[(offset % INODE_DOUBLE_INDIRECT_BLOCKS) / INODE_SINGLE_INDIRECT_BLOCKS];
        block_read(level3.blockptr, &level3.block);
        absolute_blockptr = &level3.block.blocks[offset % INDIRECT_BLOCK_ENTRIES];

        if (alloc_sparse) last_level = &level3;
    } else {
        printf("find_inode_data_blockptr: relative block offset out of bounds\n");
        return 0;
    }

    if (alloc_sparse && *absolute_blockptr == NULL_BLOCKPTR) {
        int64_t new_blockptr;
        block_allocptr(&new_blockptr);
        *absolute_blockptr = new_blockptr;

        if (last_level != NULL) {
            block_write(last_level->blockptr, &last_level->block);
        }
    }

    return *absolute_blockptr;
}

// find inode blockptrs and store them in the given buffer
int find_inode_data_blockptrs(inode_t* inode, int64_t* blockptrs, int64_t length,
                              int64_t offset) {
    if (offset + length > inode->block_count) {
        printf("find_inode_data_blockptrs: relative data block offset out of range\n");
        return -EFAULT;
    }

    const int size = sizeof(int64_t);
    long long blocks_left = length;

    for (int64_t i = offset; i < offset + length; i++) {
        const int64_t blockptr = find_inode_data_blockptr(inode, i, ALLOC_SPARSE_NO);
        if (blockptr == 0) {
            printf("find_inode_data_blockptrs: invalid blockptr returned\n");
            return -EFAULT;
        }
        blockptrs[i] = blockptr;
    }

    return 0;
}
