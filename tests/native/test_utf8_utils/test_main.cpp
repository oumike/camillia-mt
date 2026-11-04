#include <unity.h>

#define main host_test_main
#include "../../test_utf8_utils.cpp"
#undef main

void setUp() {}
void tearDown() {}
static void test_utf8_utils() { TEST_ASSERT_EQUAL_INT(0, host_test_main()); }

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_utf8_utils);
    return UNITY_END();
}
