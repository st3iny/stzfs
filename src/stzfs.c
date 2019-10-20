#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "alloc.h"
#include "bitmap_cache.h"
#include "blocks.h"
#include "find.h"
#include "free.h"
#include "fuse.h"
#include "helpers.h"
#include "inode.h"
#include "read.h"
#include "stzfs.h"
#include "vm.h"
#include "write.h"

// overwrite root dir permissions
#define STZFS_MOUNT_AS_USER 1

// show .. entry in root dir
#define STZFS_SHOW_DOUBLE_DOTS_IN_ROOT_DIR 1

// fuse operations
struct fuse_operations stzfs_ops = {
    .init = stzfs_fuse_init,
    .destroy = stzfs_destroy,
    .create = stzfs_create,
    .rename = stzfs_rename,
    .unlink = stzfs_unlink,
    .getattr = stzfs_getattr,
    .open = stzfs_open,
    .read = stzfs_read,
    .write = stzfs_write,
    .mkdir = stzfs_mkdir,
    .rmdir = stzfs_rmdir,
    .readdir = stzfs_readdir,
    .statfs = stzfs_statfs,
    .chown = stzfs_chown,
    .chmod = stzfs_chmod,
    .truncate = stzfs_truncate,
    .utimens = stzfs_utimens,
    .link = stzfs_link,
    .symlink = stzfs_symlink,
    .readlink = stzfs_readlink
};

// init filesystem
blockptr_t stzfs_makefs(inodeptr_t inode_count) {
    blockptr_t blocks = vm_size() / BLOCK_SIZE;
    printf("stzfs_makefs: creating file system with %i blocks and %i inodes\n", blocks, inode_count);

    // calculate bitmap and inode table lengths
    blockptr_t block_bitmap_length = DIV_CEIL(blocks, BLOCK_SIZE * 8);
    inodeptr_t inode_table_length = DIV_CEIL(inode_count, BLOCK_SIZE / sizeof(inode_t));
    inodeptr_t inode_bitmap_length = DIV_CEIL(inode_count, BLOCK_SIZE * 8);

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

    // init filesystem
    stzfs_init();

    // write root directory block
    dir_block root_dir_block;
    memset(&root_dir_block, 0, BLOCK_SIZE);
    root_dir_block.entries[0] = (dir_block_entry) {.name = ".", .inode = 1};
    blockptr_t root_dir_block_ptr;
    alloc_block(&root_dir_block_ptr, &root_dir_block);
    printf("stzfs_makefs: wrote root dir block at %i\n", root_dir_block_ptr);

    // create root inode
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    inode_t root_inode;
    memset(&root_inode, 0, INODE_SIZE);
    root_inode.mode = M_RU | M_WU | M_XU | M_RG | M_XG | M_RO | M_XO | M_DIR;
    root_inode.uid = 0;
    root_inode.gid = 0;
    root_inode.atime = now;
    root_inode.mtime = now;
    root_inode.ctime = now;
    root_inode.link_count = 1;
    root_inode.atom_count = 1;
    root_inode.block_count = 1;
    root_inode.data_direct[0] = root_dir_block_ptr;

    // write root inode
    inodeptr_t root_inode_ptr = alloc_inode(&root_inode);
    printf("stzfs_makefs: wrote root inode with id %i\n", root_inode_ptr);

    return blocks;
}

// init filesystem from fuse
void* stzfs_fuse_init(struct fuse_conn_info* conn, struct fuse_config* cfg) {
    cfg->kernel_cache = 1;
    cfg->use_ino = 1;

    stzfs_init();

    return NULL;
}

// low level filesystem init (has to be called manually if fuse is not used)
void stzfs_init(void) {
    bitmap_cache_init();
}

// clean up filesystem
void stzfs_destroy(void* private_data) {
    vm_destroy();
}

