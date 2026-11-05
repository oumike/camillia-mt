// env:native builds as a T-Deck; this suite is the Mesh Deck's fuel-gauge path.
#undef DEVICE_TDECK
#define DEVICE_MESH_DECK 1
#include <unity.h>

#define main host_test_main
#include "../../test_battery_util_mesh_deck.cpp"
#undef main

void setUp() {}
void tearDown() {}
static void test_battery_util_mesh_deck() { TEST_ASSERT_EQUAL_INT(0, host_test_main()); }

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_battery_util_mesh_deck);
    return UNITY_END();
}
