#include <stdio.h>
#include <stdlib.h>

#include "blocks.h"
#include "syscalls.h"
#include "types.h"
#include "vm.h"

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        printf("usage: mkfs.stzfs <device> [bytes_per_inode]\n");
        return 1;
    }

    off_t size = vm_config_set_file(argv[1]);

    long int bytes_per_inode = 16384;
    if (argc == 3) {
        bytes_per_inode = strtol(argv[2], NULL, 10);
    }
    stzfs_makefs(size / bytes_per_inode);

    return 0;
}
