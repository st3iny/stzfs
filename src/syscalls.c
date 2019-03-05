#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "blocks.h"
#include "syscalls.h"
#include "vm.h"

// find data in read blocks
int find_file_inode(const char* file_path, inodeptr_t* inodeptr, inode_t* inode,
                    inodeptr_t* parent_inodeptr, inode_t* parent_inode, char* last_name);
int find_name(const char* name, const inode_t* inode, inodeptr_t* found_inodeptr);
blockptr_t find_inode_data_blockptr(const inode_t* inode, blockptr_t offset);

// read from disk
void read_block(blockptr_t blockptr, void* block);
void read_inode(inodeptr_t inodeptr, inode_t* inode);
blockptr_t read_inode_data_block(const inode_t* inode, blockptr_t offset, void* block);
void read_inode_data_blocks(const inode_t* inode, void* data_block_array);

// alloc in bitmap
blockptr_t alloc_bitmap(const char* title, blockptr_t bitmap_offset, blockptr_t bitmap_length);
blockptr_t alloc_block(const void* new_block);
inodeptr_t alloc_inode(const inode_t* new_inode);
blockptr_t alloc_inode_data_block(inode_t* inode, const void* block);
int alloc_dir_entry(inode_t* inode, const char* name, inodeptr_t target_inodeptr);

// write to disk
void write_block(blockptr_t blockptr, const void* block);
void write_inode(inodeptr_t inodeptr, const inode_t* inode);
blockptr_t write_inode_data_block(const inode_t* inode, blockptr_t offset, const void* block);

// misc helpers
uint32_t uint32_div_ceil(uint32_t a, uint32_t b);
int read_or_alloc_block(blockptr_t* blockptr, void* block);

blockptr_t sys_makefs(inodeptr_t inode_count) {
    blockptr_t blocks = vm_size() / BLOCK_SIZE;
    printf("SYS: Creating file system with %i blocks and %i inodes.\n", blocks, inode_count);

    // calculate bitmap and inode table lengths
    blockptr_t block_bitmap_length = uint32_div_ceil(blocks, BLOCK_SIZE * 8);
    inodeptr_t inode_table_length = uint32_div_ceil(inode_count, BLOCK_SIZE / sizeof(inode_t));
    inodeptr_t inode_bitmap_length = uint32_div_ceil(inode_count, BLOCK_SIZE * 8);

    const blockptr_t initial_block_count = 1 + block_bitmap_length + inode_bitmap_length + inode_table_length;

    // create superblock
    super_block sb;
    memset(&sb, 0, BLOCK_SIZE);
    sb.block_count = blocks;
    sb.free_blocks = blocks - initial_block_count;
    sb.block_bitmap = 1;
    sb.block_bitmap_length = block_bitmap_length;
    sb.inode_bitmap = 1 + block_bitmap_length;
    sb.inode_bitmap_length = inode_bitmap_length;
    sb.inode_table = 1 + block_bitmap_length + inode_bitmap_length;
    sb.inode_table_length = inode_table_length;
    sb.inode_count = inode_count;

    // initialize all bitmaps and inode table with zeroes
    data_block initial_block;
    memset(&initial_block, 0, BLOCK_SIZE);
    for (blockptr_t blockptr = 0; blockptr < initial_block_count; blockptr++) {
        vm_write(blockptr * BLOCK_SIZE, &initial_block, BLOCK_SIZE);
    }
    printf("SYS: Wrote %i initial blocks.\n", initial_block_count);

    // write initial block bitmap
    bitmap_block ba;
    blockptr_t initial_bitmap_offset;

    // write full bitmap blocks
    memset(&ba, 0xff, BLOCK_SIZE);
    blockptr_t initial_allocated_bitmap_blocks = initial_block_count / (BLOCK_SIZE * 8);
    for (initial_bitmap_offset = 0; initial_bitmap_offset < initial_allocated_bitmap_blocks; initial_bitmap_offset++) {
        vm_write((sb.block_bitmap + initial_bitmap_offset) * BLOCK_SIZE, &ba, BLOCK_SIZE);
    }

    // write partially filled bitmap block
    unsigned int allocated_entries = (initial_block_count % (BLOCK_SIZE * 8)) / 64;
    memset(&ba, 0, BLOCK_SIZE);
    memset(&ba, 0xff, allocated_entries * 8);
    int shift_partial_entry = (initial_block_count % (BLOCK_SIZE * 8)) % 64;
    ba.bitmap[allocated_entries] = (1UL << shift_partial_entry) - 1UL;
    vm_write((sb.block_bitmap + initial_bitmap_offset) * BLOCK_SIZE, &ba, BLOCK_SIZE);

    // write initial inode bitmap
    bitmap_block first_inode_bitmap_block;
    memset(&first_inode_bitmap_block, 0, BLOCK_SIZE);
    first_inode_bitmap_block.bitmap[0] = 1;
    vm_write(sb.inode_bitmap * BLOCK_SIZE, &first_inode_bitmap_block, BLOCK_SIZE);

    // write superblock
    vm_write(0, &sb, BLOCK_SIZE);

    // write root directory block
    dir_block root_dir_block;
    memset(&root_dir_block, 0, BLOCK_SIZE);
    root_dir_block.entries[0] = (dir_block_entry) {.name = ".", .inode = 1};
    blockptr_t root_dir_block_ptr = alloc_block(&root_dir_block);
    printf("SYS: Wrote root dir block at %i.\n", root_dir_block_ptr);

    // create root inode
    time_t now;
    time(&now);
    inode_t root_inode;
    memset(&root_inode, 0, INODE_SIZE);
    root_inode.mode = M_RU | M_WU | M_XU | M_RG | M_XG | M_RO | M_XO | M_DIR;
    root_inode.uid = 0;
    root_inode.gid = 0;
    root_inode.crtime = now;
    root_inode.mtime = now;
    root_inode.link_count = 1;
    root_inode.atom_count = 1;
    root_inode.block_count = 1;
    root_inode.data_direct[0] = root_dir_block_ptr;

    // write root inode
    inodeptr_t root_inode_ptr = alloc_inode(&root_inode);
    printf("SYS: Wrote root inode with id %i.\n", root_inode_ptr);

    return blocks;
}

