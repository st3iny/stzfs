cmake_minimum_required(VERSION 3.13)
project(filesystem C)

set(CMAKE_C_STANDARD 99)
set(EXECUTABLE_OUTPUT_PATH "${PROJECT_SOURCE_DIR}/bin")

enable_testing()

add_subdirectory(src)
add_subdirectory(test)
