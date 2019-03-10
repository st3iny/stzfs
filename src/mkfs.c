#include <stdio.h>

#include "syscalls.h"
#include "vm.h"

int main(int argc, char** argv) {
    if (argc <= 1) {
        printf("usage: mkfs.stzfs <device>\n");
        return 1;
    }

    vm_config_set_file(argv[1]);
    stzfs_makefs(1048576);

    return 0;
}
