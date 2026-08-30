#pragma once
// Cached US state maps: the table the Locate modal and the web-config "Maps
// Download" section both work from.
//
// This lives in a header because two translation units need the same answers and
// must not be able to disagree. A bounding box duplicated into the web UI would
// be a silently misplaced pin the first time either copy was edited alone —
// which is exactly the failure the .meta files below exist to prevent.
//
// Everything is `inline` rather than `static`: one copy of the 50-entry table in
// the image, not one per includer.
#include <Arduino.h>
#include <math.h>
#include "config.h"
#include "storage.h"

struct UsStateMapSpec {
    const char *code;
    // Spelled out for the Locate modal: "PA" is fine as a cache filename but a
    // poor thing to read under a map.
    const char *name;
    float latMin;
    float latMax;
    float lonMin;
    float lonMax;
};

inline constexpr UsStateMapSpec kUsStateMaps[] = {
    {"AL", "Alabama", 30.1f, 35.1f, -88.5f, -84.9f},
    {"AK", "Alaska", 51.2f, 71.5f, -170.0f, -129.9f},
    {"AZ", "Arizona", 31.3f, 37.0f, -114.9f, -109.0f},
    {"AR", "Arkansas", 33.0f, 36.5f, -94.6f, -89.6f},
    {"CA", "California", 32.5f, 42.1f, -124.5f, -114.1f},
    {"CO", "Colorado", 37.0f, 41.0f, -109.1f, -102.0f},
    {"CT", "Connecticut", 40.9f, 42.1f, -73.8f, -71.8f},
    {"DE", "Delaware", 38.4f, 39.9f, -75.8f, -75.0f},
    {"FL", "Florida", 24.4f, 31.1f, -87.7f, -80.0f},
    {"GA", "Georgia", 30.3f, 35.0f, -85.6f, -80.8f},
    {"HI", "Hawaii", 18.9f, 22.3f, -160.5f, -154.8f},
    {"ID", "Idaho", 41.9f, 49.1f, -117.3f, -111.0f},
    {"IL", "Illinois", 36.9f, 42.5f, -91.6f, -87.0f},
    {"IN", "Indiana", 37.8f, 41.8f, -88.1f, -84.8f},
    {"IA", "Iowa", 40.3f, 43.6f, -96.7f, -90.1f},
    {"KS", "Kansas", 37.0f, 40.1f, -102.1f, -94.6f},
    {"KY", "Kentucky", 36.5f, 39.2f, -89.7f, -82.9f},
    {"LA", "Louisiana", 28.9f, 33.1f, -94.1f, -88.8f},
    {"ME", "Maine", 43.0f, 47.5f, -71.2f, -66.9f},
    {"MD", "Maryland", 37.9f, 39.8f, -79.6f, -75.0f},
    {"MA", "Massachusetts", 41.2f, 42.9f, -73.6f, -69.9f},
    {"MI", "Michigan", 41.7f, 48.3f, -90.5f, -82.1f},
    {"MN", "Minnesota", 43.5f, 49.4f, -97.3f, -89.5f},
    {"MS", "Mississippi", 30.1f, 35.0f, -91.7f, -88.1f},
    {"MO", "Missouri", 35.9f, 40.7f, -95.9f, -89.1f},
    {"MT", "Montana", 44.3f, 49.1f, -116.1f, -104.0f},
    {"NE", "Nebraska", 39.9f, 43.1f, -104.1f, -95.3f},
    {"NV", "Nevada", 35.0f, 42.1f, -120.1f, -114.0f},
    {"NH", "New Hampshire", 42.7f, 45.4f, -72.6f, -70.6f},
    {"NJ", "New Jersey", 38.9f, 41.4f, -75.6f, -73.9f},
    {"NM", "New Mexico", 31.3f, 37.0f, -109.1f, -103.0f},
    {"NY", "New York", 40.4f, 45.1f, -79.8f, -71.8f},
    {"NC", "North Carolina", 33.8f, 36.7f, -84.4f, -75.4f},
    {"ND", "North Dakota", 45.9f, 49.1f, -104.1f, -96.5f},
    {"OH", "Ohio", 38.4f, 42.3f, -84.9f, -80.5f},
    {"OK", "Oklahoma", 33.6f, 37.1f, -103.0f, -94.4f},
    {"OR", "Oregon", 41.9f, 46.3f, -124.7f, -116.5f},
    {"PA", "Pennsylvania", 39.7f, 42.5f, -80.6f, -74.7f},
    {"RI", "Rhode Island", 41.1f, 42.1f, -71.9f, -71.1f},
    {"SC", "South Carolina", 32.0f, 35.2f, -83.4f, -78.5f},
    {"SD", "South Dakota", 42.5f, 45.9f, -104.1f, -96.4f},
    {"TN", "Tennessee", 34.9f, 36.7f, -90.4f, -81.6f},
    {"TX", "Texas", 25.8f, 36.6f, -106.7f, -93.5f},
    {"UT", "Utah", 37.0f, 42.1f, -114.1f, -109.0f},
    {"VT", "Vermont", 42.7f, 45.1f, -73.5f, -71.5f},
    {"VA", "Virginia", 36.5f, 39.5f, -83.7f, -75.2f},
    {"WA", "Washington", 45.5f, 49.1f, -124.9f, -116.9f},
    {"WV", "West Virginia", 37.2f, 40.7f, -82.7f, -77.7f},
    {"WI", "Wisconsin", 42.4f, 47.3f, -92.9f, -86.8f},
    {"WY", "Wyoming", 41.0f, 45.1f, -111.1f, -104.0f},
};

