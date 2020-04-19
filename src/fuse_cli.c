#include <stdio.h>
#include <stdlib.h>

#include "disk.h"
#include "fuse.h"
#include "stzfs.h"

void print_usage(void) {
    printf("usage: stzfs <disk> <mountpoint> [options]\n");
}

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage();
        return 1;
    }

    // extract device
    char* disk = argv[1];
    char** argv_new = (char**)malloc(argc * sizeof(char**));
    argv_new[0] = argv[0];
    for (int i = 2; i < argc; i++) {
        argv_new[i - 1] = argv[i];
    }

    // run fuse
    printf("mounting %s at %s\n", disk, argv[2]);
    disk_set_file(disk);
    struct fuse_args args = FUSE_ARGS_INIT(argc - 1, argv_new);
    fuse_opt_parse(&args, NULL, NULL, NULL);
    fuse_opt_add_arg(&args, "-s");
    int ret = fuse_main(args.argc, args.argv, &stzfs_ops, NULL);

    // cleanup fuse
    fuse_opt_free_args(&args);
    return ret;
}
