#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "stzfs.h"
#include "disk.h"

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        printf("usage: mkfs.stzfs <device> [bytes_per_inode]\n");
        return 1;
    }

    if (disk_set_file(argv[1])) {
        return 1;
    }

    long int bytes_per_inode = 16384;
    if (argc == 3) {
        bytes_per_inode = strtol(argv[2], NULL, 10);
    }

    const off_t size = disk_get_size();
    stzfs_makefs(size / bytes_per_inode);

    return 0;
}
