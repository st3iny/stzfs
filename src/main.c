#define FUSE_USE_VERSION 34
#include <fcntl.h>
#include <fuse3/fuse.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>

#include "syscalls.h"
#include "types.h"
#include "vm.h"

int main() {
    // definitions
    const blockptr_t blocks = 4 * 256 * 1024;
    const inodeptr_t inodes = (const inodeptr_t)pow(2, 20);

    // create vm hdd
    vm_config_create_file(VM_HDD_PATH, (long long)blocks * BLOCK_SIZE);
    off_t size = vm_config_set_file(VM_HDD_PATH);

    // create filesystem
    sys_makefs(inodes);

    // create some files
    struct fuse_file_info file_info;
    sys_create("/hello.world", 0, &file_info);
    sys_release("/hello.world", &file_info);

    sys_create("/foo.bar", 0, &file_info);
    sys_release("/foo.bar", &file_info);

    sys_create("/some_file", 0, &file_info);

    // write data to a file
    char buffer[1024 * BLOCK_SIZE];
    size_t len;
    long offset = 0;
    int fd = open("/tmp/bigfile", O_RDONLY);
    while ((len = read(fd, buffer, sizeof(buffer))) != 0) {
        // printf("offset = %lu, len = %i\n", offset, len);
        sys_write("/some_file", buffer, len, offset, &file_info);
        offset += len;
    }
    close(fd);

    // rename some files
    sys_rename("/some_file", "/bigfile", 0);
    sys_rename("/foo.bar", "/hello.world", 0);

    // read data from file
    offset = 0;
    fd = open("/tmp/bigfile.comp", O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR);
    while ((len = sys_read("/bigfile", buffer, sizeof(buffer), offset, &file_info)) != 0) {
        // printf("offset = %lu, len = %i\n", offset, len);
        write(fd, buffer, len);
        offset += len;
    }
    close(fd);

    return 0;
}