// get file stats
int stzfs_getattr(const char* path, struct stat* st, struct fuse_file_info* file_info) {
    // TODO: reuse inode
    if (!file_exists(path)) {
        // spams stdout
        // printf("stzfs_getattr: no such file\n");
        return -ENOENT;
    }

    file f;
    int err = find_file_inode2(path, &f, NULL, NULL);
    if (err) return err;

    if ((f.inode.mode & M_DIR)) {
        st->st_size = f.inode.atom_count * sizeof(dir_block_entry);
    } else {
        st->st_size = f.inode.atom_count;
    }

    st->st_ino = f.inodeptr;
    st->st_mode = mode_stzfs_to_posix(f.inode.mode);
    st->st_nlink = f.inode.link_count;
#if STZFS_MOUNT_AS_USER
    if (strcmp(path, "/") == 0) {
        st->st_uid = getuid();
        st->st_gid = getgid();
    } else {
#endif
    st->st_uid = f.inode.uid;
    st->st_gid = f.inode.gid;
#if STZFS_MOUNT_AS_USER
    }
#endif
    st->st_atim = f.inode.atime;
    st->st_mtim = f.inode.mtime;
    st->st_ctim = f.inode.ctime;

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
    }

    // open exsiting file
    file_info->fh = inodeptr;

    // update timestamps
    touch_atime(&inode);
    write_inode(inodeptr, &inode);

    return 0;
}

