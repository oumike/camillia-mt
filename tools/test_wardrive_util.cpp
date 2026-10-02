// Host test for src/wardrive_util.h.  g++ -std=c++17 -I src tools/test_wardrive_util.cpp && ./a.out
#include "wardrive_util.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main() {
    char b[64];
    wardriveDeg(283456789, b, sizeof b);   CHECK(!strcmp(b, "28.3456789"));
    wardriveDeg(-164567890, b, sizeof b);  CHECK(!strcmp(b, "-16.4567890"));
    wardriveDeg(-5, b, sizeof b);          CHECK(!strcmp(b, "-0.0000005"));
    wardriveDeg(0, b, sizeof b);           CHECK(!strcmp(b, "0.0000000"));

    // Tenerife: 0.001 deg lat ~ 111 m; 0.001 deg lon at 28.3N ~ 98 m
    float d = wardriveDistanceM(283000000, -165000000, 283010000, -165000000);
    CHECK(d > 110 && d < 112);
    d = wardriveDistanceM(283000000, -165000000, 283000000, -164990000);
    CHECK(d > 97 && d < 99);

    // throttle: inside interval and <50 m -> no; moved 111 m -> yes; interval elapsed -> yes
    CHECK(!wardriveShouldLog(1000, 283000000, -165000000, 283000100, -165000000, 30000, 50));
    CHECK( wardriveShouldLog(1000, 283000000, -165000000, 283010000, -165000000, 30000, 50));
    CHECK( wardriveShouldLog(30000, 283000000, -165000000, 283000000, -165000000, 30000, 50));

    char q[16];
    wardriveQuote("a\"b,c", q, sizeof q);  CHECK(!strcmp(q, "\"a\"\"b,c\""));
    wardriveQuote("x\ny", q, sizeof q);    CHECK(!strcmp(q, "\"xy\""));
    wardriveQuote("0123456789abcdef", q, sizeof q); CHECK(strlen(q) < sizeof q && q[strlen(q)-1] == '"');

    char line[384];
    wardriveFormatLine(line, sizeof line, 1790000000L, 0x1234abcd, "AB\"C", "Long, name",
                       -97.4f, 6.25f, 0, 1, 0, 283000000, -165000000, 120, 9, 0.9f, 42.0f,
                       true, 283100000, -165100000);
    printf("%s\n", line);
    CHECK(!strcmp(line, "1790000000,2026-09-21T14:13:20Z,!1234abcd,\"AB\"\"C\",\"Long, name\",-97,6.25,0,1,0,"
                        "28.3000000,-16.5000000,120,9,0.9,42.0,28.3100000,-16.5100000"));
    wardriveFormatLine(line, sizeof line, 0, 1, "", "", -120, -5, -1, 67, 1, 1, 2, 0, 0, 99.9f, 0,
                       false, 0, 0);
    printf("%s\n", line);
    CHECK(!strcmp(line, "0,,!00000001,\"\",\"\",-120,-5.00,,67,1,0.0000001,0.0000002,0,0,99.9,0.0,,"));
    printf(fails ? "%d FAILED\n" : "all ok\n", fails);
    return fails != 0;
}
