#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "../src/blocks.h"

void test_block_sizes(void** state);
void test_block_entry_sizes(void** state);

int main() {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_block_sizes),
        cmocka_unit_test(test_block_entry_sizes)
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}

void test_block_sizes(void** state) {
   assert_int_equal(sizeof(super_block), BLOCK_SIZE);
   assert_int_equal(sizeof(inode_block), BLOCK_SIZE);
   assert_int_equal(sizeof(dir_block), BLOCK_SIZE);
   assert_int_equal(sizeof(indirect_block), BLOCK_SIZE);
   assert_int_equal(sizeof(bitmap_block), BLOCK_SIZE);
   assert_int_equal(sizeof(data_block), BLOCK_SIZE);
}

void test_block_entry_sizes(void** state) {
    assert_int_equal(sizeof(inode), 128);
    assert_int_equal(sizeof(dir_block_entry), 256);
}