// read from a file
int stzfs_read(const char* file_path, char* buffer, size_t length, off_t offset,
               struct fuse_file_info* file_info) {
    if (length == 0)  {
        printf("stzfs_read: zero length read\n");
        return 0;
    }

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

    // update timestamps
    touch_atime(&inode);
    write_inode(inodeptr, &inode);

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
    if (length == 0)  {
        printf("stzfs_write: zero length write\n");
        return 0;
    }

    if (file_info->fh == 0) {
        printf("stzfs_write: invald file handle (inode)\n");
        return -EFAULT;
    }

    inodeptr_t inodeptr = file_info->fh;
    inode_t inode;
    read_inode(inodeptr, &inode);

    // check file size limits
    const uint64_t new_atom_count = MAX(offset + length, inode.atom_count);
    const blockptr_t new_block_count = DIV_CEIL(new_atom_count, BLOCK_SIZE);
    if (new_block_count > INODE_MAX_BLOCKS) {
        printf("stzfs_write: max file size exceeded\n");
        return -EFBIG;
    }

    // check continuity
    if (offset > inode.atom_count) {
        printf("stzfs_write: offset too high\n");
        return -EPERM;
    }

    // update timestamps
    touch_atime(&inode);
    touch_mtime_and_ctime(&inode);

    size_t written_bytes = 0;

    // write first partial block
    const blockptr_t first_blockptr = offset / BLOCK_SIZE;
    const blockptr_t last_blockptr = new_block_count - 1;
    const size_t initial_byte_offset = offset % BLOCK_SIZE;
    blockptr_t blockptr = first_blockptr;
    if (initial_byte_offset > 0) {
        data_block block;
        if (blockptr >= inode.block_count) {
            memset(&block, 0, BLOCK_SIZE);
        } else {
            read_inode_data_block(&inode, blockptr, &block);
        }

        // keep block boundaries
        written_bytes = BLOCK_SIZE - initial_byte_offset;
        if (written_bytes > length) {
            written_bytes = length;
        }

        memcpy(&block.data[initial_byte_offset], buffer, written_bytes);
        write_or_alloc_inode_data_block(&inode, blockptr, &block);
        blockptr++;
    }

    // write aligned full blocks
    for (; blockptr < new_block_count && (length - written_bytes) >= BLOCK_SIZE; blockptr++) {
        write_or_alloc_inode_data_block(&inode, blockptr, &buffer[written_bytes]);
        written_bytes += BLOCK_SIZE;
    }

    // write final partial block
    const size_t diff = length - written_bytes;
    if (diff > 0) {
        data_block block;

        if (last_blockptr < inode.block_count) {
            read_inode_data_block(&inode, last_blockptr, &block);
        } else {
            memset(&block.data[diff], 0, BLOCK_SIZE - diff);
        }

        memcpy(&block, &buffer[written_bytes], diff);
        write_or_alloc_inode_data_block(&inode, last_blockptr, &block);
        written_bytes += diff;
    }

    inode.atom_count += length;
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
    }

    // update timestamps
    touch_mtime_and_ctime(&parent_inode);

    struct fuse_context* context = fuse_get_context();

    // create new file
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    inode.mode = mode_posix_to_stzfs(mode);
    inode.uid = context->uid;
    inode.gid = context->gid;
    inode.atime = now;
    inode.mtime = now;
    inode.ctime = now;
    inode.link_count = 1;

    inodeptr = alloc_inode(&inode);
    alloc_dir_entry(&parent_inode, last_name, inodeptr);
    write_inode(parent_inodeptr, &parent_inode);

    file_info->fh = inodeptr;
    return 0;
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

    // update timestamps
    touch_atime(&src.inode);
    touch_ctime(&src.inode);

    if (dst_exists) {
        touch_atime(&dst.inode);
        touch_ctime(&dst.inode);
    }

    touch_mtime_and_ctime(&src_parent.inode);

    if (src_parent.inodeptr != dst_parent.inodeptr) {
        touch_mtime_and_ctime(&dst_parent.inode);
    }

    // TODO: update parent dir pointer if src is a dir
    // TODO: check inode bounds
    // replace dst with src
    src.inode.link_count++;
    touch_ctime(&src.inode);
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

    // rewrite double dot inodeptr
    if ((src.inode.mode & M_DIR) && src_parent.inodeptr != dst_parent.inodeptr) {
        dst_parent.inode.link_count++;
        write_inode(dst_parent.inodeptr, &dst_parent.inode);

        write_dir_entry(&src.inode, "..", dst_parent.inodeptr);

        src_parent.inode.link_count--;
        write_inode(src_parent.inodeptr, &src_parent.inode);
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

    if (parent.inodeptr == 0) {
        printf("stzfs_mkdir: parent not existing\n");
        return -ENOENT;
    } else if (dir.inodeptr != 0) {
        printf("stzfs_mkdir: file or directory exists\n");
        return -EEXIST;
    }

    super_block sb;
    read_super_block(&sb);

    if (sb.free_blocks == 0) {
        printf("stzfs_mkdir: no free block available\n");
        return -ENOSPC;
    } else if (sb.free_inodes == 0) {
        printf("stzfs_mkdir: no free inode available\n");
        return -ENOSPC;
    }

    // update timestamps
    touch_mtime_and_ctime(&parent.inode);

    // allocate inode in bitmap
    dir.inodeptr = alloc_inodeptr();

    // allocate and write directory block
    dir_block block;
    memset(&block, 0, BLOCK_SIZE);
    block.entries[0] = (dir_block_entry) {.name = ".", .inode=dir.inodeptr};
    block.entries[1] = (dir_block_entry) {.name = "..", .inode=parent.inodeptr};
    blockptr_t blockptr;
    alloc_block(&blockptr, &block);

    // TODO: check inode bounds
    // increase parent inode link counter
    parent.inode.link_count++;
    write_inode(parent.inodeptr, &parent.inode);

    struct fuse_context* context = fuse_get_context();

    // create and write inode
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    dir.inode.mode = mode_posix_to_stzfs(mode | S_IFDIR);
    dir.inode.uid = context->uid;
    dir.inode.gid = context->gid;
    dir.inode.link_count = 2;
    dir.inode.atom_count = 2;
    dir.inode.block_count = 1;
    dir.inode.atime = now;
    dir.inode.mtime = now;
    dir.inode.ctime = now;
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

    // update timestamps
    touch_atime(&dir.inode);
    write_inode(dir.inodeptr, &dir.inode);

#if STZFS_SHOW_DOUBLE_DOTS_IN_ROOT_DIR
    if (strcmp(path, "/") == 0) {
        filler(buffer, "..", NULL, 0, 0);
    }
#endif

    for (blockptr_t offset = 0; offset < dir.inode.block_count; offset++) {
        dir_block block;
        const blockptr_t blockptr = read_inode_data_block(&dir.inode, offset, &block);
        if (blockptr == 0) {
            printf("stzfs_readdir: can't read directory block\n");
            return -EFAULT;
        }

        const size_t remaining_entries = dir.inode.atom_count - offset * DIR_BLOCK_ENTRIES;
        const size_t entries = MIN(DIR_BLOCK_ENTRIES, remaining_entries);
        for (size_t entry = 0; entry < entries; entry++) {
            filler(buffer, (const char*)block.entries[entry].name, NULL, 0, 0);
        }
    }

    return 0;
}

