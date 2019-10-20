#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "../src/types.h"

void test_typedefs(void** state);

int main() {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_typedefs),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}

void test_typedefs(void** state) {
   assert_true(sizeof(objptr_t) >= sizeof(inodeptr_t));
   assert_true(sizeof(objptr_t) >= sizeof(blockptr_t));
}
