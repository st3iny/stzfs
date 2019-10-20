#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vm.h"
#include "types.h"

#define VM_LOG(msg) printf("VM: %s\n", msg)

// use mmap instead of direct io
#define VM_USE_MMAP 0
#if VM_USE_MMAP
#include <sys/mman.h>
#include <string.h>

static void* fp = NULL;
#endif

// global vars
static int fd = -1; // file descriptor
static long size = -1; // total virtual hard disk size

// config
int vm_config_create_file(const char* path, long length) {
    int oflag = O_CREAT | O_WRONLY;
    mode_t mode = S_IRUSR | S_IWUSR;
    fd = open(path, oflag, mode);
    if (fd == -1) {
        VM_LOG("Error while trying to create virtual hard disk.");
        return -1;
    }

    ftruncate(fd, length);
    close(fd);
    return 0;
}


off_t vm_config_set_file(const char* path) {
    if (fd != -1) {
        close(fd);
    }

    int oflag = O_RDWR;
    fd = open(path, oflag);
    if (fd == -1) {
        VM_LOG("Error while trying to open virtual hard disk.");
        return -1;
    }

    struct stat st;
    fstat(fd, &st);
    size = st.st_size;

#if VM_USE_MMAP
    fp = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
#endif

    return size;
}

// getters
int vm_get_fd(void) {
    return fd;
}

// low level methods
int vm_write(off_t addr, const void* buffer, size_t length) {
#if VM_USE_MMAP
    if (fp == NULL) {
#else
    if (fd == -1) {
#endif
        VM_LOG("Virtual hard disk not created.");
        return -1;
    }
    if (addr + length > size) {
        VM_LOG("Out of bounds while trying to write to virtual hard disk.");
        return -1;
    }

#if VM_USE_MMAP
    memcpy(fp + addr, buffer, length);
    return length;
#else
    // seek and write to virtual hard disk
    lseek(fd, addr, SEEK_SET);
    return write(fd, buffer, length);
#endif
}

int vm_read(off_t addr, void* buffer, size_t length) {
#if VM_USE_MMAP
    if (fp == NULL) {
#else
    if (fd == -1) {
#endif
        VM_LOG("Virtual hard disk not created.");
        return -1;
    }
    if (addr + length > size) {
        VM_LOG("Out of bounds while trying to read from virtual hard disk.");
        return -1;
    }

#if VM_USE_MMAP
    memcpy(buffer, fp + addr, length);
    return length;
#else
    // seek and read from virtual hard disk
    lseek(fd, addr, SEEK_SET);
    return read(fd, buffer, length);
#endif
}

off_t vm_size() {
    return size;
}

// destroy vm
void vm_destroy(void) {
#if VM_USE_MMAP
    munmap(fp, size);
#else
    close(fd);
#endif
}
