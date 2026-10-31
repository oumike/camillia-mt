// pio test -e native wrapper for tools/test_wardrive_util.cpp: runs its checks as one Unity test.
// The checks live there, so tests/run.sh and this run the same code.
#include <unity.h>

#define main host_test_main
#include "../../../tools/test_wardrive_util.cpp"
#undef main

void setUp() {}
void tearDown() {}

static void test_wardrive_util() { TEST_ASSERT_EQUAL_INT(0, host_test_main()); }

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_wardrive_util);
    return UNITY_END();
}