// retrieve filesystem stats
int stzfs_statfs(const char* path, struct statvfs* stat) {
    super_block sb;
    read_super_block(&sb);

    stat->f_bsize = BLOCK_SIZE;
    stat->f_frsize = BLOCK_SIZE;
    stat->f_blocks = sb.block_count;
    stat->f_bfree = sb.free_blocks;
    stat->f_bavail = sb.free_blocks;
    stat->f_files = sb.inode_count;
    stat->f_ffree = sb.free_inodes;
    stat->f_namemax = MAX_FILENAME_LENGTH;

    return 0;
}

// change file owner
int stzfs_chown(const char* path, uid_t uid, gid_t gid, struct fuse_file_info* fi) {
    file f;
    if (fi == NULL) {
        find_file_inode2(path, &f, NULL, NULL);
    } else {
        f.inodeptr = fi->fh;
        read_inode(f.inodeptr, &f.inode);
    }

    if (f.inodeptr == 0) {
        printf("stzfs_chown: no such file\n");
        return -ENOENT;
    }

    // update timestamps
    touch_atime(&f.inode);
    touch_ctime(&f.inode);

    f.inode.uid = uid;
    f.inode.gid = gid;
    write_inode(f.inodeptr, &f.inode);

    return 0;
}

// change file permissions
int stzfs_chmod(const char* path, mode_t mode, struct fuse_file_info* fi) {
    file f;
    if (fi == NULL) {
        find_file_inode2(path, &f, NULL, NULL);
    } else {
        f.inodeptr = fi->fh;
        read_inode(f.inodeptr, &f.inode);
    }

    if (f.inodeptr == 0) {
        printf("stzfs_chmod: no such file\n");
        return -ENOENT;
    }

    // update timestamps
    touch_atime(&f.inode);
    touch_ctime(&f.inode);

    f.inode.mode = mode_posix_to_stzfs(mode);
    write_inode(f.inodeptr, &f.inode);

    return 0;
}

// truncate file
int stzfs_truncate(const char* path, off_t offset, struct fuse_file_info* fi) {
    file f;
    if (fi == NULL) {
        find_file_inode2(path, &f, NULL, NULL);
    } else {
        f.inodeptr = fi->fh;
        read_inode(f.inodeptr, &f.inode);
    }

    if (f.inodeptr == 0) {
        printf("stzfs_truncate: no such file\n");
        return -ENOENT;
    }

    if (offset > f.inode.atom_count) {
        printf("stzfs_truncate: offset out of bounds\n");
        return -EPERM;
    }

    if (offset != f.inode.atom_count) {
        f.inode.atom_count = offset;
        touch_mtime_and_ctime(&f.inode);
    }

    blockptr_t block_offset = DIV_CEIL(offset, BLOCK_SIZE);
    if (block_offset < f.inode.block_count) {
        free_inode_data_blocks(&f.inode, block_offset);
    }

    touch_atime(&f.inode);
    write_inode(f.inodeptr, &f.inode);

    return 0;
}

// change access and modification times of a file
int stzfs_utimens(const char* path, const struct timespec tv[2], struct fuse_file_info* fi) {
    file f;
    if (fi == NULL) {
        find_file_inode2(path, &f, NULL, NULL);
    } else {
        f.inodeptr = fi->fh;
        read_inode(f.inodeptr, &f.inode);
    }

    if (f.inodeptr == 0) {
        printf("stzfs_utimens: no such file\n");
        return -ENOENT;
    }

    f.inode.atime = tv[0];
    f.inode.mtime = tv[1];
    touch_ctime(&f.inode);
    write_inode(f.inodeptr, &f.inode);

    return 0;
}

