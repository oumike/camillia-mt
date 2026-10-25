// Static preview of the web config page, built from the same bytes the firmware
// serves: kAppCss / kAppJs / kAppFontsLink from web_app_assets.h and the theme
// block from web_theme.cpp. The body is sample_body.html on stdin, which mirrors
// the markup sendConfigPage() emits. Usage: preview <camD|evrL|solD|eink>
#include "../../src/web_app_assets.h"
#include "../../src/web_theme.h"

#include <stdio.h>
#include <string.h>

struct NamedTheme { const char *name; WebThemeColors c; };

// Anchors from kUiThemePresets (main_lvgl.cpp); the derived colours are close
// stand-ins for what applyUiThemePalette() blends, enough to judge contrast.
static const NamedTheme kThemes[] = {
    {"camD", {0x0843, 0x1065, 0x18A7, 0x2909, 0x2131, 0x6A4D, 0x18A7,
              0xDA8E, 0xF7BF, 0xB5D9, 0xFFFF, 0x4E8D, 0xF628, 0xE28B, false}},
    {"evrL", {0xE73C, 0xF7DE, 0xE71B, 0xD6BA, 0xCE79, 0x9E15, 0xE71B,
              0x2D2A, 0x18E3, 0x5B2B, 0xFFFF, 0x2D2A, 0xC4A0, 0xC145, true}},
    {"solD", {0x0166, 0x01A8, 0x09E8, 0x1269, 0x1A8B, 0x2CB2, 0x09E8,
              0x2D13, 0xE73B, 0x9514, 0x0166, 0x5D6A, 0xF628, 0xE28B, false}},
    // !HAS_UI_THEMES: the e-ink palette is plain black on white.
    {"eink", {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0xFFFF,
              0x0000, 0x0000, 0x0000, 0xFFFF, 0x0000, 0x0000, 0x0000, true}},
};

int main(int argc, char **argv) {
    const char *want = argc > 1 ? argv[1] : "camD";
    const NamedTheme *t = nullptr;
    for (const NamedTheme &n : kThemes) if (strcmp(n.name, want) == 0) t = &n;
    if (!t) { fprintf(stderr, "unknown theme %s\n", want); return 2; }

    char vars[600];
    if (!webThemeCssVars(t->c, vars, sizeof(vars))) { fprintf(stderr, "vars overflow\n"); return 1; }

    printf("<!DOCTYPE html><html><head><meta charset='utf-8'>"
           "<meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>"
           "<title>Camillia for Meshtastic</title>%s<style>%s</style><style>%s</style></head><body>",
           kAppFontsLink, kAppCss, vars);
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) fwrite(buf, 1, n, stdout);
    printf("<script>%s</script></body></html>\n", kAppJs);
    return 0;
}