// open existing file
int sys_open(const char* file_path, struct fuse_file_info* file_info) {
    printf("FUSE: open %s\n", file_path);

    inodeptr_t inodeptr, parent_inodeptr;
    inode_t inode, parent_inode;
    int err = find_file_inode(file_path, &inodeptr, &inode, &parent_inodeptr, &parent_inode, NULL);
    if (err) return err;

    if (inodeptr == 0) {
        printf("FUSE: no such file\n");
        return ENOENT;
    } else if (inode.mode & M_DIR == 1) {
        printf("FUSE: can't open a directory\n");
        return EISDIR;
    } else {
        // open exsiting file
        printf("FUSE: opened file\n");
        file_info->fh = inodeptr;
        return 0;
    }
}

// close a file
int sys_release(const char* file_path, struct fuse_file_info* file_info) {}

// read from a file
int sys_read(const char* file_path, char* buffer, size_t length, off_t offset,
             struct fuse_file_info* file_info) {
    printf("FUSE: read from file %s\n", file_path);

    if (file_info->fh == 0) {
        printf("FUSE: invald file handle (inode)\n");
        return -EFAULT;
    }

    inodeptr_t inodeptr = file_info->fh;
    inode_t inode;
    read_inode(inodeptr, &inode);

    // check file bounds
    if (offset >= inode.atom_count) {
        printf("FUSE: offset out of bounds\n");
        return -ESPIPE;
    }

    size_t max_bytes = inode.atom_count < length ? inode.atom_count : length;
    size_t read_bytes = 0;
    for (blockptr_t block_offset = 0; block_offset < inode.block_count; block_offset++) {
        data_block block;
        read_inode_data_block(&inode, block_offset, &block);

        size_t diff = max_bytes - read_bytes;
        if (diff <= 0) {
            break;
        } else if (diff > 0 && diff < BLOCK_SIZE) {
            memcpy(&buffer, &block, diff);
            read_bytes += diff;
        } else {
            memcpy(&buffer, &block, BLOCK_SIZE);
            read_bytes += BLOCK_SIZE;
        }
    }

    return read_bytes;
}

