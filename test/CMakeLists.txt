add_executable(test_block test_block.c)
target_link_libraries(test_block cmocka)
add_test(NAME test_block COMMAND test_block)
