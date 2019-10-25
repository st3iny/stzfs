#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "blocks.h"
#include "read.h"
#include "types.h"
#include "stzfs.h"
#include "super_block_cache.h"
#include "utils.h"
#include "vm.h"

// helpers
static void utils_print_block_range(blockptr_t offset, blockptr_t length);
static bool utils_bitmap_allocated(blockptr_t ptr, blockptr_t block_bitmap,
                                  blockptr_t block_bitmap_length);
static void utils_print_allocation_status(const char* title, blockptr_t alloc_start,
                                          blockptr_t alloc_end, blockptr_t bitmap_offset,
                                          blockptr_t bitmap_length);

// cli entry point
int main(int argc, char** argv) {
    // declare long options
    static void (*fun_ptr_arr[])(const char*) = {
        utils_print_superblock,
        utils_print_inode_alloc,
        utils_print_block_alloc,
        utils_print_block_bitmap,
        utils_print_inode_bitmap,
        utils_print_inode_table,
        utils_print_block,
        utils_print_inode
    };

    static int selected_fun;
    static const int option_count = 8;
    static struct option long_options[] = {
        {"superblock",   no_argument,       &selected_fun, 0},
        {"inode-alloc",  no_argument,       &selected_fun, 1},
        {"block-alloc",  no_argument,       &selected_fun, 2},
        {"block-bitmap", no_argument,       &selected_fun, 3},
        {"inode-bitmap", no_argument,       &selected_fun, 4},
        {"inode-table",  no_argument,       &selected_fun, 5},
        {"block",        required_argument, &selected_fun, 6},
        {"inode",        required_argument, &selected_fun, 7},
        {0,              0,                 0,             0}
    };

    // set vm hdd file
    vm_config_set_file(argv[1]);
    stzfs_init();

    // loop as long as there are long options available
    int opt;
    int option_index;
    while((opt = getopt_long(argc, argv, "", long_options, &option_index)) != -1) {
        if (opt == 0) {
            void (*fun_ptr)(const char*) = fun_ptr_arr[selected_fun];
            fun_ptr(optarg);
        }
    }
}

// callable from argv
void utils_print_superblock(const char* arg) {
    const super_block* sb = super_block_cache;
    printf("super_block = {\n");
    printf("\tblock_count = %i\n", sb->block_count);
    printf("\tfree_blocks = %i\n", sb->free_blocks);
    printf("\tblock_bitmap = %i\n", sb->block_bitmap);
    printf("\tblock_bitmap_length = %i\n", sb->block_bitmap_length);
    printf("\tinode_bitmap = %i\n", sb->inode_bitmap);
    printf("\tinode_bitmap_length = %i\n", sb->inode_bitmap_length);
    printf("\tinode_table = %i\n", sb->inode_table);
    printf("\tinode_table_length = %i\n", sb->inode_table_length);
    printf("\tinode_count = %i\n", sb->inode_count);
    printf("}\n");
}

void utils_print_inode_alloc(const char* arg) {
    const super_block* sb = super_block_cache;
    utils_print_allocation_status("inodes", 1, sb->inode_count, sb->inode_bitmap,
                                  sb->inode_bitmap_length);
}

void utils_print_block_alloc(const char* arg) {
    const super_block* sb = super_block_cache;
    utils_print_allocation_status("blocks", 0, sb->block_count, sb->block_bitmap,
                                  sb->block_bitmap_length);
}

void utils_print_block_bitmap(const char* arg) {
    const super_block* sb = super_block_cache;
    utils_print_block_range(sb->block_bitmap, sb->block_bitmap_length);
}

void utils_print_inode_bitmap(const char* arg) {
    const super_block* sb = super_block_cache;
    utils_print_block_range(sb->inode_bitmap, sb->inode_bitmap_length);
}

void utils_print_inode_table(const char* arg) {
    const super_block* sb = super_block_cache;
    utils_print_block_range(sb->inode_table, sb->inode_table_length);
}

void utils_print_block(const char* arg) {
    blockptr_t blockptr = strtol(arg, NULL, 10);
    data_block block;
    read_block(blockptr, &block);

    // write block to stdout to enable piping to hexdump for example
    write(1, &block, STZFS_BLOCK_SIZE);
}

