#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/fs.h>
#include <time.h>

#include "blocks.h"
#include "syscalls.h"
#include "vm.h"

// fuse operations
struct fuse_operations stzfs_ops = {
    .init = stzfs_init,
    .create = stzfs_create,
    .rename = stzfs_rename,
    .unlink = stzfs_unlink,
    .getattr = stzfs_getattr,
    .open = stzfs_open,
    .read = stzfs_read,
    .write = stzfs_write,
    .mkdir = stzfs_mkdir,
    .rmdir = stzfs_rmdir,
    .readdir = stzfs_readdir
};

// structs
typedef struct file {
    inodeptr_t inodeptr;
    inode_t inode;
} file;

// find data in read blocks
static int find_file_inode(const char* file_path, inodeptr_t* inodeptr, inode_t* inode,
                    inodeptr_t* parent_inodeptr, inode_t* parent_inode, char* last_name);
static int find_file_inode2(const char* file_path, file* f, file* parent, char* last_name);
static int find_name(const char* name, const inode_t* inode, inodeptr_t* found_inodeptr);
static blockptr_t find_inode_data_blockptr(const inode_t* inode, blockptr_t offset);
static int find_inode_data_blockptrs(const inode_t* inode, blockptr_t* blockptrs);

// read from disk
static void read_block(blockptr_t blockptr, void* block);
static void read_inode(inodeptr_t inodeptr, inode_t* inode);
static blockptr_t read_inode_data_block(const inode_t* inode, blockptr_t offset, void* block);
static int read_inode_data_blocks(const inode_t* inode, void* data_block_array);

// alloc in bitmap
static blockptr_t alloc_bitmap(const char* title, blockptr_t bitmap_offset, blockptr_t bitmap_length);
static blockptr_t alloc_block(const void* new_block);
static inodeptr_t alloc_inode(const inode_t* new_inode);
static blockptr_t alloc_inode_data_block(inode_t* inode, const void* block);
static int alloc_dir_entry(inode_t* inode, const char* name, inodeptr_t target_inodeptr);

// free
static int free_bitmap(blockptr_t index, blockptr_t bitmap_offset, blockptr_t bitmap_length);
static int free_blocks(const blockptr_t* blockptrs, size_t length);
static int free_inode(inodeptr_t inodeptr, inode_t* inode);
static int free_inode_data_blocks(inode_t* inode, blockptr_t offset);
static int free_last_inode_data_block(inode_t* inode);
static int free_dir_entry(inode_t* inode, const char* name);

// write to disk
static void write_block(blockptr_t blockptr, const void* block);
static void write_inode(inodeptr_t inodeptr, const inode_t* inode);
static blockptr_t write_inode_data_block(const inode_t* inode, blockptr_t offset, const void* block);
static int write_dir_entry(const inode_t* inode, const char* name, inodeptr_t target_inodeptr);

// file helpers
static int file_exists(const char* path);

// misc helpers
static uint32_t uint32_div_ceil(uint32_t a, uint32_t b);
static int read_or_alloc_block(blockptr_t* blockptr, void* block);
static void memcpy_min(void* dest, const void* src, size_t mult, size_t a, size_t b);
static int unlink_file_or_dir(const char* path, int allow_dir);

// init filesystem
blockptr_t stzfs_makefs(inodeptr_t inode_count) {
    blockptr_t blocks = vm_size() / BLOCK_SIZE;
    printf("stzfs_makefs: creating file system with %i blocks and %i inodes\n", blocks, inode_count);

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
    sb.free_inodes = inode_count - 2;
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
    printf("stzfs_makefs: wrote %i initial blocks\n", initial_block_count);

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
    printf("stzfs_makefs: wrote root dir block at %i\n", root_dir_block_ptr);

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
    printf("stzfs_makefs: wrote root inode with id %i\n", root_inode_ptr);

    return blocks;
}

// init fuse
void* stzfs_init(struct fuse_conn_info* conn, struct fuse_config* cfg) {
    cfg->kernel_cache = 1;
    cfg->use_ino = 1;
    return NULL;
}

