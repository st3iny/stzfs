add_executable(filesystem main.c syscalls.h syscalls.c blocks.h types.h vm.h vm.c)
add_executable(utils utils.c utils.h vm.h vm.c blocks.h types.h)
add_executable(mkfs.stzfs mkfs.c syscalls.h syscalls.c blocks.h types.h vm.c vm.h)