// write to a file
int sys_write(const char* file_path, const char* buffer, size_t length, off_t offset,
              struct fuse_file_info* file_info) {
    // printf("FUSE: write to file %s\n", file_path);

    if (file_info->fh == 0) {
        printf("FUSE: invald file handle (inode)\n");
        return -EFAULT;
    }

    inodeptr_t inodeptr = file_info->fh;
    inode_t inode;
    read_inode(inodeptr, &inode);

    // check file size limits
    size_t max_block_offset = uint32_div_ceil(offset + length, BLOCK_SIZE);
    if (max_block_offset >= INODE_MAX_BLOCKS) {
        printf("FUSE: max file size exceeded\n");
        return -EFBIG;
    }

    // check continuity
    if (offset > inode.atom_count) {
        printf("FUSE: offset too high\n");
        return -ESPIPE;
    }

    // write first partial block
    size_t written_bytes = 0;
    size_t initial_offset = offset / BLOCK_SIZE;
    size_t initial_byte_offset = offset % BLOCK_SIZE;
    if (initial_byte_offset > 0) {
        data_block block;
        if (initial_offset >= inode.block_count) {
            memset(&block, 0, BLOCK_SIZE);
        } else {
            read_inode_data_block(&inode, initial_offset, &block);
        }

        // keep block boundaries
        written_bytes = BLOCK_SIZE - initial_byte_offset;
        if (written_bytes > length) {
            written_bytes = length;
        }
        memcpy(&block.data[initial_byte_offset], buffer, written_bytes);

        if (initial_offset >= inode.block_count) {
            alloc_inode_data_block(&inode, &block);
        } else {
            write_inode_data_block(&inode, initial_offset, &block);
        }

        initial_offset++;
    }

    // write aligned data
    for (blockptr_t block_offset = initial_offset; block_offset < max_block_offset; block_offset++) {
        data_block block;
        size_t diff = length - written_bytes;
        if (diff <= 0) {
            break;
        } else if (diff > 0 && diff < BLOCK_SIZE) {
            memcpy(&block, buffer, diff);
            memset(&block.data[diff], 0, BLOCK_SIZE - diff);
            written_bytes += diff;
        } else {
            memcpy(&block, buffer, BLOCK_SIZE);
            written_bytes += BLOCK_SIZE;
        }

        if (block_offset >= inode.block_count) {
            alloc_inode_data_block(&inode, &block);
        } else {
            write_inode_data_block(&inode, block_offset, &block);
        }
    }

    inode.atom_count += written_bytes;
    write_inode(inodeptr, &inode);
    return written_bytes;
}

// create new file and open it
int sys_create(const char* file_path, mode_t mode, struct fuse_file_info* file_info) {
    printf("FUSE: create %s\n", file_path);

    inodeptr_t inodeptr, parent_inodeptr;
    inode_t inode, parent_inode;
    char last_name[2048];
    int err = find_file_inode(file_path, &inodeptr, &inode, &parent_inodeptr, &parent_inode, last_name);
    if (err) return err;

    if (inodeptr != 0) {
        printf("FUSE: file is already existing\n");
        return EEXIST;
    } else {
        // create new file
        printf("FUSE: create file\n");
        time_t now;
        time(&now);
        inode.mode = 0xffff;
        inode.crtime = now;
        inode.mtime = now;
        inode.link_count = 1;
        inodeptr = alloc_inode(&inode);
        alloc_dir_entry(&parent_inode, last_name, inodeptr);
        write_inode(parent_inodeptr, &parent_inode);
        file_info->fh = inodeptr;
        return 0;
    }
}

