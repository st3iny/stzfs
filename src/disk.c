#include "disk.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "error.h"
#include "log.h"
#include "types.h"

// use mmap instead of direct io
#define DISK_USE_MMAP 0
#if DISK_USE_MMAP
#include <sys/mman.h>
#include <string.h>

static void* fp = NULL;
#endif

// global vars
static int fd = -1; // file descriptor
static long long size = -1; // total virtual hard disk size

// create new disk file with given size
// TODO: this method is not really needed
stzfs_error_t disk_create_file(const char* path, off_t size) {
    const int oflag = O_CREAT | O_WRONLY;
    const mode_t mode = S_IRUSR | S_IWUSR;

    fd = open(path, oflag, mode);
    if (fd == -1) {
        LOG("error while trying to create disk file");
        return ERROR;
    }

    ftruncate(fd, size);
    close(fd);
    return SUCCESS;
}

// open disk file
stzfs_error_t disk_set_file(const char* path) {
    if (fd != -1) {
        LOG("closing previos vm file")
        close(fd);
    }

    const int oflag = O_RDWR;
    fd = open(path, oflag);
    if (fd == -1) {
        LOG("error while trying to open disk file");
        return ERROR;
    }

    struct stat st;
    fstat(fd, &st);
    size = st.st_size;

#if DISK_USE_MMAP
    fp = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
#endif

    return SUCCESS;
}

// write to disk file
stzfs_error_t disk_write(off_t addr, const void* buffer, size_t length) {
#if DISK_USE_MMAP
    if (fp == NULL) {
#else
    if (fd == -1) {
#endif
        LOG("disk file not open");
        return ERROR;
    }

    if (addr + length > size) {
        LOG("out of bounds while trying to write to disk file");
        return ERROR;
    }

#if DISK_USE_MMAP
    memcpy(fp + addr, buffer, length);
#else
    // seek and write to disk file
    lseek(fd, addr, SEEK_SET);
    write(fd, buffer, length);
#endif

    return SUCCESS;
}

// read from disk file
stzfs_error_t disk_read(off_t addr, void* buffer, size_t length) {
#if DISK_USE_MMAP
    if (fp == NULL) {
#else
    if (fd == -1) {
#endif
        LOG("disk file not open");
        return ERROR;
    }

    if (addr + length > size) {
        LOG("out of bounds while trying to read from disk file");
        return ERROR;
    }

#if DISK_USE_MMAP
    memcpy(buffer, fp + addr, length);
#else
    // seek and read from disk file
    lseek(fd, addr, SEEK_SET);
    read(fd, buffer, length);
#endif

    return SUCCESS;
}

// get disk file size
off_t disk_get_size(void) {
    return size;
}

// get disk file descriptor
int disk_get_fd(void) {
    return fd;
}

// close disk file
void disk_close(void) {
#if DISK_USE_MMAP
    munmap(fp, size);
#endif
    close(fd);
}
