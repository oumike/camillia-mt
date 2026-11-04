#include <unity.h>

#define main host_test_main
#include "../../test_map_tiles.cpp"
#undef main

void setUp() {}
void tearDown() {}
static void test_map_tiles() { TEST_ASSERT_EQUAL_INT(0, host_test_main()); }

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_map_tiles);
    return UNITY_END();
}
