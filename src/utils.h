#ifndef FILESYSTEM_UTILS_H
#define FILESYSTEM_UTILS_H

int main(int argc, char** argv);

// callable from argv
void utils_print_superblock(const char* arg);
void utils_print_inode_alloc(const char* arg);
void utils_print_block_alloc(const char* arg);
void utils_print_block_bitmap(const char* arg);
void utils_print_inode_bitmap(const char* arg);
void utils_print_inode_table(const char* arg);
void utils_print_block(const char* arg);
void utils_print_inode(const char* arg);

// helpers
void utils_print_block_range(blockptr_t offset, blockptr_t length);
int utils_bitmap_allocated(blockptr_t ptr, blockptr_t block_bitmap, blockptr_t block_bitmap_length);
void utils_print_allocation_status(const char* title, blockptr_t alloc_start, blockptr_t alloc_end, blockptr_t bitmap_offset, blockptr_t bitmap_length);

#endif // FILESYSTEM_UTILS_H
