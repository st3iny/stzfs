cmake_minimum_required(VERSION 3.13)
project(stzfs C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0")
set(EXECUTABLE_OUTPUT_PATH "${PROJECT_SOURCE_DIR}/bin")

enable_testing()

add_subdirectory(src)
add_subdirectory(test)