// get file stats
int stzfs_getattr(const char* path, struct stat* st, struct fuse_file_info* file_info) {
    // TODO: reuse inode
    if (!file_exists(path)) {
        // printf("stat: no such file\n");
        return -ENOENT;
    }

    file f;
    int err = find_file_inode2(path, &f, NULL, NULL);
    if (err) return err;

    if ((f.inode.mode & M_DIR)) {
        st->st_ino = f.inodeptr;
        st->st_mode = S_IFDIR | 0755;
        st->st_size = f.inode.atom_count * sizeof(dir_block_entry);
        st->st_nlink = f.inode.link_count;
    } else {
        st->st_ino = f.inodeptr;
        st->st_mode = S_IFREG | 0444;
        st->st_size = f.inode.atom_count;
        st->st_nlink = f.inode.link_count;
    }

    return 0;
}

// open existing file
int stzfs_open(const char* file_path, struct fuse_file_info* file_info) {
    inodeptr_t inodeptr, parent_inodeptr;
    inode_t inode, parent_inode;
    int err = find_file_inode(file_path, &inodeptr, &inode, &parent_inodeptr, &parent_inode, NULL);
    if (err) return err;

    if (inodeptr == 0) {
        printf("stzfs_open: no such file\n");
        return -ENOENT;
    } else if (inode.mode & M_DIR) {
        printf("stzfs_open: is a directory\n");
        return -EISDIR;
    } else {
        // open exsiting file
        file_info->fh = inodeptr;
        return 0;
    }
}

// read from a file
int stzfs_read(const char* file_path, char* buffer, size_t length, off_t offset,
               struct fuse_file_info* file_info) {
    if (file_info->fh == 0) {
        printf("stzfs_read: invald file handle (inode)\n");
        return -EFAULT;
    }

    inodeptr_t inodeptr = file_info->fh;
    inode_t inode;
    read_inode(inodeptr, &inode);

    // check file bounds
    if (inode.mode & M_DIR) {
        printf("stzfs_read: is a directory\n");
        return -EISDIR;
    }

    // end of file reached
    if (offset >= inode.atom_count) {
        return 0;
    }

    // read first partial block
    size_t read_bytes = 0;
    blockptr_t initial_offset = offset / BLOCK_SIZE;
    size_t initial_byte_offset = offset % BLOCK_SIZE;
    if (initial_byte_offset > 0) {
        data_block block;
        read_inode_data_block(&inode, initial_offset, &block);

        // keep block boundaries
        read_bytes = BLOCK_SIZE - initial_byte_offset;
        if (read_bytes > length) {
            read_bytes = length;
        }
        memcpy(buffer, &block.data[initial_byte_offset], read_bytes);

        initial_offset++;
    }

    size_t max_bytes = inode.atom_count < length ? inode.atom_count : length;
    for (blockptr_t block_offset = initial_offset; block_offset < inode.block_count; block_offset++) {
        data_block block;
        read_inode_data_block(&inode, block_offset, &block);

        size_t diff = max_bytes - read_bytes;
        if (diff <= 0) {
            break;
        } else if (diff > 0 && diff < BLOCK_SIZE) {
            memcpy(&buffer[read_bytes], &block, diff);
            read_bytes += diff;
        } else {
            memcpy(&buffer[read_bytes], &block, BLOCK_SIZE);
            read_bytes += BLOCK_SIZE;
        }
    }

    return read_bytes;
}

