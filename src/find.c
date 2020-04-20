#include "find.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "direntry.h"
#include "block.h"
#include "blocks.h"
#include "helpers.h"
#include "inode.h"
#include "inodeptr.h"
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
            direntry_find(inode, name, &found_inodeptr);

            if (inodeptr_is_valid(found_inodeptr)) {
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