void utils_print_inode(const char* arg) {
    const super_block* sb = super_block_cache;

    inodeptr_t inodeptr = strtol(arg, NULL, 10);
    blockptr_t inode_table_block_offset = inodeptr / (STZFS_BLOCK_SIZE / INODE_SIZE);

    if (inode_table_block_offset > sb->inode_table_length) {
        fprintf(stderr, "out of bound while trying to read inode at %i\n", inodeptr);
    }

    inode_block inode_table_block;
    read_block(sb->inode_table + inode_table_block_offset, &inode_table_block);
    inode_t* inode_data = &inode_table_block.inodes[inodeptr % (STZFS_BLOCK_SIZE / INODE_SIZE)];

    printf("inode@%i = {\n", inodeptr);
    printf("\tmode = %u\n", inode_data->mode);
    printf("\tuid = %i\n", inode_data->uid);
    printf("\tgid = %i\n", inode_data->gid);

    struct tm* time_info;
    char pretty_time[100];
    char format_string[] = "%X %d.%m.%Y";

    time_info = localtime(&inode_data->atime.tv_sec);
    strftime(pretty_time, 100, format_string, time_info);
    printf("\tatime = %s\n", pretty_time);

    time_info = localtime(&inode_data->mtime.tv_sec);
    strftime(pretty_time, 100, format_string, time_info);
    printf("\tmtime = %s\n", pretty_time);

    time_info = localtime(&inode_data->ctime.tv_sec);
    strftime(pretty_time, 100, format_string, time_info);
    printf("\tctime = %s\n", pretty_time);

    printf("\tlink_count = %i\n", inode_data->link_count);
    printf("\tatom_count = %lu\n", inode_data->atom_count);
    printf("\tblock_count = %i\n", inode_data->block_count);

    printf("\tdata_direct = [");
    for (int i = 0; i < INODE_DIRECT_BLOCKS; i++) {
        printf("%u", inode_data->data_direct[i]);
        if (i < INODE_DIRECT_BLOCKS - 1) {
            printf(", ");
        }
    }
    printf("]\n");

    printf("\tdata_single_indirect = %i\n", inode_data->data_single_indirect);
    printf("\tdata_double_indirect = %i\n", inode_data->data_double_indirect);
    printf("\tdata_triple_indirect = %i\n", inode_data->data_triple_indirect);
    printf("}\n");
}

// helpers
void utils_print_block_range(blockptr_t offset, blockptr_t length) {
    for (blockptr_t blockptr = offset; blockptr < offset + length; blockptr++) {
        data_block block;
        read_block(blockptr, &block);
        write(1, &block, STZFS_BLOCK_SIZE);
    }
}

bool utils_bitmap_allocated(blockptr_t ptr, blockptr_t block_bitmap, blockptr_t block_bitmap_length) {
    blockptr_t bitmap_block_offset = ptr / (STZFS_BLOCK_SIZE * 8);
    blockptr_t inner_offset = (ptr / (sizeof(bitmap_entry_t) * 8)) % BITMAP_BLOCK_ENTRIES;

    if (bitmap_block_offset >= block_bitmap_length) {
        fprintf(stderr, "out of bounds while trying to check bitmap allocation at %i\n", bitmap_block_offset);
        return 0;
    }

    // extract bitmap allocation status for given block or inode
    bitmap_block ba;
    read_block(block_bitmap + bitmap_block_offset, &ba);
    bitmap_entry_t entry = ba.bitmap[inner_offset];

    // mask status as only the least significant bit matters
    return (entry >> (ptr % (sizeof(bitmap_entry_t) * 8))) & 1;
}

void utils_print_allocation_status(const char* title, blockptr_t alloc_start, blockptr_t alloc_end, blockptr_t bitmap_offset, blockptr_t bitmap_length) {
    printf("allocated_%s = [\n", title);

    bool begin_set = false;
    bool end_set = false;
    blockptr_t begin, end;
    for (blockptr_t i = alloc_start; i <= alloc_end; i++) {
        bool allocated;
        if (i == alloc_end) {
            allocated = false;
        } else {
            allocated = utils_bitmap_allocated(i, bitmap_offset, bitmap_length);
        }

        if (!begin_set && allocated) {
            begin = i;
            begin_set = true;
        } else if (begin_set && !allocated) {
            end = i - 1;
            end_set = true;
        }

        if (begin_set && end_set) {
            if (begin == end) {
                printf("\t%lu", end);
            } else {
                printf("\t%lu - %lu", begin, end);
            }
            begin_set = false;
            end_set = false;
        }
    }

    printf("\n]\n");
}