// write to a file
int stzfs_write(const char* file_path, const char* buffer, size_t length, off_t offset,
                struct fuse_file_info* file_info) {
    if (file_info->fh == 0) {
        printf("stzfs_write: invald file handle (inode)\n");
        return -EFAULT;
    }

    inodeptr_t inodeptr = file_info->fh;
    inode_t inode;
    read_inode(inodeptr, &inode);

    // check file size limits
    size_t max_block_offset = uint32_div_ceil(offset + length, BLOCK_SIZE);
    if (max_block_offset >= INODE_MAX_BLOCKS) {
        printf("stzfs_write: max file size exceeded\n");
        return -EFBIG;
    }

    // check continuity
    if (offset > inode.atom_count) {
        printf("stzfs_write: offset too high\n");
        return -ESPIPE;
    }

    // write first partial block
    size_t written_bytes = 0;
    blockptr_t initial_offset = offset / BLOCK_SIZE;
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
            if (block_offset < inode.block_count) {
                read_inode_data_block(&inode, block_offset, &block);
            } else {
                memset(&block.data[diff], 0, BLOCK_SIZE - diff);
            }

            memcpy(&block, &buffer[written_bytes], diff);
            written_bytes += diff;
        } else {
            memcpy(&block, &buffer[written_bytes], BLOCK_SIZE);
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
int stzfs_create(const char* file_path, mode_t mode, struct fuse_file_info* file_info) {
    inodeptr_t inodeptr, parent_inodeptr;
    inode_t inode, parent_inode;
    char last_name[2048];
    int err = find_file_inode(file_path, &inodeptr, &inode, &parent_inodeptr, &parent_inode, last_name);
    if (err) return err;

    if (inodeptr != 0) {
        printf("stzfs_create: file is already existing\n");
        return -EEXIST;
    } else {
        // create new file
        time_t now;
        time(&now);
        // TODO: implement mode
        inode.mode = 0;
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

// rename a file
int stzfs_rename(const char* src_path, const char* dst_path, unsigned int flags) {
    // TODO: improve me (use inodes and ptrs)
    int src_exists = file_exists(src_path);
    int dst_exists = file_exists(dst_path);

    if (src_exists == 0) {
        printf("stzfs_rename: src file does not exist\n");
        return -ENOENT;
    } else if (dst_exists && (flags & RENAME_NOREPLACE)) {
        printf("stzfs_rename: dest file exists but RENAME_NOREPLACE is set\n");
        return -EEXIST;
    }

    // find src file nodes
    char src_last_name[MAX_FILENAME_LENGTH];
    file src, src_parent;
    int err = find_file_inode(src_path, &src.inodeptr, &src.inode, &src_parent.inodeptr,
                              &src_parent.inode, src_last_name);
    if (err) return err;

    // find dest file nodes
    char dst_last_name[MAX_FILENAME_LENGTH];
    file dst, dst_parent;
    err = find_file_inode(dst_path, &dst.inodeptr, &dst.inode, &dst_parent.inodeptr,
                          &dst_parent.inode, dst_last_name);
    if (err) return err;

    // TODO: update parent dir pointer if src is a dir
    // replace dst with src
    src.inode.link_count++;
    write_inode(src.inodeptr, &src.inode);

    if (dst_exists) {
        write_dir_entry(&dst_parent.inode, dst_last_name, src.inodeptr);
        dst.inode.link_count--;
        if (dst.inode.link_count <= 0) {
            free_inode(dst.inodeptr, &dst.inode);
        } else {
            write_inode(dst.inodeptr, &dst.inode);
        }
    } else {
        alloc_dir_entry(&dst_parent.inode, dst_last_name, src.inodeptr);
        write_inode(dst_parent.inodeptr, &dst_parent.inode);
    }

    if (src_parent.inodeptr == dst_parent.inodeptr) {
        src_parent.inode = dst_parent.inode;
    }

    // unlink src from parent directory
    free_dir_entry(&src_parent.inode, src_last_name);
    write_inode(src_parent.inodeptr, &src_parent.inode);

    src.inode.link_count--;
    write_inode(src.inodeptr, &src.inode);
}

// unlink a file
int stzfs_unlink(const char* path) {
    return unlink_file_or_dir(path, 0);
}

// create a new directory
int stzfs_mkdir(const char* path, mode_t mode) {
    char name[MAX_FILENAME_LENGTH];
    file dir, parent;
    int err = find_file_inode2(path, &dir, &parent, name);
    if (err) return err;

    if (dir.inodeptr != 0) {
        printf("stzfs_mkdir: file exists\n");
        return -EEXIST;
    }

    super_block sb;
    read_block(0, &sb);

    if (sb.free_blocks == 0) {
        printf("stzfs_mkdir: no free block available\n");
        return -ENOSPC;
    } else if (sb.free_inodes == 0) {
        printf("stzfs_mkdir: no free inode available\n");
        return -ENOSPC;
    }

    // allocate inode in bitmap
    dir.inodeptr = alloc_bitmap("inode", sb.inode_bitmap, sb.inode_bitmap_length);

    // allocate and write directory block
    dir_block block;
    memset(&block, 0, BLOCK_SIZE);
    block.entries[0] = (dir_block_entry) {.name = ".", .inode=dir.inodeptr};
    block.entries[1] = (dir_block_entry) {.name = "..", .inode=parent.inodeptr};
    blockptr_t blockptr = alloc_block(&block);

    // increase parent inode link counter
    parent.inode.link_count++;
    write_inode(parent.inodeptr, &parent.inode);

    // create and write inode
    time_t now;
    time(&now);
    dir.inode.mode = M_DIR;
    dir.inode.link_count = 2;
    dir.inode.atom_count = 2;
    dir.inode.block_count = 1;
    dir.inode.crtime = now;
    dir.inode.mtime = now;
    dir.inode.data_direct[0] = blockptr;
    write_inode(dir.inodeptr, &dir.inode);

    // allocate entry in parent dir
    alloc_dir_entry(&parent.inode, name, dir.inodeptr);
    write_inode(parent.inodeptr, &parent.inode);

    return 0;
}

// remove an empty directory
int stzfs_rmdir(const char* path) {
    // TODO: check root directory? fuse relative or absolute path?
    return unlink_file_or_dir(path, 1);
}

// read the contents of a directory
int stzfs_readdir(const char* path, void* buffer, fuse_fill_dir_t filler, off_t offset,
                  struct fuse_file_info* file_info, enum fuse_readdir_flags flags) {
    if (!file_exists(path)) {
        printf("stzfs_readdir: no such directory\n");
        return -ENOENT;
    }

    file dir;
    find_file_inode2(path, &dir, NULL, NULL);

    if ((dir.inode.mode & M_DIR) == 0) {
        printf("stzfs_readdir: not a directory\n");
        return -ENOTDIR;
    }

    dir_block dir_blocks[dir.inode.block_count];
    read_inode_data_blocks(&dir.inode, dir_blocks);

    for (blockptr_t offset = 0; offset < dir.inode.block_count; offset++) {
        for (size_t entry = 0; entry < DIR_BLOCK_ENTRIES; entry++) {
            if (entry + offset * DIR_BLOCK_ENTRIES >= dir.inode.atom_count) return 0;

            dir_block_entry* entry_data = &dir_blocks[offset].entries[entry];
            filler(buffer, entry_data->name, NULL, 0, 0);
        }
    }

    return 0;
}

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
    // char* full_name = (char*)malloc(2048 * sizeof(char));
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
        printf("read_inode: out of bounds while trying to read inode\n");
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
        printf("read_inode_data_block: could not read inode data block\n");
        return 0;
    }

    read_block(blockptr, block);
    return blockptr;
}

// read all data blocks of an inode
int read_inode_data_blocks(const inode_t* inode, void* data_block_array) {
    blockptr_t blockptrs[inode->block_count];
    int err = find_inode_data_blockptrs(inode, blockptrs);
    if (err) return err;

    for (size_t offset = 0; offset < inode->block_count; offset++) {
        read_block(blockptrs[offset], &((data_block*)data_block_array)[offset]);
    }

    return 0;
}

// write block to disk
void write_block(blockptr_t blockptr, const void* block) {
    vm_write(blockptr * BLOCK_SIZE, block, BLOCK_SIZE);
}

// write inode to disk
void write_inode(inodeptr_t inodeptr, const inode_t* inode) {
    if (inodeptr == 0) {
        printf("write_inode: can't write protected inode 0\n");
        return;
    }

    super_block sb;
    read_block(0, &sb);

    blockptr_t table_block_offset = inodeptr / INODE_BLOCK_ENTRIES;
    if (table_block_offset > sb.inode_table_length) {
        printf("write_inode: inode index out of bounds\n");
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
        printf("write_inode_data_block: could not write inode data block\n");
        return 0;
    }

    write_block(blockptr, block);
    return blockptr;
}

// replace inodeptr of name in directory
int write_dir_entry(const inode_t* inode, const char* name, inodeptr_t target_inodeptr) {
    if ((inode->mode & M_DIR) == 0) {
        printf("write_dir_entry: not a directory\n");
        return -ENOTDIR;
    }

    dir_block dir_blocks[inode->block_count];
    read_inode_data_blocks(inode, dir_blocks);

    // search and replace name in directory
    for (blockptr_t offset = 0; offset < inode->block_count; offset++) {
        for (size_t entry = 0; entry < DIR_BLOCK_ENTRIES; entry++) {
            dir_block_entry* entry_data = &dir_blocks[offset].entries[entry];
            if (strcmp(entry_data->name, name) == 0) {
                entry_data->inode = target_inodeptr;
                write_inode_data_block(inode, offset, &dir_blocks[offset]);
                return 0;
            }
        }
    }

    printf("write_dir_entry: name does not exist in directory\n");
    return -ENOENT;
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
        printf("alloc_bitmap: could not allocate a free %s\n", title);
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
    if (inode->block_count >= INODE_MAX_BLOCKS) {
        printf("alloc_inode_data_block: inode has reached max block count\n");
        return 0;
    }

    // level struct for indirection convenience
    typedef struct level {
        blockptr_t* blockptr;
        indirect_block block;
        int changed;
    } level;

    blockptr_t offset = inode->block_count;
    blockptr_t blockptr = alloc_block(block);
    if (offset < INODE_DIRECT_BLOCKS) {
        inode->data_direct[offset] = blockptr;
    } else if ((offset -= INODE_DIRECT_BLOCKS) < INODE_SINGLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = &inode->data_single_indirect};
        read_or_alloc_block(level1.blockptr, &level1.block);

        level1.block.blocks[offset] = blockptr;

        write_block(*level1.blockptr, &level1.block);
    } else if ((offset -= INODE_SINGLE_INDIRECT_BLOCKS) < INODE_DOUBLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = &inode->data_double_indirect};
        if (read_or_alloc_block(level1.blockptr, &level1.block)) level1.changed = 1;

        level level2 = {.blockptr = &level1.block.blocks[offset / INODE_SINGLE_INDIRECT_BLOCKS]};
        if (read_or_alloc_block(level2.blockptr, &level2.block)) level1.changed = 1;

        level2.block.blocks[offset % INODE_SINGLE_INDIRECT_BLOCKS] = blockptr;

        if (level1.changed) write_block(*level1.blockptr, &level1.block);
        write_block(*level2.blockptr, &level2.block);
    } else if ((offset -= INODE_DOUBLE_INDIRECT_BLOCKS) < INODE_TRIPLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = &inode->data_triple_indirect};
        if (read_or_alloc_block(level1.blockptr, &level1.block)) level1.changed = 1;

        level level2 = {.blockptr = &level1.block.blocks[offset / INODE_DOUBLE_INDIRECT_BLOCKS]};
        if (read_or_alloc_block(level2.blockptr, &level2.block)) {
            level1.changed = 1;
            level2.changed = 1;
        }

        level level3 = {.blockptr = &level2.block.blocks[offset % INODE_DOUBLE_INDIRECT_BLOCKS]};
        if (read_or_alloc_block(level3.blockptr, &level3.block)) level2.changed = 1;

        level3.block.blocks[offset % INODE_SINGLE_INDIRECT_BLOCKS] = blockptr;

        if (level1.changed) write_block(*level1.blockptr, &level1.block);
        if (level2.changed) write_block(*level2.blockptr, &level2.block);
        write_block(*level3.blockptr, &level3.block);
    }

    // one block is allocated in each case
    inode->block_count++;
    return blockptr;
}