inline constexpr int kUsStateMapCount =
    (int)(sizeof(kUsStateMaps) / sizeof(kUsStateMaps[0]));
// The size the browser composites to. Chosen so the image is *downscaled* on
// every panel rather than stretched: a T-Deck's Locate box is 282x174, so 240x160
// was being blown up 0.92x and looked soft. 384x256 lands at 1.47x supersampling.
//
// Not larger: 480x320 decodes to 600 KB (ARGB8888) and peaks near 768 KB, which
// is the whole LVGL pool on the 2 MB Heltec — and it only reaches 1.85x on a
// panel 282 px wide, for double the tiles and double the download.
//
// The renderer reads each file's real dimensions from its header, so maps cached
// at an older size keep working; they are simply softer until refreshed.
inline constexpr int kStateMapImageW = 384;
inline constexpr int kStateMapImageH = 256;
inline constexpr const char *kStateMapCacheVersion = "v2";
inline constexpr const char *kStateMapMarkerPath = "/camillia/state_maps/state_maps.complete";
inline constexpr const char *kStateMapLegacyMarkerPath = "/camillia/state_maps/.complete";

// Fixed-lat/lon detail maps are keyed by 0.1-degree cells. A key names the
// cell's lower-left corner in tenths of a degree.
inline constexpr int kDetailMapCellScale = 10;   // 10 tenths per degree
inline constexpr float kDetailMapCellDeg = 0.1f;
inline constexpr int kDetailMapImageW = kStateMapImageW;
inline constexpr int kDetailMapImageH = kStateMapImageH;
inline constexpr const char *kDetailMapCacheVersion = "v1";
inline constexpr const char *kDetailMapMarkerPath = "/camillia/detail_maps/detail_maps.version";

inline bool nodesDetailMapKeyFromIndex(int lat10, int lon10, char *out, size_t outLen) {
    if (!out || outLen < 11) return false;
    // Valid cell starts: latitude [-90.0, 89.9], longitude [-180.0, 179.9].
    if (lat10 < -900 || lat10 > 899) return false;
    if (lon10 < -1800 || lon10 > 1799) return false;
    const char latHem = (lat10 < 0) ? 'S' : 'N';
    const char lonHem = (lon10 < 0) ? 'W' : 'E';
    const int latAbs = (lat10 < 0) ? -lat10 : lat10;
    const int lonAbs = (lon10 < 0) ? -lon10 : lon10;
    snprintf(out, outLen, "%c%04d%c%04d", latHem, latAbs, lonHem, lonAbs);
    return true;
}

inline bool nodesDetailMapParseKey(const char *key, int &lat10, int &lon10) {
    if (!key || strlen(key) != 10) return false;
    auto digit = [](char c) -> int { return (c >= '0' && c <= '9') ? (c - '0') : -1; };

    const char latHem = key[0];
    const char lonHem = key[5];
    if (!(latHem == 'N' || latHem == 'S')) return false;
    if (!(lonHem == 'E' || lonHem == 'W')) return false;

    int la = 0;
    int lo = 0;
    for (int i = 1; i <= 4; i++) {
        int d = digit(key[i]);
        if (d < 0) return false;
        la = la * 10 + d;
    }
    for (int i = 6; i <= 9; i++) {
        int d = digit(key[i]);
        if (d < 0) return false;
        lo = lo * 10 + d;
    }

    lat10 = (latHem == 'S') ? -la : la;
    lon10 = (lonHem == 'W') ? -lo : lo;
    if (lat10 < -900 || lat10 > 899) return false;
    if (lon10 < -1800 || lon10 > 1799) return false;
    return true;
}

