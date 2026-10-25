#include "../src/web_theme.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); g_fail = 1; } } while (0)

static WebThemeColors camilliaDark() {
    WebThemeColors c = {};
    c.bg = 0x0843; c.panel = 0x1065; c.panelAlt = 0x18A7; c.panelStrong = 0x18A7;
    c.divider = 0x2104; c.dividerHi = 0x4208; c.inputBg = 0x18A7;
    c.accent = 0xDA8E; c.text = 0xFFFF; c.textDim = 0xBDF7; c.onAccent = 0xFFFF;
    c.good = 0x07E0; c.warn = 0xFFE0; c.bad = 0xF800;
    c.isLight = false;
    return c;
}

// Black on white is the ceiling of the WCAG scale, a colour on itself the floor.
static void testContrastExtremes() {
    CHECK(fabs(webThemeContrast(0x0000, 0xFFFF) - 21.0) < 0.1);
    CHECK(webThemeContrast(0x1234, 0x1234) == 1.0);
}

static void testCamilliaDark() {
    char out[600];
    const size_t n = webThemeCssVars(camilliaDark(), out, sizeof(out));
    CHECK(n > 0 && n == strlen(out));
    CHECK(strncmp(out, ":root{", 6) == 0);
    CHECK(strstr(out, "color-scheme:dark") != nullptr);
    CHECK(strstr(out, "--bg:#080818") != nullptr);
    CHECK(strstr(out, "--accent:#de5073") != nullptr);
}

static void testLightScheme() {
    WebThemeColors c = camilliaDark();
    c.isLight = true;
    char out[600];
    CHECK(webThemeCssVars(c, out, sizeof(out)) > 0);
    CHECK(strstr(out, "color-scheme:light") != nullptr);
}

// A custom theme whose text all but vanishes into its background gets
// Camillia's text pair for its mode instead.
static void testContrastGuard() {
    WebThemeColors c = camilliaDark();
    c.bg = 0x8410;
    c.text = 0x8430;
    char out[600];
    CHECK(webThemeCssVars(c, out, sizeof(out)) > 0);
    CHECK(strstr(out, "--text:#f4f6fb") != nullptr);
    CHECK(strstr(out, "--dim:#b0b8c8") != nullptr);
}

static void testTooSmall() {
    char out[16];
    out[0] = 'x';
    CHECK(webThemeCssVars(camilliaDark(), out, sizeof(out)) == 0);
    CHECK(out[0] == 'x');
}

int main() {
    testContrastExtremes();
    testCamilliaDark();
    testLightScheme();
    testContrastGuard();
    testTooSmall();
    printf(g_fail ? "FAILED\n" : "ok\n");
    return g_fail;
}
