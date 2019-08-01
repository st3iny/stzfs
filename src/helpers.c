#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "alloc.h"
#include "find.h"
#include "free.h"
#include "helpers.h"
#include "read.h"
#include "write.h"

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

// update inode atime
void touch_atime(inode_t* inode) {
    clock_gettime(CLOCK_REALTIME, &inode->atime);
}

// update inode mtime and ctime
void touch_mtime_and_ctime(inode_t* inode) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    inode->mtime = now;
    inode->ctime = now;
}

// update inode ctime
void touch_ctime(inode_t* inode) {
    clock_gettime(CLOCK_REALTIME, &inode->ctime);
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

    // update timestamps
    touch_atime(&f.inode);
    touch_ctime(&f.inode);
    touch_mtime_and_ctime(&parent.inode);

    if ((f.inode.mode & M_DIR)) {
        parent.inode.link_count--;
    }

    free_dir_entry(&parent.inode, name);
    write_inode(parent.inodeptr, &parent.inode);

    f.inode.link_count--;
    if (f.inode.link_count == 0) {
        free_inode(f.inodeptr, &f.inode);
    } else {
        write_inode(f.inodeptr, &f.inode);
    }

    return 0;
}

// convert posix file mode to stzfs
stzfs_mode_t mode_posix_to_stzfs(mode_t mode) {
    stzfs_mode_t stzfs_mode = 0;

    // file type
    if      (S_ISREG(mode)) stzfs_mode |= M_REG;
    else if (S_ISLNK(mode)) stzfs_mode |= M_LNK;
    else if (S_ISDIR(mode)) stzfs_mode |= M_DIR;
    else    printf("mode_posix_to_stzfs: invalid file type\n");

    // user permissions
    if (mode & S_IRUSR) stzfs_mode |= M_RU;
    if (mode & S_IWUSR) stzfs_mode |= M_WU;
    if (mode & S_IXUSR) stzfs_mode |= M_XU;

    // group permissions
    if (mode & S_IRGRP) stzfs_mode |= M_RG;
    if (mode & S_IWGRP) stzfs_mode |= M_WG;
    if (mode & S_IXGRP) stzfs_mode |= M_XG;

    // other permissions
    if (mode & S_IROTH) stzfs_mode |= M_RO;
    if (mode & S_IWOTH) stzfs_mode |= M_WO;
    if (mode & S_IXOTH) stzfs_mode |= M_XO;

    // special bits
    if (mode & S_ISUID) stzfs_mode |= M_SETUID;
    if (mode & S_ISGID) stzfs_mode |= M_SETGID;
    if (mode & S_ISVTX) stzfs_mode |= M_STICKY;

    return stzfs_mode;
}

// convert stzfs file mode to posix
mode_t mode_stzfs_to_posix(stzfs_mode_t stzfs_mode) {
    mode_t mode = 0;

    // file type
    if      (M_IS_REG(stzfs_mode)) mode |= S_IFREG;
    else if (M_IS_LNK(stzfs_mode)) mode |= S_IFLNK;
    else if (M_IS_DIR(stzfs_mode)) mode |= S_IFDIR;
    else    printf("mode_stzfs_to_posix: invalid file type\n");

    // user permissions
    if (stzfs_mode & M_RU) mode |= S_IRUSR;
    if (stzfs_mode & M_WU) mode |= S_IWUSR;
    if (stzfs_mode & M_XU) mode |= S_IXUSR;

    // group permissions
    if (stzfs_mode & M_RG) mode |= S_IRGRP;
    if (stzfs_mode & M_WG) mode |= S_IWGRP;
    if (stzfs_mode & M_XG) mode |= S_IXGRP;

    // other permissions
    if (stzfs_mode & M_RO) mode |= S_IROTH;
    if (stzfs_mode & M_WO) mode |= S_IWOTH;
    if (stzfs_mode & M_XO) mode |= S_IXOTH;

    // special bits
    if (stzfs_mode & M_SETUID) mode |= S_ISUID;
    if (stzfs_mode & M_SETGID) mode |= S_ISGID;
    if (stzfs_mode & M_STICKY) mode |= S_ISVTX;

    return mode;
}
