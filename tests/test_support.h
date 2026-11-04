#pragma once

#include <stdio.h>
#include <string.h>

namespace test {

static int checks = 0;
static int failures = 0;

inline void check(bool passed, const char *expression, const char *file, int line) {
    checks++;
    if (!passed) {
        failures++;
        printf("  FAIL  %s:%d: %s\n", file, line, expression);
    }
}

inline int report() {
    printf("%s  %d checks, %d failed\n", failures ? "FAILED" : "ok", checks, failures);
    return failures ? 1 : 0;
}

} // namespace test

#define CHECK(expression) test::check((expression), #expression, __FILE__, __LINE__)
