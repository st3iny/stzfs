set(EXECUTABLE_OUTPUT_PATH "${PROJECT_SOURCE_DIR}/bin/test")

add_executable(test_blocks test_blocks.c)
target_link_libraries(test_blocks cmocka)
add_test(NAME test_blocks COMMAND test_blocks)

add_executable(test_types test_types.c)
target_link_libraries(test_types cmocka)
add_test(NAME test_types COMMAND test_types)
