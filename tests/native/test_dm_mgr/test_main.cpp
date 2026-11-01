#include <unity.h>

#define main host_test_main
#include "../../test_dm_mgr.cpp"
#undef main

void setUp() {}
void tearDown() {}
static void test_dm_mgr() { TEST_ASSERT_EQUAL_INT(0, host_test_main()); }

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_dm_mgr);
    return UNITY_END();
}