// find the inode linked to given path
int find_file_inode(const char* file_path, inodeptr_t* inodeptr, inode_t* inode,
                    inodeptr_t* parent_inodeptr, inode_t* parent_inode, char* last_name) {
    super_block sb;
    read_block(0, &sb);

    // start at root inode
    *inodeptr = 1;
    read_inode(*inodeptr, inode);

    int not_existing = 0;
    // char* full_name = (char*)malloc(2048 * sizeof(char));
    char full_name[2048];
    strcpy(full_name, file_path);
    char* name = strtok(full_name, "/");
    do {
        printf("Searching for %s\n", name);

        if (not_existing) {
            // there is a non existing directory in path
            printf("FUSE: no such file or directory\n");
            return ENOENT;
        } else if (inode->mode & M_DIR == 0) {
            // a file is being treated like a directory
            printf("FUSE: expected directory, got file in path\n");
            return ENOTDIR;
        } else {
            // traverse directory and find name in it
            *parent_inodeptr = *inodeptr;
            *parent_inode = *inode;
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

// find name in directory inode
int find_name(const char* name, const inode_t* inode, inodeptr_t* found_inodeptr) {
    if (inode->mode & M_DIR == 0) {
        printf("SYS: inode is not a directory\n");
        return ENOTDIR;
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
        printf("SYS: relative inode data block offset out of bounds\n");
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
    } else {
        printf("SYS: relative data blockptr out of bounds\n");
        absolute_blockptr = 0;
    }

    return absolute_blockptr;
}

// read block from disk
void read_block(blockptr_t blockptr, void* block) {
    vm_read(blockptr * BLOCK_SIZE, block, BLOCK_SIZE);
}

// read inode from disk
void read_inode(inodeptr_t inodeptr, inode_t* inode) {
    super_block sb;
    read_block(0, &sb);

    blockptr_t inode_table_block_offset = inodeptr / (BLOCK_SIZE / sizeof(inode_t));
    if (inode_table_block_offset >= sb.inode_table_length) {
        printf("SYS: Out of bounds while trying to read inode.\n");
        return;
    }

    // get inode table block
    blockptr_t inode_table_blockptr = sb.inode_table + inode_table_block_offset;
    inode_block inode_table_block;
    read_block(inode_table_blockptr, &inode_table_block);

    // read inode from inode table block
    *inode = inode_table_block.inodes[inodeptr % (BLOCK_SIZE / sizeof(inode_t))];
}

// read inode data block with relative offset
blockptr_t read_inode_data_block(const inode_t* inode, blockptr_t offset, void* block) {
    blockptr_t blockptr = find_inode_data_blockptr(inode, offset);
    if (blockptr == 0) {
        printf("SYS: could not read inode data block\n");
        return 0;
    }

    read_block(blockptr, block);
    return blockptr;
}

// read all data blocks of an inode
void read_inode_data_blocks(const inode_t* inode, void* data_block_array) {
    int data_offset = 0;
    data_block* data_blocks = (data_block*)data_block_array;

    // read direct blocks
    for (int i = 0; i < INODE_DIRECT_BLOCKS && data_offset < inode->block_count; i++) {
        read_block(inode->data_direct[i], &data_blocks[data_offset]);
        data_offset++;
    }

    // read single indirect block
    indirect_block indirect;
    read_block(inode->data_single_indirect, &indirect);
    for (int i = 0; i < INDIRECT_BLOCK_ENTRIES && data_offset < inode->block_count; i++) {
        read_block(indirect.blocks[i], &data_blocks[data_offset + i]);
        data_offset++;
    }

    // TODO: read double and triple indirect blocks
    if (data_offset < inode->block_count) {
        printf("SYS: Did not read all inode data blocks.\n");
    }
}

// write block to disk
void write_block(blockptr_t blockptr, const void* block) {
    vm_write(blockptr * BLOCK_SIZE, block, BLOCK_SIZE);
}

// write inode to disk
void write_inode(inodeptr_t inodeptr, const inode_t* inode) {
    if (inodeptr == 0) {
        printf("SYS: can't write inode 0\n");
        return;
    }

    super_block sb;
    read_block(0, &sb);

    blockptr_t table_block_offset = inodeptr / INODE_BLOCK_ENTRIES;
    if (table_block_offset > sb.inode_table_length) {
        printf("SYS: inode index out of bounds\n");
        return;
    }

    // place inode in table and write back table block
    blockptr_t table_blockptr = sb.inode_table + table_block_offset;
    inode_block table_block;
    read_block(table_blockptr, &table_block);
    table_block.inodes[inodeptr % INODE_BLOCK_ENTRIES] = *inode;
    write_block(table_blockptr, &table_block);
}

// read inode data block with relative offset
blockptr_t write_inode_data_block(const inode_t* inode, blockptr_t offset, const void* block) {
    blockptr_t blockptr = find_inode_data_blockptr(inode, offset);
    if (blockptr == 0) {
        printf("SYS: could not write inode data block\n");
        return 0;
    }

    write_block(blockptr, block);
    return blockptr;
}

// alloc inode or block in given bitmap
blockptr_t alloc_bitmap(const char* title, blockptr_t bitmap_offset, blockptr_t bitmap_length) {
    // get next free block
    bitmap_block ba;
    blockptr_t current_bitmap_block = bitmap_offset;
    blockptr_t next_free = 0;
    while (next_free == 0 && current_bitmap_block < bitmap_offset + bitmap_length) {
        vm_read(current_bitmap_block * BLOCK_SIZE, &ba, BLOCK_SIZE);

        for (int i = 0; i < BLOCK_SIZE / 8; i++) {
            uint64_t data = ba.bitmap[i];
            if (data == 0xffffffffffffffff) {
                continue;
            }

            // there is at least one free alloc available
            int offset = 0;
            while ((data & 1) != 0) {
                data >>= 1;
                offset++;
            }

            // mark alloc in bitmap
            ba.bitmap[i] |= 1UL << offset;
            next_free = (current_bitmap_block - bitmap_offset) * BLOCK_SIZE * 8 + i * 64 + offset;
            break;
        }

        current_bitmap_block++;
    }

    if (next_free == 0) {
        printf("SYS: Could not allocate a free %s.\n", title);
        return 0;
    }

    // write bitmap
    vm_write((current_bitmap_block - 1) * BLOCK_SIZE, &ba, BLOCK_SIZE);

    return next_free;
}

// write a block in place (BLOCK_SIZE bytes of given object)
blockptr_t alloc_block(const void* new_block) {
    super_block sb;
    vm_read(0, &sb, BLOCK_SIZE);

    // get next free block and write data block
    blockptr_t next_free_block = alloc_bitmap("block", sb.block_bitmap, sb.block_bitmap_length);
    vm_write(next_free_block * BLOCK_SIZE, new_block, BLOCK_SIZE);

    return next_free_block;
}

// write an inode in place
inodeptr_t alloc_inode(const inode_t* new_inode) {
    super_block sb;
    vm_read(0, &sb, BLOCK_SIZE);

    // get next free inode
    inodeptr_t next_free_inode = alloc_bitmap("inode", sb.inode_bitmap, sb.inode_bitmap_length);

    // get inode table block
    blockptr_t inode_table_blockptr = sb.inode_table + next_free_inode / (BLOCK_SIZE / sizeof(inode_t));
    inode_block inode_table_block;
    vm_read(inode_table_blockptr * BLOCK_SIZE, &inode_table_block, BLOCK_SIZE);

    // place inode into table and write table block
    inode_table_block.inodes[next_free_inode % (BLOCK_SIZE / sizeof(inode_t))] = *new_inode;
    vm_write(inode_table_blockptr * BLOCK_SIZE, &inode_table_block, BLOCK_SIZE);

    return next_free_inode;
}

// alloc a new data block for an inode
blockptr_t alloc_inode_data_block(inode_t* inode, const void* block) {
    if (inode->block_count >= 0xffffffff) {
        printf("SYS: inode has reached max block count \n");
        return 0;
    }

    // level struct for indirection convenience
    typedef struct level {
        blockptr_t* blockptr;
        indirect_block block;
    } level;

    blockptr_t offset = inode->block_count;
    blockptr_t blockptr = alloc_block(block);
    if (offset < INODE_DIRECT_BLOCKS) {
        inode->data_direct[offset] = blockptr;
        inode->block_count++;
    } else if ((offset -= INODE_DIRECT_BLOCKS) < INODE_SINGLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = &inode->data_single_indirect};
        if (read_or_alloc_block(level1.blockptr, &level1.block)) inode->block_count++;

        level1.block.blocks[offset] = blockptr;

        write_block(*level1.blockptr, &level1.block);
    } else if ((offset -= INODE_SINGLE_INDIRECT_BLOCKS) < INODE_DOUBLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = &inode->data_double_indirect};
        if (read_or_alloc_block(level1.blockptr, &level1.block)) inode->block_count++;

        level level2 = {.blockptr = &level1.block.blocks[offset / INODE_SINGLE_INDIRECT_BLOCKS]};
        if (read_or_alloc_block(level2.blockptr, &level2.block)) inode->block_count++;

        level2.block.blocks[offset % INODE_SINGLE_INDIRECT_BLOCKS] = blockptr;

        write_block(*level1.blockptr, &level1.block);
        write_block(*level2.blockptr, &level2.block);
    } else if ((offset -= INODE_DOUBLE_INDIRECT_BLOCKS) < INODE_TRIPLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = &inode->data_triple_indirect};
        if (read_or_alloc_block(level1.blockptr, &level1.block)) inode->block_count++;

        level level2 = {.blockptr = &level1.block.blocks[offset / INODE_DOUBLE_INDIRECT_BLOCKS]};
        if (read_or_alloc_block(level2.blockptr, &level2.block)) inode->block_count++;

        level level3 = {.blockptr = &level2.block.blocks[offset % INODE_DOUBLE_INDIRECT_BLOCKS]};
        if (read_or_alloc_block(level3.blockptr, &level3.block)) inode->block_count++;

        level3.block.blocks[offset % INODE_SINGLE_INDIRECT_BLOCKS] = blockptr;

        write_block(*level1.blockptr, &level1.block);
        write_block(*level2.blockptr, &level2.block);
        write_block(*level3.blockptr, &level3.block);
    } else {
        printf("SYS: relative data blockptr out of bounds\n");
    }

    return blockptr;
}

