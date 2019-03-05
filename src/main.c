#define FUSE_USE_VERSION 34
#include <fuse3/fuse.h>
#include <math.h>
#include <stdio.h>

#include "syscalls.h"
#include "types.h"
#include "vm.h"

int main() {
    // definitions
    const blockptr_t blocks = 2 * 256 * 1024;
    const inodeptr_t inodes = (const inodeptr_t)pow(2, 20);

    // create vm hdd
    vm_config_create_file(VM_HDD_PATH, blocks * BLOCK_SIZE);
    off_t size = vm_config_set_file(VM_HDD_PATH);

    // create filesystem
    sys_makefs(inodes);

    // create some files
    struct fuse_file_info file_info;
    sys_create("/hello.world", 0, &file_info);
    sys_create("/foo.bar", 0, &file_info);
    sys_create("/some file", 0, &file_info);

    return 0;
}