inline void nodesDetailMapBoundsFromIndex(int lat10, int lon10,
                                          float &latMin, float &latMax,
                                          float &lonMin, float &lonMax) {
    latMin = (float)lat10 / (float)kDetailMapCellScale;
    lonMin = (float)lon10 / (float)kDetailMapCellScale;
    latMax = latMin + kDetailMapCellDeg;
    lonMax = lonMin + kDetailMapCellDeg;
}

inline bool nodesDetailMapKeyForCoords(float lat, float lon, char *out, size_t outLen,
                                       float *latMinOut = nullptr, float *latMaxOut = nullptr,
                                       float *lonMinOut = nullptr, float *lonMaxOut = nullptr) {
    if (!(lat >= -90.0f && lat <= 90.0f)) return false;
    if (!(lon > -100000.0f && lon < 100000.0f)) return false;

    // [90.0, 90.1) and [180.0, 180.1) do not exist as cell starts.
    if (lat >= 90.0f) lat = 89.9999f;

    lon = fmodf(lon + 180.0f, 360.0f);
    if (lon < 0.0f) lon += 360.0f;
    lon -= 180.0f;
    if (lon >= 180.0f) lon = 179.9999f;

    const int lat10 = (int)floorf(lat * (float)kDetailMapCellScale);
    const int lon10 = (int)floorf(lon * (float)kDetailMapCellScale);
    if (!nodesDetailMapKeyFromIndex(lat10, lon10, out, outLen)) return false;

    if (latMinOut || latMaxOut || lonMinOut || lonMaxOut) {
        float latMin = 0.0f, latMax = 0.0f, lonMin = 0.0f, lonMax = 0.0f;
        nodesDetailMapBoundsFromIndex(lat10, lon10, latMin, latMax, lonMin, lonMax);
        if (latMinOut) *latMinOut = latMin;
        if (latMaxOut) *latMaxOut = latMax;
        if (lonMinOut) *lonMinOut = lonMin;
        if (lonMaxOut) *lonMaxOut = lonMax;
    }
    return true;
}

inline String nodesStateMapPath(const char *stateCode) {
    String p = "/camillia/state_maps/";
    p += stateCode;
    p += ".png";
    return p;
}

inline String nodesStateMapMetaPath(const char *stateCode) {
    String p = "/camillia/state_maps/";
    p += stateCode;
    p += ".meta";
    return p;
}

inline String nodesDetailMapPath(const char *cellKey) {
    String p = "/camillia/detail_maps/";
    p += cellKey;
    p += ".png";
    return p;
}

inline String nodesDetailMapMetaPath(const char *cellKey) {
    String p = "/camillia/detail_maps/";
    p += cellKey;
    p += ".meta";
    return p;
}

inline bool nodesFileLooksLikePng(const char *path) {
#if !HAS_FILE_STORAGE
    (void)(path);
    return false;
#else
    if (!path || !path[0]) return false;
    File f = storageFs().open(path, FILE_READ);
    if (!f) return false;

    if (f.size() < 64) {
        f.close();
        return false;
    }

    uint8_t hdr[24] = {0};
    size_t n = f.read(hdr, sizeof(hdr));
    f.close();
    if (n != sizeof(hdr)) return false;

    static const uint8_t kPngSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (memcmp(hdr, kPngSig, sizeof(kPngSig)) != 0) return false;
    // Ensure first chunk type is IHDR.
    return hdr[12] == 'I' && hdr[13] == 'H' && hdr[14] == 'D' && hdr[15] == 'R';
#endif
}

// Width/height from the PNG header. Same 24 bytes nodesFileLooksLikePng() reads:
// the IHDR payload starts at offset 16, big-endian.
inline bool nodesReadPngSize(const char *path, uint32_t &w, uint32_t &h) {
#if !HAS_FILE_STORAGE
    (void)path; (void)w; (void)h;
    return false;
#else
    if (!path || !path[0]) return false;
    File f = storageFs().open(path, FILE_READ);
    if (!f) return false;
    uint8_t hdr[24] = {0};
    size_t n = f.read(hdr, sizeof(hdr));
    f.close();
    if (n != sizeof(hdr)) return false;
    static const uint8_t kPngSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (memcmp(hdr, kPngSig, sizeof(kPngSig)) != 0) return false;
    if (!(hdr[12] == 'I' && hdr[13] == 'H' && hdr[14] == 'D' && hdr[15] == 'R')) return false;
    w = ((uint32_t)hdr[16] << 24) | ((uint32_t)hdr[17] << 16) | ((uint32_t)hdr[18] << 8) | hdr[19];
    h = ((uint32_t)hdr[20] << 24) | ((uint32_t)hdr[21] << 16) | ((uint32_t)hdr[22] << 8) | hdr[23];
    return (w > 0 && h > 0);
#endif
}

