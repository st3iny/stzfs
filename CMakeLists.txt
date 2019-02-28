cmake_minimum_required(VERSION 3.13)
project(filesystem C)

set(CMAKE_C_STANDARD 99)

add_executable(filesystem src/main.c src/syscalls.h src/syscalls.c src/blocks.h src/types.h src/vm.h src/vm.c)
add_executable(utils src/utils.c src/utils.h src/vm.h src/vm.c src/blocks.h src/types.h)
