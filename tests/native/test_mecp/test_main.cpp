// SPDX-License-Identifier: GPL-3.0-or-later
#include <unity.h>

#define main host_test_main
#include "../../test_mecp.cpp"
#undef main

void setUp() {}
void tearDown() {}
static void test_mecp() { TEST_ASSERT_EQUAL_INT(0, host_test_main()); }

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_mecp);
    return UNITY_END();
}
