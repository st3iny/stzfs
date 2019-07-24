#include <stdio.h>
#include <stdlib.h>

#include "fuse.h"
#include "syscalls.h"
#include "vm.h"

void print_usage(void) {
    printf("usage: stzfs <device> <dir>\n");
}

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage();
        return 1;
    }

    // extract device
    char* device = NULL;
    char** argv_new = (char**)malloc(argc * sizeof(char**));
    argv_new[0] = argv[0];
    int index = 1;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-' && device == NULL) {
            device = argv[i];
            continue;
        }
        argv_new[index] = argv[i];
        index++;
    }

    if (device == NULL) {
        print_usage();
        return 0;
    }

    // run fuse
    printf("mounting %s\n", device);
    vm_config_set_file(device);
    struct fuse_args args = FUSE_ARGS_INIT(index, argv_new);
    int ret = fuse_main(args.argc, args.argv, &stzfs_ops, NULL);

    // cleanup fuse
    fuse_opt_free_args(&args);
    return ret;
}
