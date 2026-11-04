// test-cflags: -Ihost -DDEVICE_TDECK
#include "host/map/Arduino.h"
#include "../src/map_tiles.h"
#include "test_support.h"

#include <array>

static void testCoordinateBoundsAndPaths() {
    CHECK(nodesMapTileCoordsValid(2, 0, 0));
    CHECK(nodesMapTileCoordsValid(2, 3, 3));
    CHECK(!nodesMapTileCoordsValid(2, 4, 0));
    CHECK(!nodesMapTileCoordsValid(2, 0, 4));
    CHECK(!nodesMapTileCoordsValid(2, -1, 0));
    CHECK(!nodesMapTileCoordsValid(2, 0, -1));
    CHECK(nodesMapTileCoordsValid(19, 524287, 524287));
    CHECK(!nodesMapTileCoordsValid(19, 524288, 0));
    for (int zoom : {-1, 0, 1, 20, 32, 255}) CHECK(!nodesMapTileCoordsValid(zoom, 0, 0));
    CHECK(strcmp(nodesMapTilePath(13, 4095, 2048).c_str(),
                 "/camillia/map_tiles/13/4095/2048.png") == 0);
    CHECK(strcmp(nodesMapTileUploadTempPath(13, 4095, 2048).c_str(),
                 "/camillia/map_tiles/13/4095/2048.png.upload") == 0);
    CHECK(strcmp(nodesMapTileLiveTempPath(13, 4095, 2048).c_str(),
                 "/camillia/map_tiles/13/4095/2048.png.live") == 0);
    CHECK(nodesMapTilePath(1, 0, 0).isEmpty());
    CHECK(nodesMapTileUploadTempPath(2, -1, 0).isEmpty());
    CHECK(nodesMapTileLiveTempPath(2, 0, 4).isEmpty());
}

static void testUploadFilenames() {
    const struct { const char *name; int zoom; int32_t x; int32_t y; } good[] = {
        {"2_0_3.png", 2, 0, 3},
        {"/tmp/13_4095_2048.png", 13, 4095, 2048},
        {"C:\\tiles\\19_524287_524287.png", 19, 524287, 524287},
    };
    for (const auto &c : good) {
        int zoom = 0;
        int32_t x = 0, y = 0;
        CHECK(nodesMapTileParseUploadFilename(c.name, zoom, x, y));
        CHECK(zoom == c.zoom && x == c.x && y == c.y);
    }
    const char *bad[] = {
        nullptr, "", "/", "2_0_0", "2_0_0.jpg", "2_0_0.PNG", "2_0_0.png.extra",
        "2_0_0.png/", "2_0.png", "2_a_0.png", "1_0_0.png", "20_0_0.png",
        "2_-1_0.png", "2_0_-1.png", "2_4_0.png", "2_0_4.png",
    };
    for (const char *name : bad) {
        int zoom = -99;
        int32_t x = -98, y = -97;
        CHECK(!nodesMapTileParseUploadFilename(name, zoom, x, y));
        CHECK(zoom == -99 && x == -98 && y == -97);
    }
}

static std::array<uint8_t, 64> pngFixture() {
    // Structural fixture, not a decodable image: this API checks headers/tail,
    // while LVGL's PNG decoder validates chunks, CRCs, and compressed pixels.
    std::array<uint8_t, 64> png = {
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n',
        0, 0, 0, 13, 'I', 'H', 'D', 'R',
        0, 0, 1, 0, 0, 0, 1, 0,
    };
    memcpy(png.data() + 56, "IEND", 4);
    return png;
}

static void testPngValidation() {
    const auto png = pngFixture();
    CHECK(nodesMapTilePngHeaderValid(png.data(), 24));
    CHECK(nodesMapTilePngValid(png.data(), png.size()));
    CHECK(!nodesMapTilePngHeaderValid(nullptr, 24));
    for (size_t n = 0; n < png.size(); n++) {
        CHECK(!nodesMapTilePngValid(png.data(), n));
        if (n < 24) CHECK(!nodesMapTilePngHeaderValid(png.data(), n));
    }
    for (size_t i : {0u, 1u, 7u, 12u, 15u, 16u, 18u, 19u, 20u, 22u, 23u,
                     52u, 53u, 54u, 55u, 56u, 59u}) {
        auto bad = png;
        bad[i] ^= 1;
        CHECK(!nodesMapTilePngValid(bad.data(), bad.size()));
    }
}

int main() {
    testCoordinateBoundsAndPaths();
    testUploadFilenames();
    testPngValidation();
    return test::report();
}
