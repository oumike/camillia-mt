#include <unity.h>

#define main host_test_main
#include "../../test_sha256_portable.cpp"
#undef main

void setUp() {}
void tearDown() {}
static void test_sha256_portable() { TEST_ASSERT_EQUAL_INT(0, host_test_main()); }

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_sha256_portable);
    return UNITY_END();
}