// alloc an entry in a directory inode
int alloc_dir_entry(inode_t* inode, const char* name, inodeptr_t target_inodeptr) {
    if (strlen(name) > MAX_FILENAME_LENGTH) {
        printf("alloc_dir_entry: filename too long\n");
        return -ENAMETOOLONG;
    } else if (inode->link_count >= 0xffff) {
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

// free blocks in bitmap
// TODO: improve me
int free_blocks(const blockptr_t* blockptrs, size_t length) {
    super_block sb;
    read_block(0, &sb);

    for (size_t offset = 0; offset < length; offset++) {
        int err = free_bitmap(blockptrs[offset], sb.block_bitmap, sb.block_bitmap_length);
        if (err) return err;
    }

    return 0;
}

// free inode and allocated data blocks
int free_inode(inodeptr_t inodeptr, inode_t* inode) {
    if (inodeptr <= 1) {
        printf("free_inode: trying to free protected inode\n");
        return -EFAULT;
    } else if ((inode->mode & M_DIR) && inode->atom_count > 2) {
        printf("free_inode: directory is not empty\n");
        return -ENOTEMPTY;
    } else if ((inode->mode & M_DIR) && inode->link_count > 1) {
        printf("free_inode: directory inode link count too high\n");
        return -EPERM;
    } else if ((inode->mode & M_DIR) == 0 && inode->link_count > 0) {
        printf("free_inode: file inode link count too high\n");
        return -EPERM;
    }

    super_block sb;
    read_block(0, &sb);

    // dealloc inode first
    free_bitmap(inodeptr, sb.inode_bitmap, sb.inode_bitmap_length);

    // free allocated data blocks in bitmap
    free_inode_data_blocks(inode, 0);

    return 0;
}

// truncate inode data blocks to given offset
int free_inode_data_blocks(inode_t* inode, blockptr_t offset) {
    // FIXME: implement proper algorithm (merge with free_last_inode_data_block)
    for (blockptr_t block_count = inode->block_count; block_count > offset; block_count--) {
        free_last_inode_data_block(inode);
    }

    return 0;
}

// free the last data block of an inode
int free_last_inode_data_block(inode_t* inode) {
    if (inode->block_count <= 0) {
        return 0;
    }

    typedef struct level {
        blockptr_t blockptr;
        indirect_block block;
    } level;

    inode->block_count--;
    blockptr_t offset = inode->block_count;
    blockptr_t absolute_blockptr = 0;
    if (offset < INODE_DIRECT_BLOCKS) {
        absolute_blockptr = inode->data_direct[offset];

        // FIXME: just for debugging
        inode->data_direct[offset] = 0;
    } else if ((offset -= INODE_DIRECT_BLOCKS) < INODE_SINGLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = inode->data_single_indirect};
        read_block(level1.blockptr, &level1.block);
        absolute_blockptr = level1.block.blocks[offset];

        if (offset == 0) {
            free_blocks(&level1.blockptr, 1);

            // FIXME: just for debugging
            inode->data_single_indirect = 0;
        }
    } else if ((offset -= INODE_SINGLE_INDIRECT_BLOCKS) < INODE_DOUBLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = inode->data_double_indirect};
        read_block(level1.blockptr, &level1.block);

        level level2 = {.blockptr = level1.block.blocks[offset / INODE_SINGLE_INDIRECT_BLOCKS]};
        read_block(level2.blockptr, &level2.block);
        absolute_blockptr = level2.block.blocks[offset % INODE_SINGLE_INDIRECT_BLOCKS];

        if (offset == 0) {
            free_blocks(&level1.blockptr, 1);

            // FIXME: just for debugging
            inode->data_double_indirect = 0;
        }
        if (offset % INODE_SINGLE_INDIRECT_BLOCKS == 0) free_blocks(&level2.blockptr, 1);
    } else if ((offset -= INODE_DOUBLE_INDIRECT_BLOCKS) < INODE_TRIPLE_INDIRECT_BLOCKS) {
        level level1 = {.blockptr = inode->data_triple_indirect};
        read_block(level1.blockptr, &level1.block);

        level level2 = {.blockptr = level1.block.blocks[offset / INODE_DOUBLE_INDIRECT_BLOCKS]};
        read_block(level2.blockptr, &level2.block);

        level level3 = {.blockptr = level2.block.blocks[offset % INODE_DOUBLE_INDIRECT_BLOCKS]};
        read_block(level3.blockptr, &level3.block);
        absolute_blockptr = level3.block.blocks[offset % INODE_SINGLE_INDIRECT_BLOCKS];

        if (offset == 0) {
            free_blocks(&level1.blockptr, 1);

            // FIXME: just for debugging
            inode->data_triple_indirect = 0;
        }
        if (offset % INODE_DOUBLE_INDIRECT_BLOCKS == 0) free_blocks(&level2.blockptr, 1);
        if (offset % INODE_SINGLE_INDIRECT_BLOCKS == 0) free_blocks(&level3.blockptr, 1);
    } else {
        printf("free_last_inode_data_block: relative data offset out of bounds\n");
    }

    if (absolute_blockptr) free_blocks(&absolute_blockptr, 1);
    return 0;
}

