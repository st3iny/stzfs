#define FUSE_USE_VERSION 34
#include <fuse3/fuse.h>

#include "syscalls.h"
#include "vm.h"

int main(int argc, char** argv) {
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	// parse options
    /*
	if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1) {
		return 1;
    }
    */

    vm_config_set_file("/tmp/vm-hdd.dat");
	int ret = fuse_main(args.argc, args.argv, &sys_ops, NULL);
	fuse_opt_free_args(&args);
	return ret;
}
