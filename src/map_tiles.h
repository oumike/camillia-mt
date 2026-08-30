#pragma once

#include <Arduino.h>
#include <FS.h>

#include "config.h"
#include "storage.h"

inline constexpr int kMapTileSizePx = 256;
inline constexpr int kMapTileZoomMin = 2;
inline constexpr int kMapTileZoomRoads = 13;
inline constexpr int kMapTileZoomStreets = 16;
inline constexpr int kMapTileZoomBuildings = 19;
inline constexpr int kMapTileZoomMax = 19;
inline constexpr const char *kMapTileCacheRoot = "/camillia/map_tiles";

inline uint32_t &nodesMapTileCacheEpochStorage() {
    static uint32_t epoch = 1;
    return epoch;
}

inline uint32_t nodesMapTileCacheEpoch() {
    return nodesMapTileCacheEpochStorage();
}

inline void nodesMapTileBumpCacheEpoch() {
    uint32_t &epoch = nodesMapTileCacheEpochStorage();
    epoch++;
    if (epoch == 0) epoch = 1;
}

inline bool nodesMapTileCoordsValid(int zoom, int32_t tileX, int32_t tileY) {
    if (zoom < kMapTileZoomMin || zoom > kMapTileZoomMax) return false;
    const int32_t tilesPerAxis = (int32_t)(1UL << zoom);
    return tileX >= 0 && tileX < tilesPerAxis
           && tileY >= 0 && tileY < tilesPerAxis;
}

inline String nodesMapTileZoomDir(int zoom) {
    String path = kMapTileCacheRoot;
    path += "/";
    path += zoom;
    return path;
}

inline String nodesMapTileXDir(int zoom, int32_t tileX) {
    String path = nodesMapTileZoomDir(zoom);
    path += "/";
    path += (long)tileX;
    return path;
}

inline String nodesMapTilePath(int zoom, int32_t tileX, int32_t tileY) {
    if (!nodesMapTileCoordsValid(zoom, tileX, tileY)) return String();
    String path = nodesMapTileXDir(zoom, tileX);
    path += "/";
    path += (long)tileY;
    path += ".png";
    return path;
}

inline String nodesMapTileUploadTempPath(int zoom, int32_t tileX, int32_t tileY) {
    String path = nodesMapTilePath(zoom, tileX, tileY);
    if (path.isEmpty()) return path;
    path += ".upload";
    return path;
}

inline String nodesMapTileLiveTempPath(int zoom, int32_t tileX, int32_t tileY) {
    String path = nodesMapTilePath(zoom, tileX, tileY);
    if (path.isEmpty()) return path;
    path += ".live";
    return path;
}

inline bool nodesMapTileEnsureParentDirs(int zoom, int32_t tileX) {
#if !HAS_FILE_STORAGE
    (void)zoom;
    (void)tileX;
    return false;
#else
    if (!nodesMapTileCoordsValid(zoom, tileX, 0)) return false;
    if (!storageFs().exists("/camillia") && !storageFs().mkdir("/camillia")) return false;
    if (!storageFs().exists(kMapTileCacheRoot) && !storageFs().mkdir(kMapTileCacheRoot)) {
        return false;
    }
    String zoomDir = nodesMapTileZoomDir(zoom);
    if (!storageFs().exists(zoomDir.c_str()) && !storageFs().mkdir(zoomDir.c_str())) {
        return false;
    }
    String xDir = nodesMapTileXDir(zoom, tileX);
    if (!storageFs().exists(xDir.c_str()) && !storageFs().mkdir(xDir.c_str())) {
        return false;
    }
    return storageFs().exists(xDir.c_str());
#endif
}

inline bool nodesMapTilePngHeaderValid(const uint8_t *data, size_t bytes) {
    if (!data || bytes < 24) return false;
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (memcmp(data, signature, sizeof(signature)) != 0) return false;
    if (memcmp(data + 12, "IHDR", 4) != 0) return false;
    uint32_t width = ((uint32_t)data[16] << 24)
                     | ((uint32_t)data[17] << 16)
                     | ((uint32_t)data[18] << 8)
                     | data[19];
    uint32_t height = ((uint32_t)data[20] << 24)
                      | ((uint32_t)data[21] << 16)
                      | ((uint32_t)data[22] << 8)
                      | data[23];
    return width == kMapTileSizePx && height == kMapTileSizePx;
}

inline bool nodesMapTilePngValid(const uint8_t *data, size_t bytes) {
    if (!nodesMapTilePngHeaderValid(data, bytes) || bytes < 64) return false;
    return data[bytes - 12] == 0 && data[bytes - 11] == 0
           && data[bytes - 10] == 0 && data[bytes - 9] == 0
           && memcmp(data + bytes - 8, "IEND", 4) == 0;
}

inline bool nodesMapTileFileValid(const char *path) {
#if !HAS_FILE_STORAGE
    (void)path;
    return false;
#else
    if (!path || !path[0]) return false;
    File file = storageFs().open(path, FILE_READ);
    if (!file) return false;
    size_t bytes = (size_t)file.size();
    if (bytes < 64) {
        file.close();
        return false;
    }
    uint8_t header[24] = {0};
    size_t read = file.read(header, sizeof(header));
    uint8_t tail[12] = {0};
    bool seekOk = file.seek((uint32_t)(bytes - sizeof(tail)), SeekSet);
    size_t tailRead = seekOk ? file.read(tail, sizeof(tail)) : 0;
    file.close();
    return read == sizeof(header)
           && nodesMapTilePngHeaderValid(header, sizeof(header))
           && tailRead == sizeof(tail)
           && tail[0] == 0 && tail[1] == 0 && tail[2] == 0 && tail[3] == 0
           && memcmp(tail + 4, "IEND", 4) == 0;
#endif
}

inline bool nodesMapTileParseUploadFilename(const char *filename,
                                            int &zoom,
                                            int32_t &tileX,
                                            int32_t &tileY) {
    if (!filename || !filename[0]) return false;
    const char *base = filename;
    for (const char *cursor = filename; *cursor; cursor++) {
        if (*cursor == '/' || *cursor == '\\') base = cursor + 1;
    }

    int parsedZoom = 0;
    long parsedX = 0;
    long parsedY = 0;
    int consumed = 0;
    if (sscanf(base, "%d_%ld_%ld.png%n",
               &parsedZoom, &parsedX, &parsedY, &consumed) != 3
        || consumed <= 0 || base[consumed] != '\0') {
        return false;
    }
    if (!nodesMapTileCoordsValid(parsedZoom, (int32_t)parsedX, (int32_t)parsedY)) {
        return false;
    }

    zoom = parsedZoom;
    tileX = (int32_t)parsedX;
    tileY = (int32_t)parsedY;
    return true;
}