// free entry in bitmap
int free_bitmap(blockptr_t index, blockptr_t bitmap_offset, blockptr_t bitmap_length) {
    blockptr_t offset = index / (BLOCK_SIZE * 8);
    if (offset > bitmap_length) {
        printf("free_bitmap: bitmap index out of bounds\n");
        return -EFAULT;
    }

    size_t inner_offset = index % 64;
    size_t entry = (index % (BLOCK_SIZE * 8)) / 64;

    blockptr_t blockptr = bitmap_offset + offset;
    bitmap_block ba;
    read_block(blockptr, &ba);
    ba.bitmap[entry] ^= 1UL << inner_offset;
    write_block(blockptr, &ba);

    return 0;
}

// free entry from directory
int free_dir_entry(inode_t* inode, const char* name) {
    if ((inode->mode & M_DIR) == 0) {
        printf("free_dir_entry: not a directory\n");
        return -ENOTDIR;
    } else if (strcmp(name, ".") == 0) {
        printf("free_dir_entry: can't free protected entry .\n");
        return -EPERM;
    } else if (strcmp(name, "..") == 0) {
        printf("free_dir_entry: can't free protected entry ..\n");
        return -EPERM;
    }

    dir_block dir_blocks[inode->block_count];
    read_inode_data_blocks(inode, dir_blocks);

    // search name in directory
    int entry_found = 0;
    size_t free_entry;
    blockptr_t free_entry_offset;
    for (blockptr_t offset = 0; offset < inode->block_count && !entry_found; offset++) {
        for (size_t entry = 0; entry < DIR_BLOCK_ENTRIES; entry++) {
            if (offset * DIR_BLOCK_ENTRIES + entry >= inode->atom_count) break;

            if (strcmp(dir_blocks[offset].entries[entry].name, name) == 0) {
                free_entry = entry;
                free_entry_offset = offset;
                entry_found = 1;
                break;
            }
        }
    }

    if (!entry_found) {
        printf("free_dir_entry: name does not exist in directory\n");
        return -ENOENT;
    }

    // get last entry offsets
    size_t last_entry = (inode->atom_count - 1) % DIR_BLOCK_ENTRIES;
    blockptr_t last_entry_offset = inode->block_count - 1;

    // copy last entry to removed entry if they are not the same
    if (free_entry != last_entry || free_entry_offset != last_entry_offset) {
        dir_block* free_block = &dir_blocks[free_entry_offset];
        free_block->entries[free_entry] = dir_blocks[last_entry_offset].entries[last_entry];
        write_inode_data_block(inode, free_entry_offset, free_block);
    }

    if (last_entry == 0) {
        // the whole last block can be deleted
        free_last_inode_data_block(inode);
    }

    inode->atom_count--;
    return 0;
}

