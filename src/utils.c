#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "blocks.h"
#include "utils.h"
#include "vm.h"

// helpers
static void utils_print_block_range(blockptr_t offset, blockptr_t length);
static int utils_bitmap_allocated(blockptr_t ptr, blockptr_t block_bitmap,
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
    super_block sb;
    vm_read(0, &sb, BLOCK_SIZE);

    printf("super_block = {\n");
    printf("\tblock_count = %i\n", sb.block_count);
    printf("\tfree_blocks = %i\n", sb.free_blocks);
    printf("\tblock_bitmap = %i\n", sb.block_bitmap);
    printf("\tblock_bitmap_length = %i\n", sb.block_bitmap_length);
    printf("\tinode_bitmap = %i\n", sb.inode_bitmap);
    printf("\tinode_bitmap_length = %i\n", sb.inode_bitmap_length);
    printf("\tinode_table = %i\n", sb.inode_table);
    printf("\tinode_table_length = %i\n", sb.inode_table_length);
    printf("\tinode_count = %i\n", sb.inode_count);
    printf("}\n");
}

void utils_print_inode_alloc(const char* arg) {
    super_block sb;
    vm_read(0, &sb, BLOCK_SIZE);
    utils_print_allocation_status("inodes", 1, sb.inode_count, sb.inode_bitmap, sb.inode_bitmap_length);
}

void utils_print_block_alloc(const char* arg) {
    super_block sb;
    vm_read(0, &sb, BLOCK_SIZE);
    utils_print_allocation_status("blocks", 0, sb.block_count, sb.block_bitmap, sb.block_bitmap_length);
}

void utils_print_block_bitmap(const char* arg) {
    super_block sb;
    vm_read(0, &sb, BLOCK_SIZE);
    utils_print_block_range(sb.block_bitmap, sb.block_bitmap_length);
}

void utils_print_inode_bitmap(const char* arg) {
    super_block sb;
    vm_read(0, &sb, BLOCK_SIZE);
    utils_print_block_range(sb.inode_bitmap, sb.inode_bitmap_length);
}

void utils_print_inode_table(const char* arg) {
    super_block sb;
    vm_read(0, &sb, BLOCK_SIZE);
    utils_print_block_range(sb.inode_table, sb.inode_table_length);
}

void utils_print_block(const char* arg) {
    blockptr_t blockptr = strtol(arg, NULL, 10);
    data_block block;
    vm_read(blockptr * BLOCK_SIZE, &block, BLOCK_SIZE);

    // write block to stdout to enable piping to hexdump for example
    write(1, &block, BLOCK_SIZE);
}

void utils_print_inode(const char* arg) {
    super_block sb;
    vm_read(0, &sb, BLOCK_SIZE);

    inodeptr_t inodeptr = strtol(arg, NULL, 10);
    blockptr_t inode_table_block_offset = inodeptr / (BLOCK_SIZE / INODE_SIZE);

    if (inode_table_block_offset > sb.inode_table_length) {
        fprintf(stderr, "out of bound while trying to read inode at %i\n", inodeptr);
    }

    inode_block inode_table_block;
    vm_read((sb.inode_table + inode_table_block_offset) * BLOCK_SIZE, &inode_table_block, BLOCK_SIZE);
    inode_t* inode_data = &inode_table_block.inodes[inodeptr % (BLOCK_SIZE / INODE_SIZE)];

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
        vm_read(blockptr * BLOCK_SIZE, &block, BLOCK_SIZE);
        write(1, &block, BLOCK_SIZE);
    }
}

int utils_bitmap_allocated(blockptr_t ptr, blockptr_t block_bitmap, blockptr_t block_bitmap_length) {
    blockptr_t bitmap_block_offset = ptr / (BLOCK_SIZE * 8);
    blockptr_t inner_offset = ptr % (BLOCK_SIZE * 8);

    if (bitmap_block_offset >= block_bitmap_length) {
        fprintf(stderr, "out of bounds while trying to check bitmap allocation at %i\n", bitmap_block_offset);
        return 0;
    }

    // extract bitmap allocation status for given block or inode
    bitmap_block ba;
    vm_read((block_bitmap + bitmap_block_offset) * BLOCK_SIZE, &ba, BLOCK_SIZE);
    uint64_t status = ba.bitmap[inner_offset / 64] >> (inner_offset % 64);

    // mask status as only the least significant bit matters
    return (int)(status & 1);
}

void utils_print_allocation_status(const char* title, blockptr_t alloc_start, blockptr_t alloc_end, blockptr_t bitmap_offset, blockptr_t bitmap_length) {
    printf("allocated_%s = [\n", title);

    long begin = -1;
    int count = 0;
    blockptr_t allocptr;
    for (allocptr = alloc_start; allocptr <= alloc_end; allocptr++) {
        int printed = 0;
        if (allocptr < alloc_end && utils_bitmap_allocated(allocptr, bitmap_offset, bitmap_length)) {
            if (begin == -1) {
                begin = allocptr;
            }
        } else if (begin == allocptr - 1) {
            printf("\t%lu", begin);
            printed = 1;
        } else if (begin != -1) {
            printf("\t%lu - %i", begin, allocptr - 1);
            printed = 1;
        }

        if ((printed && count > 0 && count % 10 == 0) || allocptr == alloc_end) {
            printf("\n");
        }
        if (printed) {
            printed = 0;
            begin = -1;
            count++;
        }
    }

    printf("]\n");
}