// create a hard link to a file
// TODO: improve me
int stzfs_link(const char* src, const char* dest) {
    if (!file_exists(src)) {
        printf("stzfs_link: no such file\n");
        return -ENOENT;
    }

    if (file_exists(dest)) {
        printf("stzfs_link: dest already existing\n");
        return -EEXIST;
    }

    file src_file;
    find_file_inode2(src, &src_file, NULL, NULL);

    file dest_file, dest_parent;
    char dest_last_name[MAX_FILENAME_LENGTH];
    find_file_inode2(dest, &dest_file, &dest_parent, dest_last_name);

    // update timestamps
    touch_atime(&src_file.inode);
    touch_ctime(&src_file.inode);
    touch_mtime_and_ctime(&dest_parent.inode);

    src_file.inode.link_count++;
    write_inode(src_file.inodeptr, &src_file.inode);

    alloc_dir_entry(&dest_parent.inode, dest_last_name, src_file.inodeptr);
    write_inode(dest_parent.inodeptr, &dest_parent.inode);

    return 0;
}

// create symbolic link
int stzfs_symlink(const char* target, const char* link_name) {
    if (file_exists(link_name)) {
        printf("stzfs_symlink: link name already existing\n");
        return -EEXIST;
    }

    file symlink, symlink_parent;
    char symlink_last_name[MAX_FILENAME_LENGTH];
    find_file_inode2(link_name, &symlink, &symlink_parent, symlink_last_name);

    // update timestamps
    touch_mtime_and_ctime(&symlink_parent.inode);

    // create new file
    struct fuse_context* context = fuse_get_context();
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    symlink.inode.mode = M_LNK;
    symlink.inode.uid = context->uid;
    symlink.inode.gid = context->gid;
    symlink.inode.atime = now;
    symlink.inode.mtime = now;
    symlink.inode.ctime = now;
    symlink.inode.link_count = 1;

    symlink.inodeptr = alloc_inode(&symlink.inode);
    alloc_dir_entry(&symlink_parent.inode, symlink_last_name, symlink.inodeptr);
    write_inode(symlink_parent.inodeptr, &symlink_parent.inode);

    // write target to symbolic link data blocks
    const size_t target_length = strlen(target);
    const size_t buffer_length = DIV_CEIL(sizeof(char) * target_length, BLOCK_SIZE) * BLOCK_SIZE;
    void* buffer = malloc(buffer_length);
    memcpy(buffer, target, target_length);
    memset(buffer + target_length, 0, buffer_length - target_length);

    for (size_t offset = 0; offset < buffer_length; offset += BLOCK_SIZE) {
        alloc_inode_data_block(&symlink.inode, buffer + offset);
    }
    symlink.inode.atom_count = target_length;
    write_inode(symlink.inodeptr, &symlink.inode);
    free(buffer);

    return 0;
}

// read symbolic link target
int stzfs_readlink(const char* path, char* buffer, size_t length) {
    if (!file_exists(path)) {
        printf("stzfs_readlink: no such file\n");
        return -ENOENT;
    }

    file symlink;
    find_file_inode2(path, &symlink, NULL, NULL);

    if (!M_IS_LNK(symlink.inode.mode)) {
        printf("stzfs_readlink: not a symbolic link\n");
        return -EINVAL;
    }

    touch_atime(&symlink.inode);
    write_inode(symlink.inodeptr, &symlink.inode);

    data_block data_blocks[symlink.inode.block_count];
    read_inode_data_blocks(&symlink.inode, data_blocks, symlink.inode.block_count, 0);

    const size_t data_length = MIN(length - 1, symlink.inode.atom_count);
    memcpy(buffer, data_blocks, data_length);
    buffer[data_length] = '\0';

    return 0;
}
