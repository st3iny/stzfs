#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vm.h"
#include "types.h"

#define VM_LOG(msg) printf("VM: %s\n", msg)

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
    return size;
}

// low level methods
int vm_write(off_t addr, const void* buffer, size_t length) {
    if (fd == -1) {
        VM_LOG("Virtual hard disk not created.");
        return -1;
    }
    if (addr + length > size) {
        VM_LOG("Out of bounds while trying to write to virtual hard disk.");
        return -1;
    }

    // seek and write to virtual hard disk
    lseek(fd, addr, SEEK_SET);
    return write(fd, buffer, length);
}

int vm_read(off_t addr, void* buffer, size_t length) {
    if (fd == -1) {
        VM_LOG("Virtual hard disk not created.");
        return -1;
    }
    if (addr + length > size) {
        VM_LOG("Out of bounds while trying to read from virtual hard disk.");
        return -1;
    }

    // seek and read from virtual hard disk
    lseek(fd, addr, SEEK_SET);
    return read(fd, buffer, length);
}

off_t vm_size() {
    return size;
}
