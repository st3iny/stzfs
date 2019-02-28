#include <stdio.h>
#include <string.h>
#include <time.h>

#include "syscalls.h"
#include "vm.h"

blockptr_t sys_makefs(inodeptr_t inode_count) {
    blockptr_t blocks = vm_size() / BLOCK_SIZE;
    printf("SYS: Creating file system with %i blocks and %i inodes.\n", blocks, inode_count);

    // calculate bitmap and inode table lengths
    blockptr_t block_bitmap_length = blocks / (BLOCK_SIZE * 8);
    if (blocks % (BLOCK_SIZE * 8) > 0) {
        block_bitmap_length++;
    }

    inodeptr_t inode_table_length = inode_count / (BLOCK_SIZE / sizeof(inode));
    if (inode_count % (BLOCK_SIZE / sizeof(inode)) > 0) {
        inode_table_length++;
    }

    inodeptr_t inode_bitmap_length = inode_count / (BLOCK_SIZE * 8);
    if (inode_count % (BLOCK_SIZE * 8) > 0) {
        inode_bitmap_length++;
    }

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
    for (blockptr_t blockptr = 0; blockptr < initial_block_count; blockptr++) {
        data_block block;
        memset(&block, 0, BLOCK_SIZE);
        vm_write(blockptr * BLOCK_SIZE, &block, BLOCK_SIZE);
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

    // write empty root directory block
    dir_block root_dir_block;
    memset(&root_dir_block, 0, BLOCK_SIZE);
    blockptr_t root_dir_block_ptr = alloc_block(&root_dir_block);
    printf("SYS: Wrote root dir block at %i.\n", root_dir_block_ptr);

    // create root inode
    time_t now;
    time(&now);
    inode root_inode;
    memset(&root_inode, 0, INODE_SIZE);
    root_inode.mode = M_RU | M_WU | M_XU | M_RG | M_XG | M_RO | M_XO;
    root_inode.uid = 0;
    root_inode.gid = 0;
    root_inode.crtime = now;
    root_inode.mtime = now;
    root_inode.link_count = 0;
    root_inode.byte_length = 0;
    root_inode.block_length = 1;
    root_inode.data_direct[0] = root_dir_block_ptr;

    // write root inode
    inodeptr_t root_inode_ptr = alloc_inode(&root_inode);
    printf("SYS: Wrote root inode with id %i.\n", root_inode_ptr);

    return blocks;
}


// fd_t sys_open(const filename_t* path);
// int sys_close(fd_t fd);
// unsigned int sys_write(fd_t fd, buffer_t* buffer, fileptr_t offset, fileptr_t length);
// int sys_read(fd_t fd, const buffer_t* buffer, fileptr_t offset, fileptr_t length);

// int sys_create(const filename_t* path);
// int sys_rename(const filename_t* src, const filename_t* dst);
// int sys_unlink(const filename_t* path);

// helpers
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

blockptr_t alloc_block(const data_block* new_block) {
    super_block sb;
    vm_read(0, &sb, BLOCK_SIZE);

    // get next free block and write data block
    blockptr_t next_free_block = alloc_bitmap("block", sb.block_bitmap, sb.block_bitmap_length);
    vm_write(next_free_block * BLOCK_SIZE, new_block, BLOCK_SIZE);

    return next_free_block;
}

inodeptr_t alloc_inode(const inode* new_inode) {
    super_block sb;
    vm_read(0, &sb, BLOCK_SIZE);

    // get next free inode
    inodeptr_t next_free_inode = alloc_bitmap("inode", sb.inode_bitmap, sb.inode_bitmap_length);

    // get inode table block
    blockptr_t inode_table_blockptr = sb.inode_table + next_free_inode / (BLOCK_SIZE / sizeof(inode));
    inode_block inode_table_block;
    vm_read(inode_table_blockptr * BLOCK_SIZE, &inode_table_block, BLOCK_SIZE);

    // place inode into table and write table block
    inode_table_block.inodes[next_free_inode % (BLOCK_SIZE / sizeof(inode))] = *new_inode;
    vm_write(inode_table_blockptr * BLOCK_SIZE, &inode_table_block, BLOCK_SIZE);

    return next_free_inode;
}
