add_executable(filesystem main.c syscalls.h syscalls.c blocks.h types.h vm.h vm.c)
target_link_libraries(filesystem fuse3)

add_executable(stzfs stzfs.c syscalls.h syscalls.c blocks.h types.h vm.h vm.c)
target_link_libraries(stzfs fuse3)

add_executable(utils utils.c utils.h vm.h vm.c blocks.h types.h)
target_link_libraries(utils fuse3)

add_executable(mkfs.stzfs mkfs.c syscalls.h syscalls.c blocks.h types.h vm.c vm.h)
target_link_libraries(mkfs.stzfs fuse3)