// check if file exists
int file_exists(const char* path) {
    inodeptr_t inodeptr, parent_inodeptr;
    inode_t inode, parent_inode;
    int err = find_file_inode(path, &inodeptr, &inode, &parent_inodeptr, &parent_inode, NULL);
    if (err || inodeptr == 0) {
        return 0;
    } else {
        return 1;
    }
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

// copy mult * min(a, b) bytes from src to dest
void memcpy_min(void* dest, const void* src, size_t mult, size_t a, size_t b) {
    size_t min = a < b ? a : b;
    memcpy(dest, src, mult * min);
}

// unlink file (or directory if allow_dir is not 0)
int unlink_file_or_dir(const char* path, int allow_dir) {
    file f, parent;
    char name[MAX_FILENAME_LENGTH];
    int err = find_file_inode2(path, &f, &parent, name);
    if (err) return err;

    if (f.inodeptr == 0) {
        printf("unlink_file_or_dir: no such file or directory\n");
        return -ENOENT;
    } else if ((f.inode.mode & M_DIR) && !allow_dir) {
        printf("unlink_file_or_dir: is a directory\n");
        return -EISDIR;
    } else if ((f.inode.mode & M_DIR) && f.inode.atom_count > 2) {
        printf("unlink_file_or_dir: directory is not empty\n");
        return -ENOTEMPTY;
    }

    f.inode.link_count--;
    free_dir_entry(&parent.inode, name);
    write_inode(parent.inodeptr, &parent.inode);
    free_inode(f.inodeptr, &f.inode);

    return 0;
}