inline bool nodesWriteStateMapMeta(const char *stateCode,
                                   float latMin, float latMax,
                                   float lonMin, float lonMax) {
#if !HAS_FILE_STORAGE
    (void)(stateCode);
    (void)(latMin);
    (void)(latMax);
    (void)(lonMin);
    (void)(lonMax);
    return false;
#else
    String p = nodesStateMapMetaPath(stateCode);
    if (storageFs().exists(p.c_str())) storageFs().remove(p.c_str());
    File f = storageFs().open(p.c_str(), FILE_WRITE);
    if (!f) return false;
    f.printf("%.6f,%.6f,%.6f,%.6f\n", (double)latMin, (double)latMax, (double)lonMin, (double)lonMax);
    f.close();
    return true;
#endif
}

inline bool nodesReadStateMapMeta(const char *stateCode,
                                  float &latMin, float &latMax,
                                  float &lonMin, float &lonMax) {
#if !HAS_FILE_STORAGE
    (void)(stateCode);
    (void)(latMin);
    (void)(latMax);
    (void)(lonMin);
    (void)(lonMax);
    return false;
#else
    String p = nodesStateMapMetaPath(stateCode);
    File f = storageFs().open(p.c_str(), FILE_READ);
    if (!f) return false;
    String line = f.readStringUntil('\n');
    f.close();

    double a = 0.0, b = 0.0, c = 0.0, d = 0.0;
    if (sscanf(line.c_str(), "%lf,%lf,%lf,%lf", &a, &b, &c, &d) != 4) return false;
    latMin = (float)a;
    latMax = (float)b;
    lonMin = (float)c;
    lonMax = (float)d;
    return true;
#endif
}

inline bool nodesWriteDetailMapMeta(const char *cellKey,
                                    float latMin, float latMax,
                                    float lonMin, float lonMax) {
#if !HAS_FILE_STORAGE
    (void)(cellKey);
    (void)(latMin);
    (void)(latMax);
    (void)(lonMin);
    (void)(lonMax);
    return false;
#else
    String p = nodesDetailMapMetaPath(cellKey);
    if (storageFs().exists(p.c_str())) storageFs().remove(p.c_str());
    File f = storageFs().open(p.c_str(), FILE_WRITE);
    if (!f) return false;
    f.printf("%.6f,%.6f,%.6f,%.6f\n", (double)latMin, (double)latMax,
             (double)lonMin, (double)lonMax);
    f.close();
    return true;
#endif
}

inline bool nodesReadDetailMapMeta(const char *cellKey,
                                   float &latMin, float &latMax,
                                   float &lonMin, float &lonMax,
                                   int *lodOut = nullptr) {
#if !HAS_FILE_STORAGE
    (void)(cellKey);
    (void)(latMin);
    (void)(latMax);
    (void)(lonMin);
    (void)(lonMax);
    if (lodOut) *lodOut = -1;
    return false;
#else
    String p = nodesDetailMapMetaPath(cellKey);
    File f = storageFs().open(p.c_str(), FILE_READ);
    if (!f) return false;
    String line = f.readStringUntil('\n');
    f.close();

    double a = 0.0, b = 0.0, c = 0.0, d = 0.0;
    int lod = -1;
    int n = sscanf(line.c_str(), "%lf,%lf,%lf,%lf,%d", &a, &b, &c, &d, &lod);
    if (n < 4) return false;
    latMin = (float)a;
    latMax = (float)b;
    lonMin = (float)c;
    lonMax = (float)d;
    if (lodOut) *lodOut = (n >= 5) ? lod : -1;
    return true;
#endif
}

inline const UsStateMapSpec *nodesStateForCoords(float lat, float lon) {
    const UsStateMapSpec *best = nullptr;
    float bestArea = 1e9f;
    for (int i = 0; i < kUsStateMapCount; i++) {
        const UsStateMapSpec &s = kUsStateMaps[i];
        if (lat < s.latMin || lat > s.latMax || lon < s.lonMin || lon > s.lonMax) continue;
        float area = (s.latMax - s.latMin) * (s.lonMax - s.lonMin);
        if (!best || area < bestArea) {
            best = &s;
            bestArea = area;
        }
    }
    return best;
}
