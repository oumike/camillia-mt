// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: node locate map.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

// The combined index the highlight is on, or -1.
static int currentNodesRowIndex() {
    if (s_nodesFilteredCount <= 0 || s_nodesSelected < 0 || s_nodesSelected >= s_nodesFilteredCount) {
        return -1;
    }
    const int snapshotIdx = s_nodesFilteredIdx[s_nodesSelected];
    if (snapshotIdx < 0 || snapshotIdx >= nodesRowCountTotal()) return -1;
    return snapshotIdx;
}

static const NodeEntry *currentNodesSelection() {
    const int snapshotIdx = currentNodesRowIndex();
    if (snapshotIdx < 0 || snapshotIdx >= s_nodesSnapshotCount) return nullptr;
    // Resolved live against NodeDB; null if evicted since the snapshot.
    return Nodes.find(s_nodesSnapshotIds[snapshotIdx]);
}

// The archived record under the highlight, or null when the row is a live one.
// Its existence is what every "this is history, not a peer" branch keys off, so
// that a live row and an archived row can never both answer for the same
// selection.
static const ArchivedNode *currentNodesArchivedSelection() {
    return nodesArchivedAt(currentNodesRowIndex());
}

static void refreshNodesMap(const NodeEntry *node) {
    if (!s_nodesMapPanel || !s_nodesMapTitle || !s_nodesMapMarker) return;

    bool hasNodePos = node && (node->latI != 0 || node->lonI != 0);
    float lat = (float)MY_LAT_I / 10000000.0f;
    float lon = (float)MY_LON_I / 10000000.0f;
    if (hasNodePos) {
        lat = (float)node->latI / 10000000.0f;
        lon = (float)node->lonI / 10000000.0f;
    }

    int mapW = lv_obj_get_width(s_nodesMapPanel) - 8;
    int mapH = lv_obj_get_height(s_nodesMapPanel) - 22;
    if (mapW < 10) mapW = 10;
    if (mapH < 10) mapH = 10;

    int markerX = -1;
    int markerY = -1;
    int visibleTiles = nodesMapRenderTiles(lat, lon, 4, 20, mapW, mapH,
                                           markerX, markerY,
                                           kLiveDetailMapLodStreet);

    if (s_nodesMapCoords) {
        if (visibleTiles <= 0) {
            lv_obj_clear_flag(s_nodesMapCoords, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(s_nodesMapCoords, LV_ALIGN_CENTER, 0, 2);
            if (!kStateMapsEnabled) {
                lv_label_set_text(s_nodesMapCoords, TR("State map disabled"));
            } else {
                lv_label_set_text(s_nodesMapCoords,
                                  (!s_stateMapBootstrapTried || s_stateMapBootstrapInProgress)
                                      ? TR("Preparing state maps...")
                                      : TR("State map unavailable"));
            }
        } else {
            lv_obj_add_flag(s_nodesMapCoords, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (hasNodePos && markerX >= 0 && markerY >= 0) {
        // A 7px dot, so half its size back from the point to centre it.
        lv_obj_set_pos(s_nodesMapMarker, markerX - 3, markerY - 3);
        lv_obj_clear_flag(s_nodesMapMarker, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_nodesMapMarker, LV_OBJ_FLAG_HIDDEN);
    }

    if (!kStateMapsEnabled) {
        lv_label_set_text(s_nodesMapTitle, TR("Map (off)"));
    } else {
        if (visibleTiles > 0) {
            lv_label_set_text(s_nodesMapTitle,
                              s_nodesMapLastRenderUsedDetail ? TR("Detail Map") : TR("State Map"));
        } else {
            lv_label_set_text(s_nodesMapTitle, TR("Map (loading)"));
        }
    }
}

#if HAS_NODE_LOCATE
// ── Locate ───────────────────────────────────────────────────────────────────
// Node Actions -> Locate. This is a live slippy map: Web Mercator center/zoom
// state drives visible OpenStreetMap tile requests, and each response is
// composited into one RGB565 canvas. No state/detail cache is consulted.
// Pin geometry. The head is a circle of kLocatePinW; the stem makes up the
// rest of kLocatePinH and ends at the tip.
static constexpr int kLocatePinW = 15;
static constexpr int kLocatePinH = 24;

static lv_obj_t *s_nodeLocateBackdrop = nullptr;
static lv_obj_t *s_nodeLocateModal = nullptr;
static uint32_t s_nodeLocateNodeId = 0;

static lv_obj_t *s_nodeLocateMapBox = nullptr;
static lv_obj_t *s_nodeLocateZoomLabel = nullptr;
static lv_obj_t *s_nodeLocateTileLayer = nullptr;
static lv_obj_t *s_nodeLocateMarker = nullptr;
static int   s_locateMapW = 0;
static int   s_locateMapH = 0;
// Where the pin goes: the node being located or, on the node map, this node,
// which the node table never holds. No pin while s_locateHasPin is false.
static float s_locateNodeLat = 0.0f;
static float s_locateNodeLon = 0.0f;
static bool  s_locateHasPin = false;

static constexpr int kNodeLocateTilePx = kMapTileSizePx;
static constexpr int kNodeLocateZoomMin = kMapTileZoomMin;
static constexpr int kNodeLocateZoomDefault = kMapTileZoomRoads;
static constexpr int kNodeLocateZoomMax = kMapTileZoomMax;
// The node map's location button: this node with the mesh around it, about
// 35 km across a 320 px map at 45 degrees latitude.
static constexpr int kNodeMapSelfZoom = 10;
// Every tile the viewport can touch: as many as span it on each axis, plus one,
// since a view rarely starts on a tile edge. The map is full screen, so the
// panel sets the count, either way up -- 6 at 320x240, 9 at 480x320, 12 on the
// P4 and 15 on the CrowPanel 5". The job list fills row by row and stops here,
// so a cap below that leaves the bottom of the map unrequested.
static constexpr int nodeLocateTilesSpanned(int px) {
    return (px + kNodeLocateTilePx - 1) / kNodeLocateTilePx + 1;
}
static constexpr int kNodeLocateMaxTileJobs =
    nodeLocateTilesSpanned(DEVICE_LCD_LANDSCAPE_W / (UI_PIXEL_SCALE * UI_COORD_SCALE))
    * nodeLocateTilesSpanned(DEVICE_LCD_LANDSCAPE_H / (UI_PIXEL_SCALE * UI_COORD_SCALE));
static constexpr size_t kNodeLocateMaxTileBytes = 256U * 1024U;
static constexpr uint32_t kNodeLocatePanDebounceMs = 180UL;

struct NodeLocateTileJob {
    int zoom = kNodeLocateZoomDefault;
    int32_t tileX = 0;
    int32_t tileY = 0;
    int16_t drawX = 0;
    int16_t drawY = 0;
    uint32_t generation = 0;
    uint32_t cacheEpoch = 0;
};

static lv_obj_t *s_nodeLocateCanvas = nullptr;
static lv_draw_buf_t *s_nodeLocateCanvasBuf = nullptr;
static lv_obj_t *s_nodeLocateStatusLabel = nullptr;
static int s_nodeLocateZoom = kNodeLocateZoomDefault;
static double s_nodeLocateCenterWorldX = 0.0;
static double s_nodeLocateCenterWorldY = 0.0;
static uint32_t s_nodeLocateGeneration = 0;
static bool s_nodeLocateRefreshPending = false;
static uint32_t s_nodeLocateRefreshAtMs = 0;
static bool s_nodeLocateHasPixels = false;
static NodeLocateTileJob s_nodeLocateTileJobs[kNodeLocateMaxTileJobs];
static int s_nodeLocateTileJobCount = 0;
static int s_nodeLocateTileJobNext = 0;
static TaskHandle_t s_nodeLocateTileTask = nullptr;
static volatile bool s_nodeLocateTileBusy = false;
static volatile bool s_nodeLocateTileDone = false;
static volatile bool s_nodeLocateTileOk = false;
static NodeLocateTileJob s_nodeLocateTileRequest;
static NodeLocateTileJob s_nodeLocateTileCompleted;
static size_t s_nodeLocateTileCompletedBytes = 0;
static uint8_t *s_nodeLocateTilePng = nullptr;

// ── Node map (Nodes -> Map) ──
// The same modal with every located node on it instead of one pin. On colour
// panels the nodes are a heat map; on the 1 bpp T-Deck Pro, which has no
// colours to ramp through, each node is a black dot in a white ring.
//
// The overlay is painted into the tile canvas itself, once per pixel, whenever
// that pixel is written: after a clear, after a pan (only the strip the shift
// exposed), and after each tile lands (only that tile). The canvas is
// memmove'd on pan, so heat already painted travels with the map under it and
// never needs repainting. What makes that work is s_nodeMapHeat: a per-pixel
// intensity for the current view, rebuilt whenever the view moves. Rebuilding
// costs one small kernel stamp per node, far less than a tile decode.
static bool s_nodeLocateAllNodes = false;
struct NodeMapPoint {
    float lat;
    float lon;
};
static NodeMapPoint s_nodeMapPoints[MAX_NODES];
static int s_nodeMapPointCount = 0;
// Viewport positions of the points for the view s_nodeMapHeat was built for.
static int16_t s_nodeMapScreenX[MAX_NODES];
static int16_t s_nodeMapScreenY[MAX_NODES];
static uint8_t *s_nodeMapHeat = nullptr;   // s_locateMapW x s_locateMapH
static int s_nodeMapHeatZoom = -1;
static double s_nodeMapHeatCenterX = 0.0;
static double s_nodeMapHeatCenterY = 0.0;

static void nodeMapRebuildOverlay();
static void nodeMapPaintRect(int x0, int y0, int x1, int y1);

static void closeNodeLocateModal() {
    lv_draw_buf_t *canvasBuf = s_nodeLocateCanvasBuf;
    s_nodeLocateGeneration++;
    s_nodeLocateRefreshPending = false;
    s_nodeLocateTileJobCount = 0;
    s_nodeLocateTileJobNext = 0;
    if (canvasBuf) lv_image_cache_drop(canvasBuf);

    if (lvObjValid(s_nodeLocateBackdrop)) {
        lv_obj_del(s_nodeLocateBackdrop);
    } else if (lvObjValid(s_nodeLocateModal)) {
        lv_obj_del(s_nodeLocateModal);
    }
    if (canvasBuf) lv_draw_buf_destroy(canvasBuf);

    s_nodeLocateBackdrop = nullptr;
    s_nodeLocateModal = nullptr;
    s_nodeLocateNodeId = 0;
    s_nodeLocateMapBox = nullptr;
    s_nodeLocateZoomLabel = nullptr;
    s_nodeLocateTileLayer = nullptr;
    s_nodeLocateMarker = nullptr;
    s_nodeLocateCanvas = nullptr;
    s_nodeLocateCanvasBuf = nullptr;
    s_nodeLocateStatusLabel = nullptr;
    s_nodeLocateZoom = kNodeLocateZoomDefault;
    s_nodeLocateCenterWorldX = 0.0;
    s_nodeLocateCenterWorldY = 0.0;
    s_nodeLocateHasPixels = false;
    s_locateNodeLat = 0.0f;
    s_locateNodeLon = 0.0f;
    s_locateHasPin = false;
    s_nodeLocateAllNodes = false;
    s_nodeMapPointCount = 0;
    if (s_nodeMapHeat) {
        heap_caps_free(s_nodeMapHeat);
        s_nodeMapHeat = nullptr;
    }
    s_nodeMapHeatZoom = -1;
}

static double nodeLocateWorldSize(int zoom) {
    return ldexp((double)kNodeLocateTilePx, zoom);
}

static void nodeLocateLatLonToWorld(double lat, double lon, int zoom,
                                    double &worldX, double &worldY) {
    if (lat > 85.05112878) lat = 85.05112878;
    if (lat < -85.05112878) lat = -85.05112878;
    lon = fmod(lon + 180.0, 360.0);
    if (lon < 0.0) lon += 360.0;
    lon -= 180.0;

    const double worldSize = nodeLocateWorldSize(zoom);
    const double sinLat = sin(lat * M_PI / 180.0);
    worldX = (lon + 180.0) / 360.0 * worldSize;
    worldY = (0.5 - log((1.0 + sinLat) / (1.0 - sinLat)) / (4.0 * M_PI))
             * worldSize;
}

static void nodeLocateWorldToLatLon(double worldX, double worldY, int zoom,
                                    double &lat, double &lon) {
    const double worldSize = nodeLocateWorldSize(zoom);
    worldX = fmod(worldX, worldSize);
    if (worldX < 0.0) worldX += worldSize;
    if (worldY < 0.0) worldY = 0.0;
    if (worldY > worldSize) worldY = worldSize;

    lon = worldX / worldSize * 360.0 - 180.0;
    const double mercator = M_PI * (1.0 - 2.0 * worldY / worldSize);
    lat = atan(sinh(mercator)) * 180.0 / M_PI;
}

static void nodeLocateNormalizeCenter() {
    const double worldSize = nodeLocateWorldSize(s_nodeLocateZoom);
    s_nodeLocateCenterWorldX = fmod(s_nodeLocateCenterWorldX, worldSize);
    if (s_nodeLocateCenterWorldX < 0.0) s_nodeLocateCenterWorldX += worldSize;

    const double halfViewport = (double)s_locateMapH * 0.5;
    const double minY = halfViewport;
    const double maxY = worldSize - halfViewport;
    if (s_nodeLocateCenterWorldY < minY) s_nodeLocateCenterWorldY = minY;
    if (s_nodeLocateCenterWorldY > maxY) s_nodeLocateCenterWorldY = maxY;
}

static void nodeLocateSetStatus(const char *text) {
    if (!s_nodeLocateStatusLabel || !lvObjValid(s_nodeLocateStatusLabel)) return;
    if (!text || !text[0]) {
        lv_obj_add_flag(s_nodeLocateStatusLabel, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text(s_nodeLocateStatusLabel, text);
    lv_obj_clear_flag(s_nodeLocateStatusLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_nodeLocateStatusLabel);
}

static void nodeLocateUpdateLiveMarker() {
    if (s_nodeLocateZoomLabel && lvObjValid(s_nodeLocateZoomLabel)) {
        lv_label_set_text_fmt(s_nodeLocateZoomLabel, "%d", s_nodeLocateZoom);
    }
    if (!s_nodeLocateMarker || !lvObjValid(s_nodeLocateMarker)
        || s_locateMapW <= 0 || s_locateMapH <= 0) {
        return;
    }
    if (!s_locateHasPin) {
        lv_obj_add_flag(s_nodeLocateMarker, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    double nodeWorldX = 0.0;
    double nodeWorldY = 0.0;
    nodeLocateLatLonToWorld(s_locateNodeLat, s_locateNodeLon, s_nodeLocateZoom,
                            nodeWorldX, nodeWorldY);

    const double worldSize = nodeLocateWorldSize(s_nodeLocateZoom);
    double deltaX = nodeWorldX - s_nodeLocateCenterWorldX;
    if (deltaX > worldSize * 0.5) deltaX -= worldSize;
    if (deltaX < -worldSize * 0.5) deltaX += worldSize;
    const double deltaY = nodeWorldY - s_nodeLocateCenterWorldY;

    const int markerX = s_locateMapW / 2 + (int)lround(deltaX);
    const int markerY = s_locateMapH / 2 + (int)lround(deltaY);
    if (markerX < 0 || markerX > s_locateMapW
        || markerY < 0 || markerY > s_locateMapH) {
        lv_obj_add_flag(s_nodeLocateMarker, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    int pinX = markerX - kLocatePinW / 2;
    int pinY = markerY - kLocatePinH;
    if (pinX < 0) pinX = 0;
    if (pinY < 0) pinY = 0;
    if (pinX > s_locateMapW - kLocatePinW) pinX = s_locateMapW - kLocatePinW;
    if (pinY > s_locateMapH - kLocatePinH) pinY = s_locateMapH - kLocatePinH;
    lv_obj_set_pos(s_nodeLocateMarker, pinX, pinY);
    lv_obj_clear_flag(s_nodeLocateMarker, LV_OBJ_FLAG_HIDDEN);
}

static lv_color_t nodeLocateCanvasBgColor() {
#if defined(DEVICE_TDECK_PRO)
    // Paper white. This panel has no third tone to hold a tinted ground, and a
    // dark one reduces to a solid black viewport.
    return lv_color_hex(0xFFFFFF);
#else
    return (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0xDCE6F6)
                                           : lv_color_hex(0x0A1E45);
#endif
}

static void nodeLocateClearLiveCanvas() {
    if (!s_nodeLocateCanvas || !lvObjValid(s_nodeLocateCanvas)) return;
    lv_canvas_fill_bg(s_nodeLocateCanvas, nodeLocateCanvasBgColor(), LV_OPA_COVER);
    s_nodeLocateHasPixels = false;
    if (s_nodeLocateAllNodes) {
        nodeMapRebuildOverlay();
        nodeMapPaintRect(0, 0, s_locateMapW, s_locateMapH);
        lv_draw_buf_flush_cache(s_nodeLocateCanvasBuf, nullptr);
        lv_obj_invalidate(s_nodeLocateCanvas);
    }
}

static void nodeLocateShiftLiveCanvas(int dx, int dy) {
    if (!s_nodeLocateCanvasBuf || !s_nodeLocateCanvas
        || !lvObjValid(s_nodeLocateCanvas)
        || s_locateMapW <= 0 || s_locateMapH <= 0) {
        return;
    }
    if (dx == 0 && dy == 0) return;
    if (abs(dx) >= s_locateMapW || abs(dy) >= s_locateMapH) {
        nodeLocateClearLiveCanvas();
        return;
    }

    const uint16_t fill = lv_color_to_u16(nodeLocateCanvasBgColor());
    const int yStart = (dy > 0) ? (s_locateMapH - 1) : 0;
    const int yEnd = (dy > 0) ? -1 : s_locateMapH;
    const int yStep = (dy > 0) ? -1 : 1;

    for (int destY = yStart; destY != yEnd; destY += yStep) {
        const int srcY = destY - dy;
        uint16_t *dest = (uint16_t *)(s_nodeLocateCanvasBuf->data
                                      + destY * s_nodeLocateCanvasBuf->header.stride);
        if (srcY < 0 || srcY >= s_locateMapH) {
            for (int x = 0; x < s_locateMapW; x++) dest[x] = fill;
            continue;
        }

        uint16_t *src = (uint16_t *)(s_nodeLocateCanvasBuf->data
                                     + srcY * s_nodeLocateCanvasBuf->header.stride);
        if (dx > 0) {
            memmove(dest + dx, src, (size_t)(s_locateMapW - dx) * sizeof(uint16_t));
            for (int x = 0; x < dx; x++) dest[x] = fill;
        } else if (dx < 0) {
            const int shift = -dx;
            memmove(dest, src + shift,
                    (size_t)(s_locateMapW - shift) * sizeof(uint16_t));
            for (int x = s_locateMapW - shift; x < s_locateMapW; x++) dest[x] = fill;
        } else if (dest != src) {
            memmove(dest, src, (size_t)s_locateMapW * sizeof(uint16_t));
        }
    }

    if (s_nodeLocateAllNodes) {
        // Only the newly exposed strips: everything else carried its overlay
        // along with it. The side strips stop short of the top/bottom one, so
        // a diagonal pan does not paint its corner twice.
        nodeMapRebuildOverlay();
        int keptY0 = 0;
        int keptY1 = s_locateMapH;
        if (dy > 0) { nodeMapPaintRect(0, 0, s_locateMapW, dy); keptY0 = dy; }
        if (dy < 0) {
            nodeMapPaintRect(0, s_locateMapH + dy, s_locateMapW, s_locateMapH);
            keptY1 = s_locateMapH + dy;
        }
        if (dx > 0) nodeMapPaintRect(0, keptY0, dx, keptY1);
        if (dx < 0) nodeMapPaintRect(s_locateMapW + dx, keptY0, s_locateMapW, keptY1);
    }

    lv_draw_buf_flush_cache(s_nodeLocateCanvasBuf, nullptr);
    lv_obj_invalidate(s_nodeLocateCanvas);
}

static void nodeLocateRequestLiveTiles(uint32_t delayMs, bool clearCanvas) {
    s_nodeLocateGeneration++;
    if (s_nodeLocateGeneration == 0) s_nodeLocateGeneration = 1;
    s_nodeLocateRefreshPending = true;
    s_nodeLocateRefreshAtMs = millis() + delayMs;
    s_nodeLocateTileJobCount = 0;
    s_nodeLocateTileJobNext = 0;
    if (clearCanvas) nodeLocateClearLiveCanvas();
    if (!s_nodeLocateHasPixels) nodeLocateSetStatus(TR("Loading map..."));
    nodeLocateUpdateLiveMarker();
}

static void nodeLocateBuildVisibleTileJobs() {
    s_nodeLocateTileJobCount = 0;
    s_nodeLocateTileJobNext = 0;
    if (s_locateMapW <= 0 || s_locateMapH <= 0) return;

    const double left = s_nodeLocateCenterWorldX - (double)s_locateMapW * 0.5;
    const double top = s_nodeLocateCenterWorldY - (double)s_locateMapH * 0.5;
    const int32_t firstTileX = (int32_t)floor(left / (double)kNodeLocateTilePx);
    const int32_t lastTileX = (int32_t)floor((left + s_locateMapW - 0.001)
                                             / (double)kNodeLocateTilePx);
    const int32_t firstTileY = (int32_t)floor(top / (double)kNodeLocateTilePx);
    const int32_t lastTileY = (int32_t)floor((top + s_locateMapH - 0.001)
                                             / (double)kNodeLocateTilePx);
    const int32_t tilesPerAxis = (int32_t)(1UL << s_nodeLocateZoom);

    for (int32_t rawY = firstTileY; rawY <= lastTileY; rawY++) {
        if (rawY < 0 || rawY >= tilesPerAxis) continue;
        for (int32_t rawX = firstTileX; rawX <= lastTileX; rawX++) {
            if (s_nodeLocateTileJobCount >= kNodeLocateMaxTileJobs) break;
            int32_t wrappedX = rawX % tilesPerAxis;
            if (wrappedX < 0) wrappedX += tilesPerAxis;

            NodeLocateTileJob &job = s_nodeLocateTileJobs[s_nodeLocateTileJobCount++];
            job.zoom = s_nodeLocateZoom;
            job.tileX = wrappedX;
            job.tileY = rawY;
            job.drawX = (int16_t)lround((double)rawX * kNodeLocateTilePx - left);
            job.drawY = (int16_t)lround((double)rawY * kNodeLocateTilePx - top);
            job.generation = s_nodeLocateGeneration;
            job.cacheEpoch = nodesMapTileCacheEpoch();
        }
    }

    for (int i = 0; i < s_nodeLocateTileJobCount - 1; i++) {
        int best = i;
        int bestDx = s_nodeLocateTileJobs[i].drawX + kNodeLocateTilePx / 2
                     - s_locateMapW / 2;
        int bestDy = s_nodeLocateTileJobs[i].drawY + kNodeLocateTilePx / 2
                     - s_locateMapH / 2;
        int bestDistance = bestDx * bestDx + bestDy * bestDy;
        for (int j = i + 1; j < s_nodeLocateTileJobCount; j++) {
            int dx = s_nodeLocateTileJobs[j].drawX + kNodeLocateTilePx / 2
                     - s_locateMapW / 2;
            int dy = s_nodeLocateTileJobs[j].drawY + kNodeLocateTilePx / 2
                     - s_locateMapH / 2;
            int distance = dx * dx + dy * dy;
            if (distance < bestDistance) {
                best = j;
                bestDistance = distance;
            }
        }
        if (best != i) {
            NodeLocateTileJob tmp = s_nodeLocateTileJobs[i];
            s_nodeLocateTileJobs[i] = s_nodeLocateTileJobs[best];
            s_nodeLocateTileJobs[best] = tmp;
        }
    }
}

static bool nodeLocateTileBufferReady() {
    if (s_nodeLocateTilePng) return true;
    s_nodeLocateTilePng = (uint8_t *)heap_caps_malloc(
        kNodeLocateMaxTileBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_nodeLocateTilePng) {
        Serial.println("[map] live tile buffer allocation failed");
        return false;
    }
    return true;
}

static bool nodeLocatePngBufferValid(size_t bytes) {
    return nodesMapTilePngValid(s_nodeLocateTilePng, bytes);
}

static bool nodeLocateLoadCachedTile(const NodeLocateTileJob &job, size_t &bytesOut) {
    bytesOut = 0;
#if !HAS_FILE_STORAGE
    LV_UNUSED(job);
    return false;
#else
    if (!nodeLocateTileBufferReady()) return false;
    String path = nodesMapTilePath(job.zoom, job.tileX, job.tileY);
    if (path.isEmpty()) return false;

    NodesMapFsGuard fsGuard;
    if (!fsGuard.locked() || !sdBegin() || !storageFs().exists(path.c_str())) return false;
    File file = storageFs().open(path.c_str(), FILE_READ);
    if (!file) return false;

    size_t bytes = (size_t)file.size();
    if (bytes < 24 || bytes > kNodeLocateMaxTileBytes) {
        file.close();
        storageFs().remove(path.c_str());
        return false;
    }
    size_t read = file.read(s_nodeLocateTilePng, bytes);
    file.close();
    if (read != bytes || !nodeLocatePngBufferValid(bytes)) {
        storageFs().remove(path.c_str());
        return false;
    }

    bytesOut = bytes;
    Serial.printf("[map] tile cache hit z=%d x=%ld y=%ld, %u bytes\n",
                  job.zoom, (long)job.tileX, (long)job.tileY, (unsigned)bytes);
    return true;
#endif
}

static bool nodeLocatePersistTile(const NodeLocateTileJob &job, size_t bytes) {
#if !HAS_FILE_STORAGE
    LV_UNUSED(job);
    LV_UNUSED(bytes);
    return false;
#else
    if (!nodeLocatePngBufferValid(bytes)
        || job.cacheEpoch != nodesMapTileCacheEpoch()) return false;
    String path = nodesMapTilePath(job.zoom, job.tileX, job.tileY);
    String tempPath = nodesMapTileLiveTempPath(job.zoom, job.tileX, job.tileY);
    if (path.isEmpty() || tempPath.isEmpty()) return false;

    NodesMapFsGuard fsGuard;
    if (!fsGuard.locked() || !sdBegin()) return false;
    if (job.cacheEpoch != nodesMapTileCacheEpoch()) return false;
    if (!nodesMapTileEnsureParentDirs(job.zoom, job.tileX)) return false;
    if (storageFs().exists(path.c_str()) && nodesMapTileFileValid(path.c_str())) return true;
    if (storageFs().exists(tempPath.c_str())) storageFs().remove(tempPath.c_str());

    File file = storageFs().open(tempPath.c_str(), FILE_WRITE);
    if (!file) return false;
    size_t written = file.write(s_nodeLocateTilePng, bytes);
    file.close();
    if (written != bytes) {
        storageFs().remove(tempPath.c_str());
        return false;
    }

    if (storageFs().exists(path.c_str())) storageFs().remove(path.c_str());
    if (!storageFs().rename(tempPath.c_str(), path.c_str())) {
        storageFs().remove(tempPath.c_str());
        return false;
    }
    Serial.printf("[map] tile cached z=%d x=%ld y=%ld, %u bytes\n",
                  job.zoom, (long)job.tileX, (long)job.tileY, (unsigned)bytes);
    return true;
#endif
}

static bool nodeLocateDownloadTile(const NodeLocateTileJob &job, size_t &bytesOut) {
    bytesOut = 0;
    if (!nodeLocateTileBufferReady() || !nodesPanelCanDownloadTiles()) return false;

    char base[96];
    strncpy(base, kLiveDetailMapTileProxyBase, sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0';
    size_t baseLen = strlen(base);
    while (baseLen > 0 && base[baseLen - 1] == '/') base[--baseLen] = '\0';

    char url[192];
    snprintf(url, sizeof(url), "%s/%d/%ld/%ld.png",
             base, job.zoom, (long)job.tileX, (long)job.tileY);

    WiFiClient client;
    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout(5000);
    http.setTimeout(12000);
    if (!http.begin(client, url)) {
        client.stop();
        return false;
    }
    char userAgent[112];
    snprintf(userAgent, sizeof(userAgent),
             "Camillia-MT/%s (+https://github.com/oumike/camillia-mt)",
             APP_VERSION);
    http.addHeader("User-Agent", userAgent);
    const char *responseHeaders[] = {"x-blocked"};
    http.collectHeaders(responseHeaders, 1);

    const int code = http.GET();
    if (code != HTTP_CODE_OK || http.hasHeader("x-blocked")) {
        Serial.printf("[map] live tile rejected HTTP=%d blocked=%s z=%d x=%ld y=%ld\n",
                      code,
                      http.hasHeader("x-blocked") ? http.header("x-blocked").c_str() : "no",
                      job.zoom, (long)job.tileX, (long)job.tileY);
        http.end();
        client.stop();
        return false;
    }

    WiFiClient *stream = http.getStreamPtr();
    int remaining = http.getSize();
    if (remaining > (int)kNodeLocateMaxTileBytes) {
        http.end();
        client.stop();
        return false;
    }

    size_t written = 0;
    uint32_t idleDeadline = millis() + 12000UL;
    bool ok = true;
    while (http.connected() && (remaining > 0 || remaining < 0)) {
        int available = stream->available();
        if (available <= 0) {
            if ((int32_t)(millis() - idleDeadline) >= 0) break;
            delay(1);
            continue;
        }

        size_t capacity = kNodeLocateMaxTileBytes - written;
        if (capacity == 0) {
            ok = false;
            break;
        }
        size_t wanted = (size_t)available;
        if (wanted > capacity) wanted = capacity;
        if (remaining > 0 && wanted > (size_t)remaining) wanted = (size_t)remaining;
        if (wanted > 4096U) wanted = 4096U;

        int read = stream->readBytes(s_nodeLocateTilePng + written, wanted);
        if (read <= 0) {
            if ((int32_t)(millis() - idleDeadline) >= 0) break;
            delay(1);
            continue;
        }
        written += (size_t)read;
        if (remaining > 0) remaining -= read;
        idleDeadline = millis() + 12000UL;
        if (remaining == 0) break;
        delay(1);
    }

    http.end();
    client.stop();
    ok = ok && written >= 24 && remaining <= 0 && nodeLocatePngBufferValid(written);
    if (ok) bytesOut = written;
    return ok;
}

static void nodeLocateTileTaskMain(void *arg) {
    LV_UNUSED(arg);
    NodeLocateTileJob job = s_nodeLocateTileRequest;
    size_t bytes = 0;
    bool ok = nodeLocateDownloadTile(job, bytes);
    s_nodeLocateTileCompleted = job;
    s_nodeLocateTileCompletedBytes = bytes;
    s_nodeLocateTileOk = ok;
    s_nodeLocateTileDone = true;
    s_nodeLocateTileBusy = false;
    s_nodeLocateTileTask = nullptr;
    vTaskDelete(nullptr);
}

static bool nodeLocateComposeTile(const NodeLocateTileJob &job, size_t pngBytes);

static void nodeLocateStartNextTile() {
    if (s_nodeLocateTileBusy || s_nodeLocateTileDone) return;
    if (s_nodeLocateTileJobNext >= s_nodeLocateTileJobCount) return;
    if (!nodeLocateTileBufferReady()) return;

    NodeLocateTileJob job = s_nodeLocateTileJobs[s_nodeLocateTileJobNext++];
    size_t cachedBytes = 0;
    if (nodeLocateLoadCachedTile(job, cachedBytes)) {
        if (job.generation == s_nodeLocateGeneration) {
            nodeLocateComposeTile(job, cachedBytes);
        }
        return;
    }
    if (!nodesPanelCanDownloadTiles()) return;

    s_nodeLocateTileRequest = job;
    s_nodeLocateTileOk = false;
    s_nodeLocateTileDone = false;
    s_nodeLocateTileBusy = true;
    BaseType_t rc = xTaskCreate(nodeLocateTileTaskMain,
                                "map_live_tile",
                                12288,
                                nullptr,
                                1,
                                &s_nodeLocateTileTask);
    if (rc != pdPASS) {
        s_nodeLocateTileCompleted = s_nodeLocateTileRequest;
        s_nodeLocateTileCompletedBytes = 0;
        s_nodeLocateTileOk = false;
        s_nodeLocateTileBusy = false;
        s_nodeLocateTileDone = true;
        s_nodeLocateTileTask = nullptr;
        Serial.println("[map] failed to create live tile task");
    }
}

#if defined(DEVICE_TDECK_PRO)
// Ordered dither for the 1 bpp panel.
//
// LVGL reduces this display's pixels to I1 with a single luminance cut at
// LV_DRAW_SW_I1_LUM_THRESHOLD (127) and no dithering at all. Every surface an
// OpenStreetMap tile is made of sits well above that line -- land 0xF2EFE9 is
// luminance 239, water 0xAAD3DF is 200, building fill 0xD9D0C9 is 211, minor
// roads are pure white -- so the whole tile crosses to white together and only
// label text and the darkest road casings survive. That is a map you cannot
// read.
//
// So do the reduction here instead, while the pixels are still RGB565: stretch
// the narrow band the tiles actually occupy across the full range, then dither
// to pure black and white. What reaches LVGL's threshold afterwards is already
// 0 or 255, so it has nothing left to throw away.
//
// Bayer rather than error diffusion: tiles land one at a time and the canvas is
// memmove'd on every pan, so diffused error would seam at tile edges and change
// pattern as the map moved. The matrix is indexed by each pixel's offset within
// its own tile, which keeps the texture locked to the map content across both.
// Tiles are 256 px, a multiple of 8, so the pattern is continuous across a
// tile boundary as well.
static constexpr uint8_t kNodeLocateBayer8[64] = {
     0, 32,  8, 40,  2, 34, 10, 42,
    48, 16, 56, 24, 50, 18, 58, 26,
    12, 44,  4, 36, 14, 46,  6, 38,
    60, 28, 52, 20, 62, 30, 54, 22,
     3, 35, 11, 43,  1, 33,  9, 41,
    51, 19, 59, 27, 49, 17, 57, 25,
    15, 47,  7, 39, 13, 45,  5, 37,
    63, 31, 55, 23, 61, 29, 53, 21,
};

// Anything darker than this becomes solid ink. Set just under the darkest fill
// worth keeping texture in (motorway, luminance 176) so road casings, rail and
// label text stay solid black while land, water and buildings each land on a
// visibly different density above it.
static constexpr int kNodeLocateInkFloor = 160;

static void nodeLocateDitherTile(const NodeLocateTileJob &job) {
    if (!s_nodeLocateCanvasBuf || s_locateMapW <= 0 || s_locateMapH <= 0) return;

    int x0 = job.drawX;
    int y0 = job.drawY;
    int x1 = job.drawX + kNodeLocateTilePx;
    int y1 = job.drawY + kNodeLocateTilePx;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s_locateMapW) x1 = s_locateMapW;
    if (y1 > s_locateMapH) y1 = s_locateMapH;
    if (x0 >= x1 || y0 >= y1) return;

    for (int y = y0; y < y1; y++) {
        uint16_t *row = (uint16_t *)(s_nodeLocateCanvasBuf->data
                                     + y * s_nodeLocateCanvasBuf->header.stride);
        const int bayerRow = ((y - job.drawY) & 7) * 8;
        for (int x = x0; x < x1; x++) {
            const uint16_t px = row[x];
            const uint8_t r5 = (uint8_t)((px >> 11) & 0x1F);
            const uint8_t g6 = (uint8_t)((px >> 5) & 0x3F);
            const uint8_t b5 = (uint8_t)(px & 0x1F);
            const uint8_t r = (uint8_t)((r5 << 3) | (r5 >> 2));
            const uint8_t g = (uint8_t)((g6 << 2) | (g6 >> 4));
            const uint8_t b = (uint8_t)((b5 << 3) | (b5 >> 2));
            const int lum = (77 * r + 150 * g + 29 * b) >> 8;

            int level = 0;
            if (lum > kNodeLocateInkFloor) {
                level = (lum - kNodeLocateInkFloor) * 255
                        / (255 - kNodeLocateInkFloor);
            }
            // Cell n owns the band [4n, 4n+4); the +2 centres the comparison so
            // a stretched 0 is always black and a stretched 255 always white.
            const int threshold =
                kNodeLocateBayer8[bayerRow + ((x - job.drawX) & 7)] * 4 + 2;
            row[x] = (level > threshold) ? 0xFFFF : 0x0000;
        }
    }
}
#endif  // DEVICE_TDECK_PRO

static bool nodeLocateComposeTile(const NodeLocateTileJob &job, size_t pngBytes) {
    if (!s_nodeLocateCanvas || !lvObjValid(s_nodeLocateCanvas)
        || !s_nodeLocateCanvasBuf || !nodeLocatePngBufferValid(pngBytes)) {
        return false;
    }

    lv_image_dsc_t source = {};
    source.header.magic = LV_IMAGE_HEADER_MAGIC;
    source.data_size = (uint32_t)pngBytes;
    source.data = s_nodeLocateTilePng;

    lv_draw_image_dsc_t imageDsc;
    lv_draw_image_dsc_init(&imageDsc);
    imageDsc.src = &source;
    imageDsc.antialias = 0;

    lv_area_t tileArea = {
        job.drawX,
        job.drawY,
        (int32_t)job.drawX + kNodeLocateTilePx - 1,
        (int32_t)job.drawY + kNodeLocateTilePx - 1,
    };
    lv_layer_t layer;
    lv_canvas_init_layer(s_nodeLocateCanvas, &layer);
    lv_draw_image(&layer, &imageDsc, &tileArea);
    lv_canvas_finish_layer(s_nodeLocateCanvas, &layer);
#if defined(DEVICE_TDECK_PRO)
    nodeLocateDitherTile(job);
#endif
    if (s_nodeLocateAllNodes) {
        nodeMapPaintRect(job.drawX, job.drawY,
                         job.drawX + kNodeLocateTilePx, job.drawY + kNodeLocateTilePx);
    }
    lv_image_cache_drop(&source);
    lv_draw_buf_flush_cache(s_nodeLocateCanvasBuf, nullptr);

    s_nodeLocateHasPixels = true;
    nodeLocateSetStatus(nullptr);
    Serial.printf("[map] live tile draw z=%d x=%ld y=%ld at (%d,%d), %u bytes\n",
                  job.zoom, (long)job.tileX, (long)job.tileY,
                  (int)job.drawX, (int)job.drawY, (unsigned)pngBytes);
    return true;
}

static void serviceNodeLocateLiveMap(uint32_t nowMs) {
    if (s_nodeLocateTileDone) {
        NodeLocateTileJob completed = s_nodeLocateTileCompleted;
        size_t bytes = s_nodeLocateTileCompletedBytes;
        bool ok = s_nodeLocateTileOk;
        s_nodeLocateTileDone = false;

        bool current = s_nodeLocateModal && lvObjValid(s_nodeLocateModal)
                       && completed.generation == s_nodeLocateGeneration;
        if (ok) {
            nodeLocatePersistTile(completed, bytes);
            if (current) nodeLocateComposeTile(completed, bytes);
        } else if (!ok && current) {
            Serial.printf("[map] live tile fetch failed z=%d x=%ld y=%ld\n",
                          completed.zoom, (long)completed.tileX, (long)completed.tileY);
        }
    }

    if (!s_nodeLocateModal || !lvObjValid(s_nodeLocateModal)) return;
    if (s_nodeLocateTileBusy || s_nodeLocateTileDone) return;

    if (s_nodeLocateRefreshPending
        && (int32_t)(nowMs - s_nodeLocateRefreshAtMs) >= 0) {
        if (!nodeLocateTileBufferReady()) {
            s_nodeLocateRefreshPending = false;
            nodeLocateSetStatus(TR("Map memory unavailable"));
            return;
        }
        nodeLocateBuildVisibleTileJobs();
        s_nodeLocateRefreshPending = false;
    }

    if (!s_nodeLocateRefreshPending
        && s_nodeLocateTileJobNext < s_nodeLocateTileJobCount) {
        nodeLocateStartNextTile();
    } else if (!s_nodeLocateRefreshPending
               && s_nodeLocateTileJobCount > 0
               && s_nodeLocateTileJobNext >= s_nodeLocateTileJobCount
               && !s_nodeLocateHasPixels) {
        nodeLocateSetStatus(nodesPanelCanDownloadTiles()
                                ? TR("Map unavailable")
                                : TR("Waiting for Wi-Fi..."));
    }
}

static void nodeMapFitAll();

// Points the node map's pin at this node. Read afresh each time, since a GPS
// fix can arrive or move while the map is open. False with no position.
static bool nodeMapLoadSelfPin() {
    double lat = 0.0;
    double lon = 0.0;
    s_locateHasPin = nodeLosSelfPosition(lat, lon);
    if (s_locateHasPin) {
        s_locateNodeLat = (float)lat;
        s_locateNodeLon = (float)lon;
    }
    return s_locateHasPin;
}

// The location button, and H/C. Puts the node back under the middle of the map
// box at the current zoom. On the node map, centres on this node at
// kNodeMapSelfZoom instead, or frames every node when it has no position.
static void nodeLocateCenterOnNode() {
    if (s_locateMapW <= 0 || s_locateMapH <= 0) return;
    if (s_nodeLocateAllNodes) {
        if (!nodeMapLoadSelfPin()) {
            nodeMapFitAll();
            return;
        }
        s_nodeLocateZoom = kNodeMapSelfZoom;
    }
    nodeLocateLatLonToWorld(s_locateNodeLat, s_locateNodeLon, s_nodeLocateZoom,
                            s_nodeLocateCenterWorldX, s_nodeLocateCenterWorldY);
    nodeLocateNormalizeCenter();
    nodeLocateRequestLiveTiles(0, true);
}

static void nodeLocateZoomStep(int delta) {
    if (!delta || s_locateMapW <= 0 || s_locateMapH <= 0) return;
    int nextZoom = s_nodeLocateZoom + delta;
    if (nextZoom < kNodeLocateZoomMin) nextZoom = kNodeLocateZoomMin;
    if (nextZoom > kNodeLocateZoomMax) nextZoom = kNodeLocateZoomMax;
    if (nextZoom == s_nodeLocateZoom) return;

    double centerLat = 0.0;
    double centerLon = 0.0;
    nodeLocateWorldToLatLon(s_nodeLocateCenterWorldX, s_nodeLocateCenterWorldY,
                            s_nodeLocateZoom, centerLat, centerLon);
    s_nodeLocateZoom = nextZoom;
    nodeLocateLatLonToWorld(centerLat, centerLon, s_nodeLocateZoom,
                            s_nodeLocateCenterWorldX, s_nodeLocateCenterWorldY);
    nodeLocateNormalizeCenter();
    nodeLocateRequestLiveTiles(0, true);
}

static void nodeLocatePanBy(int dx, int dy) {
    if (!dx && !dy) return;
    const double oldY = s_nodeLocateCenterWorldY;
    s_nodeLocateCenterWorldX -= (double)dx;
    s_nodeLocateCenterWorldY -= (double)dy;
    nodeLocateNormalizeCenter();

    int actualDy = (int)lround(oldY - s_nodeLocateCenterWorldY);
    nodeLocateShiftLiveCanvas(dx, actualDy);
    nodeLocateRequestLiveTiles(kNodeLocatePanDebounceMs, false);
}

static void nodeLocateOnDetailMapUpdated() {
    // The live Locate viewport does not consume persistent detail-map updates.
}

#if HAS_TOUCH
// Drag to pan. lv_indev_get_vect() is the movement since the last event, which
// is exactly the increment wanted here — tracking a press origin ourselves
// would have to cope with the indev's own drag threshold and gesture handling.
static void onNodeLocateMapPressing(lv_event_t *e) {
    LV_UNUSED(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t vect;
    lv_indev_get_vect(indev, &vect);
    nodeLocatePanBy((int)vect.x, (int)vect.y);
}

static void onNodeLocateZoomInPressed(lv_event_t *e) {
    LV_UNUSED(e);
    nodeLocateZoomStep(1);
}

static void onNodeLocateZoomOutPressed(lv_event_t *e) {
    LV_UNUSED(e);
    nodeLocateZoomStep(-1);
}

static void onNodeLocateCenterPressed(lv_event_t *e) {
    LV_UNUSED(e);
    nodeLocateCenterOnNode();
}
#endif  // HAS_TOUCH

static void onNodeLocateClosePressed(lv_event_t *e) {
    LV_UNUSED(e);
    closeNodeLocateModal();
}

// Builds the modal around titleText: map box, canvas, chips, controls, and the
// pin when there is one node to show. Sets s_locateMapW/H; the caller picks the
// view and requests tiles. Returns false when nothing was built.
static bool nodeLocateBuildModal(const char *titleText, bool allNodes) {
    if (!s_rootScreen) return false;
    if (s_nodeLocateModal || s_nodeLocateBackdrop) return false;
    s_nodeLocateAllNodes = allNodes;

    const int scrW = lv_disp_get_hor_res(NULL);
    const int scrH = lv_disp_get_ver_res(NULL);
    // Edge to edge on every board (#116 began it on the P4). This was a card at
    // most 300 px wide, sized for 320-wide panels: wider ones kept a band of
    // backdrop either side -- on the CrowPanel 5"'s 800x480 the map was a strip
    // down the middle -- and even at 320 it spent a frame and a title row. The
    // title rides on the map as a chip instead and the canvas gets the whole
    // display, Locate and the node map both. The canvas is RGB565 from the LVGL
    // pool, which is 2 MB of PSRAM on every board that compiles maps
    // (lv_conf.h): 150 KB at 320x240, 300 KB at 480x320, 750 KB at 800x480.
    const int modalW = scrW;
#if UI_TOUCH_ONLY_PROFILE
    const int closeXSize = kHeltecCloseXSize;   // the X that is the way back
#else
    // A key is the way back (the close key or Enter; see the key handler), as
    // from every modal on a keyboard build. There is no outside left to tap.
    const int closeXSize = 0;
#endif
    // Full viewport height. The map box is the modal's only flex-grow child, so
    // every row reclaimed here lands in the canvas rather than in padding.
    //
    // This used to be capped at 224. Among the boards that compile Locate at
    // all the T-Deck Pro is the only portrait panel -- HAS_STATE_MAPS is 0 on
    // the Cardputer and on both Heltecs -- so the cap bound nothing except that
    // one board, where it left 96 px of a 320 px panel unused. Every landscape
    // panel here is already shorter than the cap was.
    const int modalH = scrH;

    const bool lightUi = (s_cfg.uiMode == UI_MODE_LIGHT);
#if defined(DEVICE_TDECK_PRO)
    // Ink on paper. Every colour in the branch below is a blue whose luminance
    // falls under LV_DRAW_SW_I1_LUM_THRESHOLD, and this panel is 1 bpp: chrome,
    // borders, chips and button faces all reduce to the same black, and the
    // opacities that separate them on a TFT have no middle tone to land on.
    LV_UNUSED(lightUi);
    const lv_color_t modalBg = lv_color_hex(0xFFFFFF);
    const lv_color_t titleColor = lv_color_hex(0x000000);
    const lv_color_t bodyColor = lv_color_hex(0x000000);
    // The status and attribution chips sit on the map, so they stay opaque: a
    // translucent chip dithers through and takes its own text down with it.
    const lv_color_t chipBg = lv_color_hex(0xFFFFFF);
    const lv_color_t chipText = lv_color_hex(0x000000);
    const lv_opa_t chipOpa = LV_OPA_COVER;
#else
    const lv_color_t modalBg = lightUi ? lv_color_hex(0xEAF1FB) : lv_color_hex(0x0E285B);
    const lv_color_t titleColor = lightUi ? lv_color_hex(0x16233A) : lv_color_hex(0xD9E8FF);
    const lv_color_t bodyColor = lightUi ? lv_color_hex(0x13243D) : lv_color_hex(0xE8F1FF);
    const lv_color_t chipBg = lightUi ? lv_color_hex(0xEAF1FB) : lv_color_hex(0x0E285B);
    const lv_color_t chipText = lightUi ? lv_color_hex(0x31445E) : lv_color_hex(0xD9E8FF);
    const lv_opa_t chipOpa = LV_OPA_70;
#endif

    s_nodeLocateBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_nodeLocateBackdrop, scrW, scrH);
    lv_obj_align(s_nodeLocateBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_nodeLocateBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_nodeLocateBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_nodeLocateBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_nodeLocateBackdrop, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_nodeLocateBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_nodeLocateBackdrop, 0, 0);

    s_nodeLocateModal = lv_obj_create(s_nodeLocateBackdrop);
    lv_obj_set_size(s_nodeLocateModal, modalW, modalH);
    lv_obj_align(s_nodeLocateModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_nodeLocateModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_nodeLocateModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_nodeLocateModal, modalBg, 0);
    lv_obj_set_style_bg_opa(s_nodeLocateModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_nodeLocateModal, 0, 0);
    lv_obj_set_style_pad_all(s_nodeLocateModal, 0, 0);
    lv_obj_set_style_pad_row(s_nodeLocateModal, 0, 0);
    lv_obj_set_style_radius(s_nodeLocateModal, 0, 0);
    lv_obj_set_flex_flow(s_nodeLocateModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_nodeLocateModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_nodeLocateBackdrop);

    // The map area. Its own container so the renderer can position the image and
    // the pin in coordinates that belong to nobody else.
    lv_obj_t *mapBox = lv_obj_create(s_nodeLocateModal);
    lv_obj_set_width(mapBox, lv_pct(100));
    lv_obj_set_flex_grow(mapBox, 1);
    lv_obj_clear_flag(mapBox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(mapBox, nodeLocateCanvasBgColor(), 0);
    lv_obj_set_style_bg_opa(mapBox, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(mapBox, 0, 0);
    lv_obj_set_style_pad_all(mapBox, 0, 0);
    lv_obj_set_style_radius(mapBox, 0, 0);
    s_nodeLocateMapBox = mapBox;
#if HAS_TOUCH
    // The drag surface. The layers above it — tile layer, image, pin — are
    // decoration and must not swallow the press, or a drag begun on the map
    // itself (the usual case) would never reach this handler.
    lv_obj_add_flag(mapBox, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(mapBox, onNodeLocateMapPressing, LV_EVENT_PRESSING, nullptr);
#endif

#if HAS_TOUCH
    // No control strip under the map. Controls are overlaid on the map itself,
    // so the whole reclaimed height becomes viewport.
#endif

#if UI_TOUCH_ONLY_PROFILE
    {
        lv_obj_t *closeX = appendHeltecCloseX(s_nodeLocateModal, onNodeLocateClosePressed);
        // On the map itself, in from the corner like the chips.
        if (closeX) lv_obj_align(closeX, LV_ALIGN_TOP_RIGHT, -4, 4);
    }
#endif

    // Build one RGB565 viewport. Live PNG tiles are decoded into this canvas
    // one at a time, so decoded memory stays bounded regardless of how many
    // tile boundaries the viewport crosses.
    lv_obj_update_layout(s_nodeLocateModal);
    const int mapW = lv_obj_get_width(mapBox);
    const int mapH = lv_obj_get_height(mapBox);

    s_nodeLocateTileLayer = lv_obj_create(mapBox);
    lv_obj_remove_style_all(s_nodeLocateTileLayer);
    lv_obj_clear_flag(s_nodeLocateTileLayer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_nodeLocateTileLayer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(s_nodeLocateTileLayer, 0, 0);
    lv_obj_set_size(s_nodeLocateTileLayer, mapW, mapH);

    s_nodeLocateCanvasBuf = lv_draw_buf_create((uint32_t)mapW, (uint32_t)mapH,
                                               LV_COLOR_FORMAT_RGB565,
                                               LV_STRIDE_AUTO);
    if (s_nodeLocateCanvasBuf) {
        s_nodeLocateCanvas = lv_canvas_create(s_nodeLocateTileLayer);
        lv_canvas_set_draw_buf(s_nodeLocateCanvas, s_nodeLocateCanvasBuf);
#if UI_COORD_SCALE > 1
        // The buffer and the tile math are in layout pixels; stretch the canvas
        // over the box, which is in LVGL's (hal/ui_coord_shim.h).
        lv_image_set_inner_align(s_nodeLocateCanvas, LV_IMAGE_ALIGN_STRETCH);
        lv_obj_set_size(s_nodeLocateCanvas, mapW, mapH);
#endif
        lv_obj_set_pos(s_nodeLocateCanvas, 0, 0);
        lv_obj_clear_flag(s_nodeLocateCanvas, LV_OBJ_FLAG_CLICKABLE);
        lv_canvas_fill_bg(s_nodeLocateCanvas, nodeLocateCanvasBgColor(),
                          LV_OPA_COVER);
    }
    s_nodeLocateStatusLabel = lv_label_create(s_nodeLocateTileLayer);
    lv_obj_set_style_text_font(s_nodeLocateStatusLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_nodeLocateStatusLabel, bodyColor, 0);
    lv_obj_set_style_bg_color(s_nodeLocateStatusLabel, chipBg, 0);
    lv_obj_set_style_bg_opa(s_nodeLocateStatusLabel, chipOpa, 0);
    lv_obj_set_style_pad_all(s_nodeLocateStatusLabel, 3, 0);
    lv_obj_set_style_radius(s_nodeLocateStatusLabel, 3, 0);
    lv_label_set_text(s_nodeLocateStatusLabel,
                      s_nodeLocateCanvasBuf ? TR("Loading map...") : TR("Map memory unavailable"));
    lv_obj_center(s_nodeLocateStatusLabel);

    lv_obj_t *attribution = lv_label_create(s_nodeLocateTileLayer);
    lv_obj_set_style_text_font(attribution, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(attribution, chipText, 0);
    lv_obj_set_style_bg_color(attribution, chipBg, 0);
    lv_obj_set_style_bg_opa(attribution, chipOpa, 0);
    lv_obj_set_style_pad_left(attribution, 2, 0);
    lv_obj_set_style_pad_right(attribution, 2, 0);
    lv_label_set_text(attribution, TR("(c) OpenStreetMap"));
    lv_obj_align(attribution, LV_ALIGN_BOTTOM_LEFT, 2, -2);

    // The title as a chip, top left, stopping short of whatever holds the top
    // right corner: the X, or the zoom column on a keyboard build with touch
    // (narrowed to that below, once the column has its width).
    lv_obj_t *titleChip = lv_label_create(s_nodeLocateTileLayer);
    lv_obj_set_style_text_font(titleChip, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(titleChip, titleColor, 0);
    lv_obj_set_style_bg_color(titleChip, chipBg, 0);
    lv_obj_set_style_bg_opa(titleChip, chipOpa, 0);
    lv_obj_set_style_pad_all(titleChip, 3, 0);
    lv_obj_set_style_radius(titleChip, 3, 0);
    lv_label_set_long_mode(titleChip, LV_LABEL_LONG_DOT);
    lv_obj_set_width(titleChip, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(titleChip, mapW - closeXSize - 20, 0);
    setLabelTextEmojiSafe(titleChip, titleText);
    lv_obj_align(titleChip, LV_ALIGN_TOP_LEFT, 4, 4);

    // The pin: the node being located or, on the node map, this node. The node
    // map draws every other node into the canvas (nodeMapPaintRect()).
    {
        // A map pin rather than a dot: a round head over a short stem, tip down.
        // Built from two plain objects because there is no pin glyph to lean on —
        // this build already falls back LV_SYMBOL_GPS to LV_SYMBOL_DRIVE — and a
        // canvas would cost a buffer for something this simple.
        //
        // Red with a white outline on both parts. The outline is what keeps it
        // readable over dark map features; red alone disappears into a highway.
        s_nodeLocateMarker = lv_obj_create(s_nodeLocateTileLayer);
        lv_obj_remove_style_all(s_nodeLocateMarker);
        lv_obj_clear_flag(s_nodeLocateMarker, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(s_nodeLocateMarker, kLocatePinW, kLocatePinH);
        lv_obj_clear_flag(s_nodeLocateMarker, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *pinHead = lv_obj_create(s_nodeLocateMarker);
        lv_obj_remove_style_all(pinHead);
        lv_obj_clear_flag(pinHead, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(pinHead, kLocatePinW, kLocatePinW);
        lv_obj_set_pos(pinHead, 0, 0);
        lv_obj_set_style_radius(pinHead, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(pinHead, lv_color_hex(0xE04A4A), 0);
        lv_obj_set_style_bg_opa(pinHead, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(pinHead, 2, 0);
        lv_obj_set_style_border_color(pinHead, lv_color_hex(0xFFFFFF), 0);

        lv_obj_t *pinStem = lv_obj_create(s_nodeLocateMarker);
        lv_obj_remove_style_all(pinStem);
        lv_obj_clear_flag(pinStem, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(pinStem, 5, kLocatePinH - kLocatePinW + 2);
        // Overlapping the head by two rows hides the seam between them.
        lv_obj_set_pos(pinStem, (kLocatePinW - 5) / 2, kLocatePinW - 2);
        lv_obj_set_style_radius(pinStem, 1, 0);
        lv_obj_set_style_bg_color(pinStem, lv_color_hex(0xE04A4A), 0);
        lv_obj_set_style_bg_opa(pinStem, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(pinStem, 1, 0);
        lv_obj_set_style_border_color(pinStem, lv_color_hex(0xFFFFFF), 0);

        lv_obj_add_flag(s_nodeLocateMarker, LV_OBJ_FLAG_HIDDEN);
    }

#if HAS_TOUCH
    // Zoom / center controls overlaid on the map. Twice the size they started
    // at (24x18, glyphs at 10), which was smaller than the fingertip aiming at
    // them; a column of these still leaves most of the map clear.
    {
        lv_obj_t *ctlCol = lv_obj_create(mapBox);
        lv_obj_set_size(ctlCol, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_clear_flag(ctlCol, LV_OBJ_FLAG_SCROLLABLE);
#if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_bg_color(ctlCol, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(ctlCol, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(ctlCol, lv_color_hex(0x000000), 0);
#else
        lv_obj_set_style_bg_color(ctlCol, lv_color_hex(0x102D57), 0);
        lv_obj_set_style_bg_opa(ctlCol, LV_OPA_70, 0);
        lv_obj_set_style_border_color(ctlCol, lv_color_hex(0x3E649B), 0);
#endif
        lv_obj_set_style_border_width(ctlCol, 1, 0);
        lv_obj_set_style_radius(ctlCol, 4, 0);
        lv_obj_set_style_pad_all(ctlCol, 3, 0);
        lv_obj_set_style_pad_row(ctlCol, 4, 0);
        lv_obj_set_flex_flow(ctlCol, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(ctlCol, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        struct Local {
            static lv_obj_t *btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb) {
                lv_obj_t *b = lv_btn_create(parent);
                lv_obj_set_style_pad_all(b, 0, 0);
                lv_obj_set_style_shadow_width(b, 0, 0);
                lv_obj_set_style_border_width(b, 1, 0);
                lv_obj_t *l = lv_label_create(b);
#if defined(DEVICE_TDECK_PRO)
                // A white face inside a black outline, with a black glyph. The
                // TFT styling below is a dark fill inside a mid-blue border and
                // a near-white glyph: on 1 bpp the fill and the border both
                // reduce to black, and montserrat_10's thin strokes lose their
                // antialiased edges to the same threshold, so the whole control
                // arrives as one filled rectangle. The larger face and the 12 px
                // font are what keep the glyph legible once it is ink on white.
                lv_obj_set_size(b, 56, 44);
                lv_obj_set_style_radius(b, 3, 0);
                lv_obj_set_style_bg_color(b, lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
                lv_obj_set_style_border_color(b, lv_color_hex(0x000000), 0);
                lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
                lv_obj_set_style_text_color(l, lv_color_hex(0x000000), 0);
#else
                lv_obj_set_size(b, 48, 36);
                lv_obj_set_style_radius(b, 5, 0);
                lv_obj_set_style_bg_color(b, lv_color_hex(0x16386F), 0);
                lv_obj_set_style_bg_opa(b, LV_OPA_80, 0);
                lv_obj_set_style_border_color(b, lv_color_hex(0x335D9D), 0);
                lv_obj_set_style_text_font(l, &lv_font_montserrat_18, 0);
                lv_obj_set_style_text_color(l, lv_color_hex(0xE8F1FF), 0);
#endif
                lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
                lv_label_set_text(l, text);
                lv_obj_center(l);
                return b;
            }
        };

        Local::btn(ctlCol, LV_SYMBOL_PLUS, onNodeLocateZoomInPressed);
        s_nodeLocateZoomLabel = lv_label_create(ctlCol);
#if defined(DEVICE_TDECK_PRO)
        // Matches the button face above and below it.
        lv_obj_set_width(s_nodeLocateZoomLabel, 56);
        lv_obj_set_style_text_font(s_nodeLocateZoomLabel, &lv_font_montserrat_20, 0);
#else
        lv_obj_set_width(s_nodeLocateZoomLabel, 48);
        lv_obj_set_style_text_font(s_nodeLocateZoomLabel, &lv_font_montserrat_16, 0);
#endif
        lv_obj_set_style_text_color(s_nodeLocateZoomLabel, bodyColor, 0);
        lv_obj_set_style_text_align(s_nodeLocateZoomLabel, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text_fmt(s_nodeLocateZoomLabel, "%d", kNodeLocateZoomDefault);
        Local::btn(ctlCol, LV_SYMBOL_MINUS, onNodeLocateZoomOutPressed);
        Local::btn(ctlCol, LV_SYMBOL_GPS, onNodeLocateCenterPressed);

        // Where there is a close X it holds the corner, so the column starts
        // under it. Where there is not, the column takes the corner and the
        // title chip stops short of it instead.
        lv_obj_align(ctlCol, LV_ALIGN_TOP_RIGHT, -4, closeXSize ? 4 + closeXSize + 4 : 4);
        lv_obj_move_foreground(ctlCol);
        if (!closeXSize) {
            lv_obj_update_layout(ctlCol);
            lv_obj_set_style_max_width(titleChip, mapW - lv_obj_get_width(ctlCol) - 20, 0);
        }
    }
#endif

    s_locateMapW = mapW;
    s_locateMapH = mapH;
    return true;
}

static void openNodeLocateModal(uint32_t nodeId) {
    if (nodeId == 0) return;
    const NodeEntry *node = Nodes.find(nodeId);
    if (!node || !node->hasPosition || (node->latI == 0 && node->lonI == 0)) return;

    const float lat = (float)node->latI / 10000000.0f;
    const float lon = (float)node->lonI / 10000000.0f;

    char who[40];
    nodesActionTitleLabel(nodeId, who, sizeof(who));
    char titleBuf[56];
    snprintf(titleBuf, sizeof(titleBuf), TR("Locate: %s"), who);
    if (!nodeLocateBuildModal(titleBuf, false)) return;
    s_nodeLocateNodeId = nodeId;

    // Zoom 13 is the default. The selected node is the canonical view center,
    // so its pin starts exactly at the center of the map viewport.
    s_locateNodeLat = lat;
    s_locateNodeLon = lon;
    s_locateHasPin = true;
    s_nodeLocateZoom = kNodeLocateZoomDefault;
    nodeLocateLatLonToWorld(lat, lon, s_nodeLocateZoom,
                            s_nodeLocateCenterWorldX, s_nodeLocateCenterWorldY);
    nodeLocateNormalizeCenter();
    s_nodeLocateHasPixels = false;
    if (s_nodeLocateCanvasBuf) nodeLocateRequestLiveTiles(0, false);
    else nodeLocateUpdateLiveMarker();

    // Bottom status text removed so the map can take the reclaimed space.
}
// ── Node map overlay ──
// Heat kernel radius in pixels. Fixed on screen rather than in metres, so a
// node reads as the same size blob at every zoom and clusters merge as the
// map zooms out -- which is what makes it a density map.
static constexpr int kNodeMapHeatR = 18;
// One node's peak intensity. Lone nodes land mid-ramp (teal-green); two
// overlapping go yellow-orange and three or more saturate to red.
static constexpr int kNodeMapHeatPeak = 110;
#if !defined(DEVICE_TDECK_PRO)
static uint8_t s_nodeMapKernel[(2 * kNodeMapHeatR + 1) * (2 * kNodeMapHeatR + 1)];
static bool s_nodeMapKernelReady = false;

static void nodeMapBuildKernel() {
    if (s_nodeMapKernelReady) return;
    const int side = 2 * kNodeMapHeatR + 1;
    const float r2 = (float)(kNodeMapHeatR * kNodeMapHeatR);
    for (int y = -kNodeMapHeatR; y <= kNodeMapHeatR; y++) {
        for (int x = -kNodeMapHeatR; x <= kNodeMapHeatR; x++) {
            float f = 1.0f - (float)(x * x + y * y) / r2;
            // Quartic falloff: a soft edge without a Gaussian's long tail.
            f = (f > 0.0f) ? f * f : 0.0f;
            s_nodeMapKernel[(y + kNodeMapHeatR) * side + (x + kNodeMapHeatR)] =
                (uint8_t)lroundf(f * (float)kNodeMapHeatPeak);
        }
    }
    s_nodeMapKernelReady = true;
}
#endif

// Positions the points and rebuilds the intensity buffer for the current view.
// A no-op when the view has not moved since the last build.
static void nodeMapRebuildOverlay() {
    if (!s_nodeLocateAllNodes || s_locateMapW <= 0 || s_locateMapH <= 0) return;
    if (s_nodeLocateZoom == s_nodeMapHeatZoom
        && s_nodeLocateCenterWorldX == s_nodeMapHeatCenterX
        && s_nodeLocateCenterWorldY == s_nodeMapHeatCenterY) {
        return;
    }
    s_nodeMapHeatZoom = s_nodeLocateZoom;
    s_nodeMapHeatCenterX = s_nodeLocateCenterWorldX;
    s_nodeMapHeatCenterY = s_nodeLocateCenterWorldY;

    const double worldSize = nodeLocateWorldSize(s_nodeLocateZoom);
    for (int i = 0; i < s_nodeMapPointCount; i++) {
        double wx = 0.0;
        double wy = 0.0;
        nodeLocateLatLonToWorld(s_nodeMapPoints[i].lat, s_nodeMapPoints[i].lon,
                                s_nodeLocateZoom, wx, wy);
        double dx = wx - s_nodeLocateCenterWorldX;
        if (dx > worldSize * 0.5) dx -= worldSize;
        if (dx < -worldSize * 0.5) dx += worldSize;
        const double sx = (double)s_locateMapW * 0.5 + dx;
        const double sy = (double)s_locateMapH * 0.5 + (wy - s_nodeLocateCenterWorldY);
        // Kept while any of its blob could reach the viewport.
        if (sx < -kNodeMapHeatR || sx > s_locateMapW + kNodeMapHeatR
            || sy < -kNodeMapHeatR || sy > s_locateMapH + kNodeMapHeatR) {
            s_nodeMapScreenX[i] = INT16_MIN;
            s_nodeMapScreenY[i] = INT16_MIN;
        } else {
            s_nodeMapScreenX[i] = (int16_t)lround(sx);
            s_nodeMapScreenY[i] = (int16_t)lround(sy);
        }
    }

#if !defined(DEVICE_TDECK_PRO)
    const size_t heatBytes = (size_t)s_locateMapW * (size_t)s_locateMapH;
    if (!s_nodeMapHeat) {
        s_nodeMapHeat = (uint8_t *)heap_caps_malloc(heatBytes,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_nodeMapHeat) {
            s_nodeMapHeat = (uint8_t *)heap_caps_malloc(heatBytes, MALLOC_CAP_8BIT);
        }
        // Without it the map still shows every node as a dot, just no heat.
        if (!s_nodeMapHeat) Serial.println("[map] node heat buffer unavailable");
    }
    if (!s_nodeMapHeat) return;
    memset(s_nodeMapHeat, 0, heatBytes);
    nodeMapBuildKernel();
    const int side = 2 * kNodeMapHeatR + 1;
    for (int i = 0; i < s_nodeMapPointCount; i++) {
        if (s_nodeMapScreenX[i] == INT16_MIN) continue;
        const int cx = s_nodeMapScreenX[i];
        const int cy = s_nodeMapScreenY[i];
        const int y0 = max(0, cy - kNodeMapHeatR);
        const int y1 = min(s_locateMapH - 1, cy + kNodeMapHeatR);
        const int x0 = max(0, cx - kNodeMapHeatR);
        const int x1 = min(s_locateMapW - 1, cx + kNodeMapHeatR);
        for (int y = y0; y <= y1; y++) {
            uint8_t *row = s_nodeMapHeat + (size_t)y * s_locateMapW;
            const uint8_t *k = s_nodeMapKernel
                               + (y - cy + kNodeMapHeatR) * side + (kNodeMapHeatR - cx);
            for (int x = x0; x <= x1; x++) {
                const int v = row[x] + k[x];
                row[x] = (uint8_t)(v > 255 ? 255 : v);
            }
        }
    }
#endif
}

#if !defined(DEVICE_TDECK_PRO)
// Intensity to colour: blue, cyan, green, yellow, red at 0, 64, 128, 192, 255.
static void nodeMapHeatColor(uint8_t v, uint8_t &r, uint8_t &g, uint8_t &b) {
    static constexpr uint8_t kStops[5][3] = {
        { 48,  96, 255},
        {  0, 224, 255},
        { 64, 236,  72},
        {255, 224,   0},
        {255,  48,  32},
    };
    const int seg = (v >= 255) ? 3 : v / 64;
    const int t = (v >= 255) ? 64 : v - seg * 64;   // 0..64 within the segment
    r = (uint8_t)(kStops[seg][0] + (kStops[seg + 1][0] - kStops[seg][0]) * t / 64);
    g = (uint8_t)(kStops[seg][1] + (kStops[seg + 1][1] - kStops[seg][1]) * t / 64);
    b = (uint8_t)(kStops[seg][2] + (kStops[seg + 1][2] - kStops[seg][2]) * t / 64);
}
#endif

// Paints the overlay over canvas pixels [x0,x1) x [y0,y1) that have just been
// written. Each pixel must be painted exactly once after it is written, or the
// heat would build up on it.
static void nodeMapPaintRect(int x0, int y0, int x1, int y1) {
    if (!s_nodeLocateAllNodes || !s_nodeLocateCanvasBuf) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s_locateMapW) x1 = s_locateMapW;
    if (y1 > s_locateMapH) y1 = s_locateMapH;
    if (x0 >= x1 || y0 >= y1) return;
    const uint32_t stride = s_nodeLocateCanvasBuf->header.stride;

#if !defined(DEVICE_TDECK_PRO)
    if (s_nodeMapHeat) {
        for (int y = y0; y < y1; y++) {
            uint16_t *px = (uint16_t *)(s_nodeLocateCanvasBuf->data + y * stride);
            const uint8_t *heat = s_nodeMapHeat + (size_t)y * s_locateMapW;
            for (int x = x0; x < x1; x++) {
                const uint8_t v = heat[x];
                if (!v) continue;
                // Fades in from nothing at the blob's edge and stops short of
                // opaque, so the streets under a hot spot still show.
                const int a = (v * 3 > 190) ? 190 : v * 3;
                uint8_t hr, hg, hb;
                nodeMapHeatColor(v, hr, hg, hb);
                const uint16_t p = px[x];
                const int r = ((p >> 11) & 0x1F) << 3;
                const int g = ((p >> 5) & 0x3F) << 2;
                const int b = (p & 0x1F) << 3;
                const int nr = r + (hr - r) * a / 255;
                const int ng = g + (hg - g) * a / 255;
                const int nb = b + (hb - b) * a / 255;
                px[x] = (uint16_t)(((nr >> 3) << 11) | ((ng >> 2) << 5) | (nb >> 3));
            }
        }
    }
#endif

    // A crisp dot on each node, so single nodes are pinpointed rather than
    // only implied by their blob. Outlines first, then cores, so one node's
    // outline never cuts into a neighbour's core.
#if defined(DEVICE_TDECK_PRO)
    // Black core in a white ring: the ring is what separates it from the
    // dithered map, which is black and white too.
    static constexpr int kRingR2 = 25;
    static constexpr int kCoreR2 = 10;
    static constexpr uint16_t kRingPx = 0xFFFF;
    static constexpr uint16_t kCorePx = 0x0000;
#else
    static constexpr int kRingR2 = 8;
    static constexpr int kCoreR2 = 3;
    static constexpr uint16_t kRingPx = 0x10C5;   // near-black navy
    static constexpr uint16_t kCorePx = 0xFFFF;
#endif
    for (int pass = 0; pass < 2; pass++) {
        const int r2 = pass ? kCoreR2 : kRingR2;
        const uint16_t color = pass ? kCorePx : kRingPx;
        const int r = (int)ceilf(sqrtf((float)r2));
        for (int i = 0; i < s_nodeMapPointCount; i++) {
            if (s_nodeMapScreenX[i] == INT16_MIN) continue;
            const int cx = s_nodeMapScreenX[i];
            const int cy = s_nodeMapScreenY[i];
            const int dy0 = max(y0, cy - r);
            const int dy1 = min(y1 - 1, cy + r);
            const int dx0 = max(x0, cx - r);
            const int dx1 = min(x1 - 1, cx + r);
            for (int y = dy0; y <= dy1; y++) {
                uint16_t *px = (uint16_t *)(s_nodeLocateCanvasBuf->data + y * stride);
                for (int x = dx0; x <= dx1; x++) {
                    const int ddx = x - cx;
                    const int ddy = y - cy;
                    if (ddx * ddx + ddy * ddy <= r2) px[x] = color;
                }
            }
        }
    }
}

// Frames every node: the closest zoom at which their bounding box fits inside
// the viewport with a margin, centred on it. Does not try to handle a set of
// nodes straddling the antimeridian, which frames as the long way round.
static void nodeMapFitAll() {
    if (s_locateMapW <= 0 || s_locateMapH <= 0) return;
    // Close enough to see streets, without opening a lone node at building level.
    static constexpr int kFitZoomMax = 15;
    static constexpr int kFitMargin = 28;

    if (s_nodeMapPointCount == 0) {
        // Nothing to frame. Our own node if it has a position, else the world.
        if (s_locateHasPin) {
            s_nodeLocateZoom = kNodeLocateZoomDefault;
            nodeLocateLatLonToWorld(s_locateNodeLat, s_locateNodeLon,
                                    s_nodeLocateZoom, s_nodeLocateCenterWorldX,
                                    s_nodeLocateCenterWorldY);
        } else {
            s_nodeLocateZoom = kNodeLocateZoomMin;
            nodeLocateLatLonToWorld(0.0, 0.0, s_nodeLocateZoom,
                                    s_nodeLocateCenterWorldX, s_nodeLocateCenterWorldY);
        }
        nodeLocateNormalizeCenter();
        nodeLocateRequestLiveTiles(0, true);
        return;
    }

    double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
    for (int i = 0; i < s_nodeMapPointCount; i++) {
        double wx = 0.0;
        double wy = 0.0;
        nodeLocateLatLonToWorld(s_nodeMapPoints[i].lat, s_nodeMapPoints[i].lon, 0, wx, wy);
        if (i == 0 || wx < minX) minX = wx;
        if (i == 0 || wx > maxX) maxX = wx;
        if (i == 0 || wy < minY) minY = wy;
        if (i == 0 || wy > maxY) maxY = wy;
    }

    const double fitW = (double)max(1, s_locateMapW - 2 * kFitMargin);
    const double fitH = (double)max(1, s_locateMapH - 2 * kFitMargin);
    int zoom = kNodeLocateZoomMin;
    for (int z = kFitZoomMax; z > kNodeLocateZoomMin; z--) {
        const double scale = ldexp(1.0, z);
        if ((maxX - minX) * scale <= fitW && (maxY - minY) * scale <= fitH) {
            zoom = z;
            break;
        }
    }
    s_nodeLocateZoom = zoom;
    const double scale = ldexp(1.0, zoom);
    s_nodeLocateCenterWorldX = (minX + maxX) * 0.5 * scale;
    s_nodeLocateCenterWorldY = (minY + maxY) * 0.5 * scale;
    nodeLocateNormalizeCenter();
    nodeLocateRequestLiveTiles(0, true);
}

// Nodes -> Map. Every node in the table with a position, at once.
static void openNodesMapModal() {
    // Checked before the points are touched: they belong to an open map.
    if (!s_rootScreen || s_nodeLocateModal || s_nodeLocateBackdrop) return;
    s_nodeMapPointCount = 0;
    const int total = min(Nodes.count(), (int)MAX_NODES);
    for (int i = 0; i < total; i++) {
        const NodeEntry *n = Nodes.at(i);
        if (!n || n->nodeId == 0 || !n->hasPosition) continue;
        if (n->latI == 0 && n->lonI == 0) continue;
        s_nodeMapPoints[s_nodeMapPointCount].lat = (float)n->latI / 10000000.0f;
        s_nodeMapPoints[s_nodeMapPointCount].lon = (float)n->lonI / 10000000.0f;
        s_nodeMapPointCount++;
    }
    const int located = s_nodeMapPointCount;

    char titleBuf[48];
    snprintf(titleBuf, sizeof(titleBuf), TR("Node map: %d located"), located);
    if (!nodeLocateBuildModal(titleBuf, true)) {
        s_nodeMapPointCount = 0;
        return;
    }
    s_nodeMapHeatZoom = -1;
    s_nodeLocateHasPixels = false;
    nodeMapLoadSelfPin();
    if (s_nodeLocateCanvasBuf) {
        nodeMapFitAll();
    } else {
        nodeLocateUpdateLiveMarker();
    }
}
#endif  // HAS_NODE_LOCATE

#if HAS_NODE_LOS || HAS_WEATHER || HAS_NODE_LOCATE
// Where this node thinks it is: the GPS fix when there is one, otherwise the
// configured or last-known position, otherwise nothing.
//
// Outside the feature blocks because several ask it — LOS for one end of the
// path, the weather screen for the only end it has, the node map for its pin.
// It lived inside the LOS block when LOS was the only caller; leaving it there
// would have made the features' gates load-bearing for each other, and a board
// that ever wanted one without the others would have found out at link time.
static bool nodeLosSelfPosition(double &lat, double &lon) {
#if HAS_GPS
    if (gpsHasFix() && (gpsLatI() != 0 || gpsLonI() != 0)) {
        lat = (double)gpsLatI() / 1e7;
        lon = (double)gpsLonI() / 1e7;
        return true;
    }
#endif
    if (s_cfg.latI != 0 || s_cfg.lonI != 0) {
        lat = (double)s_cfg.latI / 1e7;
        lon = (double)s_cfg.lonI / 1e7;
        return true;
    }
    return false;
}

#endif

#if HAS_NODE_LOS
// ── LOS (terrain line of sight) ──────────────────────────────────────────────
// Node Actions -> LOS. Fetches a ground-elevation profile along the great
// circle to the contact, adds earth curvature, and reports whether the path is
// clear, grazing the Fresnel zone, or blocked — with a cross-section to show
// where. The fetch runs on a worker task (see los.cpp), so this side is a
// poll: open the modal, show "Analyzing…", render when the result lands.
//
// Antenna heights are fixed at 2 m both ends for now. They move the verdict
// materially, so this is a deliberate simplification rather than an oversight:
// the reference port exposes +/- buttons and that is the natural follow-up.
static constexpr float kLosAntennaSelfM = 2.0f;
static constexpr float kLosAntennaPeerM = 2.0f;

static lv_obj_t  *s_nodeLosBackdrop = nullptr;
static lv_obj_t  *s_nodeLosModal    = nullptr;
static lv_obj_t  *s_nodeLosStatus   = nullptr;
static lv_obj_t  *s_nodeLosPlot     = nullptr;
static lv_obj_t  *s_nodeLosVerdict  = nullptr;
static lv_timer_t *s_nodeLosTimer   = nullptr;
static uint32_t   s_nodeLosNodeId   = 0;
// lv_line keeps the caller's array by pointer, so these outlive the draw call.
static lv_point_precise_t s_nodeLosTerrainPts[kLosSamples];
static lv_point_precise_t s_nodeLosSightPts[2];

static void closeNodeLosModal() {
    if (s_nodeLosTimer) { lv_timer_del(s_nodeLosTimer); s_nodeLosTimer = nullptr; }
    if (lvObjValid(s_nodeLosBackdrop)) {
        lv_obj_del(s_nodeLosBackdrop);
    } else if (lvObjValid(s_nodeLosModal)) {
        lv_obj_del(s_nodeLosModal);
    }
    s_nodeLosBackdrop = nullptr;
    s_nodeLosModal    = nullptr;
    s_nodeLosStatus   = nullptr;
    s_nodeLosPlot     = nullptr;
    s_nodeLosVerdict  = nullptr;
    s_nodeLosNodeId   = 0;
    // Leaves a mid-flight fetch alone; losReset() declines while the worker is
    // still running so its arrays are never pulled out from under it.
    losReset();
}

static void onNodeLosClosePressed(lv_event_t *e) {
    LV_UNUSED(e);
    closeNodeLosModal();
}

static void onNodeLosBackdropPressed(lv_event_t *e) {
    LV_UNUSED(e);
    closeNodeLosModal();
}

// Our own position: a live fix when there is one, the stored/manual position
// otherwise. Without either there is no path to analyse.
static void nodeLosRenderResult() {
    if (!lvObjValid(s_nodeLosPlot) || !lvObjValid(s_nodeLosVerdict)) return;

    LosAnalysis a;
    if (!losAnalyze(kLosAntennaSelfM, kLosAntennaPeerM, a)) return;

    lv_obj_clean(s_nodeLosPlot);
    lv_obj_update_layout(s_nodeLosPlot);
    const int gw = lv_obj_get_width(s_nodeLosPlot);
    const int gh = lv_obj_get_height(s_nodeLosPlot);
    if (gw < 8 || gh < 8) return;

    // Scale to whatever the profile actually spans, including both antennas —
    // a flat path and a mountain range both want to fill the box.
    double vmin = 1e9, vmax = -1e9;
    for (int i = 0; i < kLosSamples; ++i) {
        const double v = (double)a.terrainM[i];
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
    }
    if (a.h0M < vmin) vmin = a.h0M;
    if (a.h0M > vmax) vmax = a.h0M;
    if (a.hNM < vmin) vmin = a.hNM;
    if (a.hNM > vmax) vmax = a.hNM;
    if (vmax - vmin < 1.0) vmax = vmin + 1.0;

    const int pad = 4;
    // Line points go to LVGL as they are, so in its pixels (UI_COORD_SCALE).
    auto X = [&](int i) -> lv_value_precise_t {
        return (lv_value_precise_t)(i * (gw - 1) / (kLosSamples - 1) * UI_COORD_SCALE);
    };
    auto Y = [&](double v) -> lv_value_precise_t {
        const double t = (v - vmin) / (vmax - vmin);
        return (lv_value_precise_t)((gh - pad - t * (gh - 2 * pad)) * UI_COORD_SCALE);
    };

    for (int i = 0; i < kLosSamples; ++i) {
        s_nodeLosTerrainPts[i].x = X(i);
        s_nodeLosTerrainPts[i].y = Y((double)a.terrainM[i]);
    }
    lv_obj_t *tline = lv_line_create(s_nodeLosPlot);
    lv_line_set_points(tline, s_nodeLosTerrainPts, kLosSamples);
    lv_obj_set_style_line_color(tline, lv_color_hex(0x6FBF73), 0);
    lv_obj_set_style_line_width(tline, 2, 0);
    lv_obj_set_style_line_rounded(tline, true, 0);

    const uint32_t vcol = (a.verdict == LOS_BLOCKED)  ? 0xD7574E
                        : (a.verdict == LOS_MARGINAL) ? 0xC8A030
                                                      : 0x4DA8FF;
    s_nodeLosSightPts[0].x = X(0);
    s_nodeLosSightPts[0].y = Y(a.h0M);
    s_nodeLosSightPts[1].x = X(kLosSamples - 1);
    s_nodeLosSightPts[1].y = Y(a.hNM);
    lv_obj_t *sline = lv_line_create(s_nodeLosPlot);
    lv_line_set_points(sline, s_nodeLosSightPts, 2);
    lv_obj_set_style_line_color(sline, lv_color_hex(vcol), 0);
    lv_obj_set_style_line_width(sline, 2, 0);

    // Mark the tightest point, which is the whole answer to "where".
    if (a.verdict != LOS_CLEAR && a.worstIdx >= 0) {
        lv_obj_t *dot = lv_obj_create(s_nodeLosPlot);
        lv_obj_remove_style_all(dot);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(dot, 6, 6);
        lv_obj_set_style_radius(dot, 3, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0xD7574E), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_pos(dot, (int)X(a.worstIdx) - 3,
                            (int)Y((double)a.terrainM[a.worstIdx]) - 3);
    }

    const char *vstr = (a.verdict == LOS_BLOCKED)  ? TR("NO LINE OF SIGHT")
                     : (a.verdict == LOS_MARGINAL) ? TR("MARGINAL (Fresnel)")
                                                   : TR("LINE OF SIGHT");
    char detail[64];
    if (a.verdict == LOS_BLOCKED) {
        snprintf(detail, sizeof(detail), TR("Blocked @ %.1f km, %dm over sight"),
                 a.worstD1M / 1000.0, (int)(-a.minClearM + 0.5));
    } else if (a.verdict == LOS_MARGINAL) {
        snprintf(detail, sizeof(detail), TR("Grazes terrain - F1=%dm"),
                 (int)(a.worstF1M + 0.5));
    } else {
        snprintf(detail, sizeof(detail), TR("Clear by %dm at tightest"),
                 (int)(a.minClearM + 0.5));
    }

    char body[160];
    snprintf(body, sizeof(body),
             TR("%s\n%.1f km - brg %03d %s\n%s\nyou %dm - peer %dm - %.0f MHz"),
             vstr, a.distanceKm, (int)(a.bearingDeg + 0.5), a.compass, detail,
             (int)kLosAntennaSelfM, (int)kLosAntennaPeerM, a.freqMhz);
    lv_label_set_text(s_nodeLosVerdict, body);
    lv_obj_set_style_text_color(s_nodeLosVerdict, lv_color_hex(vcol), 0);

    if (lvObjValid(s_nodeLosStatus)) {
        lv_obj_add_flag(s_nodeLosStatus, LV_OBJ_FLAG_HIDDEN);
    }
}

static void nodeLosPoll(lv_timer_t *t) {
    LV_UNUSED(t);
    if (!lvObjValid(s_nodeLosModal)) return;

    const LosState st = losState();
    if (st == LOS_FETCHING) return;

    if (s_nodeLosTimer) { lv_timer_del(s_nodeLosTimer); s_nodeLosTimer = nullptr; }

    if (st == LOS_OK) {
        nodeLosRenderResult();
        return;
    }

    if (!lvObjValid(s_nodeLosStatus)) return;
    // Each failure names the thing the operator can actually change. A bare
    // "failed" here would be indistinguishable from a bug in the analysis.
    const char *msg;
    char httpBuf[72];
    switch (st) {
        case LOS_ERR_NO_SERVER:
            msg = TR("No elevation server set.\nWeb Config -> Elevation Server (LOS)");
            break;
        case LOS_ERR_NO_WIFI:
            msg = TR("Wi-Fi needed to fetch the\nterrain profile for this path.");
            break;
        case LOS_ERR_BADREPLY:
            msg = TR("Server answered, but not with\nelevation data. Check the proxy\npoints at the elevation service.");
            break;
        case LOS_ERR_DATA:
            msg = TR("Elevation data too sparse\nfor this path.");
            break;
        default:
            snprintf(httpBuf, sizeof(httpBuf),
                     TR("Elevation fetch failed (%d).\nCheck the server URL - it must be http://"),
                     losHttpCode());
            msg = httpBuf;
            break;
    }
    lv_label_set_text(s_nodeLosStatus, msg);
}

static void openNodeLosModal(uint32_t nodeId) {
    if (!s_rootScreen || nodeId == 0) return;
    if (s_nodeLosModal || s_nodeLosBackdrop) return;

    const NodeEntry *node = Nodes.find(nodeId);
    if (!node || !node->hasPosition || (node->latI == 0 && node->lonI == 0)) return;

    double selfLat = 0, selfLon = 0;
    const bool haveSelf = nodeLosSelfPosition(selfLat, selfLon);
    const double peerLat = (double)node->latI / 1e7;
    const double peerLon = (double)node->lonI / 1e7;

    s_nodeLosNodeId = nodeId;

    const int scrW = lv_disp_get_hor_res(NULL);
    const int scrH = lv_disp_get_ver_res(NULL);
    int modalW = scrW - 24;
    if (modalW > 300) modalW = 300;
    int modalH = scrH - 20;
    if (modalH > 224) modalH = 224;

    const bool lightUi = (s_cfg.uiMode == UI_MODE_LIGHT);
    const lv_color_t modalBg = lightUi ? lv_color_hex(0xEAF1FB) : lv_color_hex(0x0E285B);
    const lv_color_t modalBorder = lightUi ? lv_color_hex(0x6E8FB8) : lv_color_hex(0x5C86C6);
    const lv_color_t titleColor = lightUi ? lv_color_hex(0x16233A) : lv_color_hex(0xD9E8FF);
    const lv_color_t bodyColor = lightUi ? lv_color_hex(0x13243D) : lv_color_hex(0xE8F1FF);

    s_nodeLosBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_nodeLosBackdrop, scrW, scrH);
    lv_obj_align(s_nodeLosBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_nodeLosBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_nodeLosBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_nodeLosBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_nodeLosBackdrop, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_nodeLosBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_nodeLosBackdrop, 0, 0);
    lv_obj_add_event_cb(s_nodeLosBackdrop, onNodeLosBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_nodeLosModal = lv_obj_create(s_nodeLosBackdrop);
    lv_obj_set_size(s_nodeLosModal, modalW, modalH);
    lv_obj_align(s_nodeLosModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_nodeLosModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_nodeLosModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_nodeLosModal, modalBg, 0);
    lv_obj_set_style_bg_opa(s_nodeLosModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_nodeLosModal, 1, 0);
    lv_obj_set_style_border_color(s_nodeLosModal, modalBorder, 0);
    lv_obj_set_style_pad_all(s_nodeLosModal, 6, 0);
    lv_obj_set_style_pad_row(s_nodeLosModal, 3, 0);
    lv_obj_set_flex_flow(s_nodeLosModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_nodeLosModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_nodeLosBackdrop);

    char who[40];
    nodesActionTitleLabel(nodeId, who, sizeof(who));
    lv_obj_t *title = lv_label_create(s_nodeLosModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, titleColor, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_label_set_text(title, who);

    lv_obj_t *plot = lv_obj_create(s_nodeLosModal);
    s_nodeLosPlot = plot;
    lv_obj_remove_style_all(plot);
    lv_obj_set_width(plot, modalW - 20);
    // 74 everywhere with the height to spare; less on the Cardputer, whose
    // modal is only 115 px tall on a 240x135 panel — 74 of that is two thirds
    // of the screen for the cross-section, and the verdict underneath it (which
    // wraps to two lines) would be pushed off the bottom.
    //
    // Shrunk rather than dropped: the silhouette is what makes this feature
    // read at a glance, and 40 px of it over 200 px of width still shows where
    // the terrain rises. The numbers below say the rest.
#if UI_CARDPUTER_COMPACT
    lv_obj_set_height(plot, 40);
#else
    lv_obj_set_height(plot, 74);
#endif
    lv_obj_clear_flag(plot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(plot, lv_color_hex(0x0B0D0F), 0);
    lv_obj_set_style_bg_opa(plot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(plot, 4, 0);

    s_nodeLosStatus = lv_label_create(s_nodeLosModal);
    lv_obj_set_width(s_nodeLosStatus, lv_pct(100));
    lv_obj_set_style_text_font(s_nodeLosStatus, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_nodeLosStatus, bodyColor, 0);
    lv_obj_set_style_text_align(s_nodeLosStatus, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_nodeLosStatus, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_nodeLosStatus, TR("Analyzing terrain..."));

    s_nodeLosVerdict = lv_label_create(s_nodeLosModal);
    lv_obj_set_width(s_nodeLosVerdict, lv_pct(100));
    lv_obj_set_style_text_font(s_nodeLosVerdict, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_nodeLosVerdict, bodyColor, 0);
    lv_obj_set_style_text_align(s_nodeLosVerdict, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_nodeLosVerdict, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_nodeLosVerdict, "");

#if UI_TOUCH_ONLY_PROFILE
    reserveHeltecCloseXRow(title);
    appendHeltecCloseX(s_nodeLosModal, onNodeLosClosePressed);
#endif

    if (!haveSelf) {
        // The peer's position is not the missing half — say which one is, or the
        // operator checks the wrong node.
        lv_label_set_text(s_nodeLosStatus,
                          TR("This node has no position yet.\nNeeds a GPS fix or a set location."));
        return;
    }

    losReset();
    if (!losRequest(s_cfg.losElevServer, selfLat, selfLon, peerLat, peerLon,
                    (double)s_cfg.loraFreq)) {
        nodeLosPoll(nullptr);      // publishes whatever refusal reason was set
        return;
    }
    s_nodeLosTimer = lv_timer_create(nodeLosPoll, 250, nullptr);
}
#endif  // HAS_NODE_LOS

