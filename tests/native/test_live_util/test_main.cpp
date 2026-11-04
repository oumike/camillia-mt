#include <unity.h>

#define main host_test_main
#include "../../test_live_util.cpp"
#undef main

void setUp() {}
void tearDown() {}
static void test_live_util() { TEST_ASSERT_EQUAL_INT(0, host_test_main()); }

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_live_util);
    return UNITY_END();
}
