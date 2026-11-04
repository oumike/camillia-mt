#include <unity.h>

#define main host_test_main
#include "../../test_base64_util.cpp"
#undef main

void setUp() {}
void tearDown() {}
static void test_base64_util() { TEST_ASSERT_EQUAL_INT(0, host_test_main()); }

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_base64_util);
    return UNITY_END();
}
