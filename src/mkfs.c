#include <stdio.h>
#include <stdlib.h>

#include "blocks.h"
#include "syscalls.h"
#include "types.h"
#include "vm.h"

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        printf("usage: mkfs.stzfs <device> [inode_count]\n");
        return 1;
    }

    off_t size = vm_config_set_file(argv[1]);

    inodeptr_t inode_count;
    if (argc == 3) {
        inode_count = strtol(argv[2], NULL, 10);
    } else {
        inode_count = (size / sizeof(inode_t)) / 8;
    }
    stzfs_makefs(inode_count);

    return 0;
}
