#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "../src/blocks.h"
#include "../src/inode.h"

void test_STZFS_BLOCK_SIZEs(void** state);
void test_block_entry_sizes(void** state);

int main() {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_STZFS_BLOCK_SIZEs),
        cmocka_unit_test(test_block_entry_sizes)
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}

void test_STZFS_BLOCK_SIZEs(void** state) {
   assert_int_equal(sizeof(super_block), STZFS_BLOCK_SIZE);
   assert_int_equal(sizeof(inode_block), STZFS_BLOCK_SIZE);
   assert_int_equal(sizeof(dir_block), STZFS_BLOCK_SIZE);
   assert_int_equal(sizeof(indirect_block), STZFS_BLOCK_SIZE);
   assert_int_equal(sizeof(bitmap_block), STZFS_BLOCK_SIZE);
   assert_int_equal(sizeof(data_block), STZFS_BLOCK_SIZE);
}

void test_block_entry_sizes(void** state) {
    assert_int_equal(sizeof(inode_t), 128);
    assert_int_equal(sizeof(dir_block_entry), 256);
}
