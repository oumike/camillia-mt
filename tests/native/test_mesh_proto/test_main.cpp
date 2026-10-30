// pio test -e native wrapper for tests/test_mesh_proto.cpp: runs its checks as one Unity test.
// The checks live there, so tests/run.sh and this run the same code.
#include <unity.h>

#define main host_test_main
#include "../../../tests/test_mesh_proto.cpp"
#undef main

void setUp() {}
void tearDown() {}

static void test_mesh_proto() { TEST_ASSERT_EQUAL_INT(0, host_test_main()); }

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_mesh_proto);
    return UNITY_END();
}
