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

#endif // FILESYSTEM_UTILS_H