// alloc an entry in a directory inode
int alloc_dir_entry(inode_t* inode, const char* name, inodeptr_t target_inodeptr) {
    if (strlen(name) > MAX_FILENAME_LENGTH) {
        printf("SYS: Filename too long.\n");
        return ENAMETOOLONG;
    } else if (inode->link_count >= 0xffff) {
        printf("SYS: max link count reached\n");
        return EMLINK;
    } else if (inode->mode & M_DIR == 0) {
        printf("SYS: not a directory\n");
        return ENOTDIR;
    }

    dir_block block;
    dir_block_entry* entry;

    // get next free dir block
    blockptr_t block_offset = inode->atom_count / DIR_BLOCK_ENTRIES;
    int next_free_entry = inode->atom_count % DIR_BLOCK_ENTRIES;

    if (next_free_entry == 0) {
        // allocate a new dir block
        memset(&block, 0, BLOCK_SIZE);
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

// integer divide a / b and ceil of a % b > 0
uint32_t uint32_div_ceil(uint32_t a, uint32_t b) {
    uint32_t result = a / b;
    if (a % b > 0) {
        result++;
    }
    return result;
}

// read block from blockptr if not zero or init the block with zeroes
int read_or_alloc_block(blockptr_t* blockptr, void* block) {
    if (*blockptr) {
        read_block(*blockptr, block);
        return 0;
    } else {
        memset(block, 0, BLOCK_SIZE);
        *blockptr = alloc_block(block);
        return 1;
    }
}
