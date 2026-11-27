// SPDX-License-Identifier: GPL-3.0-or-later
// Part of web_config.cpp: chunked socket writes, state maps and map tile uploads.
// Included in order from web_config.cpp as one translation unit; not built on its own.

static bool clientWritable(uint32_t timeoutMs) {
    const int sock = server.client().fd();
    if (sock < 0) return false;

    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(sock, &writable);
    struct timeval tv;
    tv.tv_sec  = (time_t)(timeoutMs / 1000);
    tv.tv_usec = (suseconds_t)((timeoutMs % 1000) * 1000);
    return select(sock + 1, nullptr, &writable, nullptr, &tv) > 0
           && FD_ISSET(sock, &writable);
}

// True when the response can't continue: client gone, or the socket has stopped
// draining. Either way the rest of the page is skipped — it keeps building (that
// part is cheap and needs no network) and simply lands nowhere.
static bool sendStalled() {
    if (gSendAborted) return true;
    if (!server.client().connected()) {
        // Said out loud, because the two ways a response dies want opposite
        // fixes and used to look identical in the log: an abandoned page with
        // no reason given. This one is the browser hanging up — it gave up
        // waiting, was reloaded, or navigated away — and points at how long the
        // device took to answer, not at the socket. The other, below, is the
        // device unable to push bytes into a connection still nominally open.
        Serial.println("[web] client hung up mid-response — abandoning");
        gSendAborted = true;
        return true;
    }
    if (!clientWritable(kWriteWindowMs)) {
        Serial.println("[web] socket not draining — abandoning response");
        gSendAborted = true;
        return true;
    }
    return false;
}

// Writes every byte or gives up after one full no-progress window.
//
// WiFiClient::write() has its own ten-attempt select loop, outside our timeout,
// and can still return a partial count after that loop. Writing on the socket
// directly keeps every retry under this function's deadline. Partial sends are
// safe to resume because sendChunkedRaw() has already assembled the complete
// frame in stable storage; only a full no-progress window or a hard socket error
// abandons it.
static bool writeAllRaw(const char *data, size_t len) {
    // Bound once: client() returns by value, and the copy shares the socket
    // through a refcounted handle. Fetching it per iteration would be correct
    // but pointless churn.
    WiFiClient c = server.client();
    const int sock = c.fd();
    if (sock < 0) {
        Serial.println("[web] raw write failed: invalid socket");
        return false;
    }

    size_t off = 0;
    uint32_t deadline = millis() + kWriteWindowMs;
    while (off < len) {
        if (!c.connected()) {
            Serial.printf("[web] raw write failed: client closed at %u/%u bytes\n",
                          (unsigned)off, (unsigned)len);
            return false;
        }

        const uint32_t now = millis();
        if ((int32_t)(now - deadline) >= 0) {
            Serial.printf("[web] raw write failed: no progress at %u/%u bytes\n",
                          (unsigned)off, (unsigned)len);
            return false;
        }
        const uint32_t waitMs = deadline - now;
        if (!clientWritable(waitMs)) {
            Serial.printf("[web] raw write failed: socket stalled at %u/%u bytes\n",
                          (unsigned)off, (unsigned)len);
            return false;
        }

        const ssize_t sent = send(sock, data + off, len - off, MSG_DONTWAIT);
        if (sent > 0) {
            off += (size_t)sent;
            deadline = millis() + kWriteWindowMs;
            continue;
        }
        if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            Serial.printf("[web] raw write failed: send errno=%d at %u/%u bytes\n",
                          errno, (unsigned)off, (unsigned)len);
            return false;
        }
        delay(1);
    }
    return true;
}

// One chunked-transfer chunk, framed by hand so the size header can never
// disagree with what actually went out — and emitted as a SINGLE write.
//
// The single write matters as much as the framing. Sending the size header, the
// payload and the trailing CRLF as three writes makes three tiny TCP segments,
// and with Nagle enabled the second and third are held until the client ACKs
// the first. The client's delayed-ACK timer is ~100-200 ms, so the page went
// out at one ACK round-trip per 512 bytes — measured at 107-307 ms per chunk,
// which is both glacial and enough to trip the writability check and abandon
// the response. Coalescing costs one 512-byte memcpy and removes the round trip
// entirely.
//
// Static rather than stack: this is called for every chunk of every page, and
// half a KB of stack in a WebServer handler is not worth the risk. Single
// request at a time, so there is nothing to share it with.
static char sChunkFrame[kFlashChunkBytes + 24];

static bool sendChunkedRaw(const char *data, size_t len) {
    if (!len) return true;
    if (len > kFlashChunkBytes) return false;   // caller slices; never happens
    const int hn = snprintf(sChunkFrame, sizeof(sChunkFrame), "%x\r\n", (unsigned)len);
    if (hn <= 0) return false;
    memcpy(sChunkFrame + hn, data, len);
    sChunkFrame[hn + len]     = '\r';
    sChunkFrame[hn + len + 1] = '\n';
    return writeAllRaw(sChunkFrame, (size_t)hn + len + 2);
}

// Slices, frames and sends. Returns false once the response has been abandoned.
static bool sendSliced(const char *data, size_t len, const char *what) {
    // Nagle is the other half of the stall above: it holds a small write back
    // waiting for an ACK that the client is itself delaying. Off for the whole
    // response — every page here is a stream of modest chunks, which is exactly
    // the traffic pattern Nagle penalises. Idempotent, so setting it per slice
    // rather than plumbing a response-start hook costs a setsockopt per section.
    //
    // Guarded on connected(): setsockopt on a socket the peer has already closed
    // fails with EINVAL, and the core logs every failure at error level. On a
    // client that goes away mid-response that is one console line per slice,
    // which buries whatever else is being diagnosed. A dropped client also has
    // nothing to gain from TCP_NODELAY.
    if (server.client().connected()) server.client().setNoDelay(true);
    for (size_t off = 0; off < len; ) {
        if (sendStalled()) return false;
        const size_t n = (len - off > kFlashChunkBytes) ? kFlashChunkBytes
                                                        : (len - off);
        const uint32_t t0 = millis();
        const bool ok = sendChunkedRaw(data + off, n);
        const uint32_t dt = millis() - t0;
        if (dt > 100) Serial.printf("[web] slow %s: %u bytes in %u ms\n",
                                    what, (unsigned)n, (unsigned)dt);
        if (!ok) {
            // Half a chunk may already be on the wire, so the stream cannot be
            // trusted from here. Drop the socket rather than let the browser
            // parse the wreckage as content.
            Serial.printf("[web] short write in %s — dropping response\n", what);
            gSendAborted = true;
            server.client().stop();
            return false;
        }
        off += n;
        delay(1);   // let the TCP stack push data out and take ACKs
    }
    return true;
}

static void sendChunk(String &html) {
    const size_t len = html.length();
    if (!len) return;
    // Sliced for the same reason as sendFlash(): keep any single write inside
    // what the socket just said it could take.
    sendSliced(html.c_str(), len, "chunk");
    html = "";
}

// Same guards for the flash-resident constants, which bypass the String buffer.
//
// Sliced rather than handed over whole: several of these run to multiple KB, and
// a socket reporting "writable" only promises *some* space. Passing 7 KB to one
// write() lets it fill the send buffer and then spin in its own retry loop for
// 10 s — exactly the stall the writability check was meant to prevent. Writing
// in small pieces keeps that check meaningful, since it runs again before each.
static void sendFlash(const char *progmem) {
    // ESP32 maps flash into the address space, so these constants can be read
    // and written straight through — no _P copy step, and the same framed
    // writer as everything else.
    sendSliced(progmem, strlen(progmem), "flash chunk");
}

// Boards without the LVGL palette (none today) link this instead of the one in
// main_lvgl.cpp, and the page keeps /app.css's Camillia Dark defaults.
__attribute__((weak)) bool webCfgThemeColors(WebThemeColors *out) {
    (void)out;
    return false;
}

// The full page's <head>: kHeadOpen, the fonts, the device theme's :root block,
// kHeadClose. The theme block is a few hundred bytes from the stack, so this
// costs no more heap than the old inline stylesheet did.
static void sendFullHead() {
    sendFlash(kHeadOpen);
    sendFlash(kAppFontsLink);
    // /app.css is cached for a year, so its URL carries a hash of its bytes:
    // any change to the stylesheet, even in a flash that keeps the version, is
    // a URL the browser has never cached.
    static uint32_t cssHash = 0;
    if (!cssHash) cssHash = webAssetHash(kAppCss);
    char link[64];
    snprintf(link, sizeof(link), "<link rel='stylesheet' href='/app.css?v=%08lx'>",
             (unsigned long)cssHash);
    server.sendContent(link);
    WebThemeColors tc;
    char vars[600];
    if (webCfgThemeColors(&tc) && webThemeCssVars(tc, vars, sizeof(vars))) {
        server.sendContent("<style>");
        server.sendContent(vars);
        server.sendContent("</style>");
    }
    sendFlash(kHeadClose);
}

// Flush only once the buffer is worth a TCP segment. Chunk size is a balance
// with two failure modes at either end: one huge String needs a contiguous
// allocation a fragmented AP-mode heap can't provide, while a flush per table
// row means dozens of tiny chunked writes, and each one is a separate segment
// the synchronous WebServer blocks on. In AP mode, with LWIP down to ~15 KB,
// a pbuf allocation that fails mid-write wedges the main loop.
static void sendChunkIfBig(String &html, size_t threshold = 700) {
    if (html.length() >= threshold) sendChunk(html);
}

#if HAS_STATE_MAPS
// ── State map cache ──────────────────────────────────────────────────────────
// The Locate modal on the device draws a node's position onto a cached PNG of
// the state it falls in. This firmware has no TLS client, so it cannot fetch
// those maps itself — the browser, which already renders the Nodes heatmap from
// live OSM tiles, does the fetching and hands the finished image back here.
//
// This file owns the receiving half: what is already cached (/state-map-status)
// and where new ones land (/state-map-upload). The bounding boxes both ends
// work from come from state_maps.h so the two cannot drift apart.

// Compile-time: some boards have nowhere to put a file at all. Runtime: a board
// with a slot still needs a card in it. The Maps Download section is hidden
// outright when either is false, rather than shown disabled — there is nothing
// actionable to say to a browser about a card slot it cannot see.
static bool stateMapStorageReady() {
#if !HAS_FILE_STORAGE
    return false;
#else
    // sdBegin(), not storageBegin(). They are not interchangeable: sdBegin() is
    // the board-aware mount — it runs SPI.begin() with this board's LoRa pins and
    // deasserts the SD, LoRa and TFT chip selects before touching the card, which
    // is what makes a shared SPI bus safe, and it carries the per-board speed and
    // profile ladders. storageBegin() only calls SD.begin() against whatever state
    // the bus happens to be in, and tracks its own separate mounted flag.
    //
    // On a board where something had already brought the bus up, the difference
    // was invisible. On the M9 it was not. Every other storage call site in this
    // file and in main_lvgl.cpp already uses sdBegin(); this was the odd one out.
    //
    // Boards with no slot but internal flash are covered too: sdBegin() mounts
    // LittleFS for them, so callers never need to know which backend they have.
    return sdBegin();
#endif
}

static bool stateMapEnsureDir() {
#if !HAS_FILE_STORAGE
    return false;
#else
    if (!stateMapStorageReady()) return false;
    if (!storageFs().exists("/camillia")) storageFs().mkdir("/camillia");
    if (!storageFs().exists("/camillia/state_maps")) storageFs().mkdir("/camillia/state_maps");
    return storageFs().exists("/camillia/state_maps");
#endif
}

static bool detailMapEnsureDir() {
#if !HAS_FILE_STORAGE
    return false;
#else
    if (!stateMapStorageReady()) return false;
    if (!storageFs().exists("/camillia")) storageFs().mkdir("/camillia");
    if (!storageFs().exists("/camillia/detail_maps")) storageFs().mkdir("/camillia/detail_maps");
    return storageFs().exists("/camillia/detail_maps");
#endif
}

static bool detailMapMarkerMatchesVersion() {
#if !HAS_FILE_STORAGE
    return false;
#else
    if (!storageFs().exists(kDetailMapMarkerPath)) return false;
    File marker = storageFs().open(kDetailMapMarkerPath, FILE_READ);
    if (!marker) return false;
    String line = marker.readStringUntil('\n');
    marker.close();
    line.trim();
    return line.equals(kDetailMapCacheVersion);
#endif
}

static void detailMapWriteVersionMarker() {
#if HAS_FILE_STORAGE
    if (!detailMapEnsureDir()) return;
    if (storageFs().exists(kDetailMapMarkerPath)) storageFs().remove(kDetailMapMarkerPath);
    File marker = storageFs().open(kDetailMapMarkerPath, FILE_WRITE);
    if (!marker) return;
    marker.print(kDetailMapCacheVersion);
    marker.close();
#endif
}

static bool legacyDetailMapArtifactOwned(const char *filename) {
    if (!filename || !filename[0]) return false;
    const char *base = filename;
    for (const char *cursor = filename; *cursor; cursor++) {
        if (*cursor == '/' || *cursor == '\\') base = cursor + 1;
    }
    if (strcmp(base, "detail_maps.version") == 0) return true;
    if (strlen(base) < 14) return false;

    char key[11] = {};
    memcpy(key, base, 10);
    key[10] = '\0';
    if (key[0] >= 'a' && key[0] <= 'z') key[0] = (char)(key[0] - 32);
    if (key[5] >= 'a' && key[5] <= 'z') key[5] = (char)(key[5] - 32);
    int lat10 = 0;
    int lon10 = 0;
    if (!nodesDetailMapParseKey(key, lat10, lon10)) return false;
    const char *suffix = base + 10;
    return strcmp(suffix, ".png") == 0
           || strcmp(suffix, ".meta") == 0
           || strcmp(suffix, ".png.tmp") == 0;
}

static void detailMapResetIfStale() {
#if HAS_FILE_STORAGE
    if (!detailMapEnsureDir()) return;
    if (detailMapMarkerMatchesVersion()) return;

    File dir = storageFs().open("/camillia/detail_maps");
    if (dir && dir.isDirectory()) {
        for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
            String path = f.name();
            bool isDir = f.isDirectory();
            bool owned = !isDir && legacyDetailMapArtifactOwned(f.name());
            f.close();
            if (!owned) continue;
            if (!path.startsWith("/")) path = String("/camillia/detail_maps/") + path;
            storageFs().remove(path.c_str());
        }
        dir.close();
    } else if (dir) {
        dir.close();
    }

    detailMapWriteVersionMarker();
#endif
}

// Accepts either a bare filename or a full path; extracts and validates the
// 0.1-degree detail cell key.
static bool detailMapKeyFromFilename(const char *filename, char outKey[11]) {
    if (!filename || !outKey) return false;
    const char *base = filename;
    for (const char *p = filename; *p; p++) {
        if (*p == '/') base = p + 1;
    }
    auto lower = [](char c) -> char { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; };
    if (strlen(base) != 14
        || lower(base[10]) != '.' || lower(base[11]) != 'p'
        || lower(base[12]) != 'n' || lower(base[13]) != 'g') {
        return false;
    }

    char key[11];
    memcpy(key, base, 10);
    key[10] = '\0';
    if (key[0] >= 'a' && key[0] <= 'z') key[0] = (char)(key[0] - 32);
    if (key[5] >= 'a' && key[5] <= 'z') key[5] = (char)(key[5] - 32);
    int lat10 = 0, lon10 = 0;
    if (!nodesDetailMapParseKey(key, lat10, lon10)) return false;
    memcpy(outKey, key, 11);
    return true;
}

// Metadata line format: latMin,latMax,lonMin,lonMax,lod
// Legacy files may have only the first 4 fields.
static bool detailMapReadLod(const char *cellKey, int &lodOut) {
#if !HAS_FILE_STORAGE
    (void)cellKey;
    lodOut = -1;
    return false;
#else
    if (!cellKey || !cellKey[0]) { lodOut = -1; return false; }
    String p = nodesDetailMapMetaPath(cellKey);
    File f = storageFs().open(p.c_str(), FILE_READ);
    if (!f) { lodOut = -1; return false; }
    String line = f.readStringUntil('\n');
    f.close();

    double a = 0.0, b = 0.0, c = 0.0, d = 0.0;
    int lod = -1;
    int n = sscanf(line.c_str(), "%lf,%lf,%lf,%lf,%d", &a, &b, &c, &d, &lod);
    if (n < 4) { lodOut = -1; return false; }
    lodOut = (n >= 5) ? lod : -1;
    return (n >= 5);
#endif
}

// A cached map counts only when the PNG is real. A truncated upload leaves a
// file the device would reject at decode time anyway; reporting it as present
// would just move the confusion to the device.
// One directory pass, in place of fifty path probes.
//
// The status endpoint used to ask stateMapCached() about every state in the
// table. On a board backed by LittleFS each miss is a failed open that the VFS
// layer both charges for and logs, and fifty of them measured ~625 ms on the
// Heltec — a blocking stall in loop() every time the config page was opened,
// which is ~20 dropped UI frames and reads as exactly the lag issue #53 is
// about. Reading the directory costs one open regardless of how empty it is.
//
// Dimensions come off the directory entry's own handle, so a cached map costs no
// extra open either.
struct StateMapCacheEntry {
    char code[3];
    uint32_t w;
    uint32_t h;
};

static int stateMapScanCache(StateMapCacheEntry *out, int maxOut) {
#if !HAS_FILE_STORAGE
    (void)out; (void)maxOut;
    return 0;
#else
    if (!out || maxOut <= 0) return 0;
    if (!stateMapStorageReady()) return 0;

    File dir = storageFs().open("/camillia/state_maps");
    if (!dir) return 0;
    if (!dir.isDirectory()) { dir.close(); return 0; }

    int n = 0;
    for (File f = dir.openNextFile(); f && n < maxOut; f = dir.openNextFile()) {
        if (f.isDirectory()) { f.close(); continue; }

        // name() is a full path on some cores and a bare name on others.
        const char *nm = f.name();
        if (!nm) { f.close(); continue; }
        const char *base = nm;
        for (const char *p2 = nm; *p2; p2++) {
            if (*p2 == '/') base = p2 + 1;
        }

        // Exactly "XX.png"; anything else in the folder is not ours.
        auto lower = [](char c) -> char { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; };
        if (strlen(base) != 6
            || lower(base[2]) != '.' || lower(base[3]) != 'p'
            || lower(base[4]) != 'n' || lower(base[5]) != 'g') {
            f.close();
            continue;
        }

        uint8_t hdr[24] = {0};
        const bool readOk = (f.read(hdr, sizeof(hdr)) == sizeof(hdr));
        f.close();
        if (!readOk) continue;

        static const uint8_t kPngSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
        if (memcmp(hdr, kPngSig, sizeof(kPngSig)) != 0) continue;
        if (!(hdr[12] == 'I' && hdr[13] == 'H' && hdr[14] == 'D' && hdr[15] == 'R')) continue;

        out[n].code[0] = (char)toupper((unsigned char)base[0]);
        out[n].code[1] = (char)toupper((unsigned char)base[1]);
        out[n].code[2] = '\0';
        out[n].w = ((uint32_t)hdr[16] << 24) | ((uint32_t)hdr[17] << 16)
                 | ((uint32_t)hdr[18] << 8) | hdr[19];
        out[n].h = ((uint32_t)hdr[20] << 24) | ((uint32_t)hdr[21] << 16)
                 | ((uint32_t)hdr[22] << 8) | hdr[23];
        if (out[n].w == 0 || out[n].h == 0) continue;
        n++;
    }
    dir.close();
    return n;
#endif
}

// GET /state-map-status
// One object per state: the bounds to composite against, and whether it is
// already on the card. The browser intersects this with the node positions it
// already has (NODE_HEAT_POINTS) to decide what to download.
static void handleGetStateMapStatus() {
    if (!isLoggedIn()) {
        server.send(403, "application/json", "{\"error\":\"unauthorized\"}");
        return;
    }

    const bool ready = stateMapStorageReady();
    if (ready) detailMapResetIfStale();

    // Scanned once up front. Everything below answers from this, so the endpoint
    // touches the filesystem a fixed number of times instead of once per state.
    StateMapCacheEntry cached[kUsStateMapCount];
    const int cachedCount = ready ? stateMapScanCache(cached, kUsStateMapCount) : 0;

    // gSendAborted is sticky across requests: once any response gives up on a
    // stalled socket, every later chunked send is skipped until someone clears
    // it. Every chunked sender in this file has to open by resetting it — a
    // client that walked away mid-download would otherwise mute this endpoint
    // until the next reboot.
    gSendAborted = false;

    // Chunked rather than one String: 50 entries of bounds is a few KB, and this
    // file's rule is that nothing assembles a multi-KB contiguous buffer it can
    // avoid.
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");

    String out;
    out.reserve(1024);
    out += "{\"storage\":";
    out += (ready ? "true" : "false");
    out += ",\"imageW\":";
    out += String(kStateMapImageW);
    out += ",\"imageH\":";
    out += String(kStateMapImageH);
    out += ",\"states\":[";

    for (int i = 0; i < kUsStateMapCount; i++) {
        const UsStateMapSpec &st = kUsStateMaps[i];
        if (i) out += ",";
        out += "{\"code\":\"";
        out += st.code;
        out += "\",\"name\":\"";
        out += st.name;
        out += "\",\"latMin\":";
        out += String(st.latMin, 4);
        out += ",\"latMax\":";
        out += String(st.latMax, 4);
        out += ",\"lonMin\":";
        out += String(st.lonMin, 4);
        out += ",\"lonMax\":";
        out += String(st.lonMax, 4);
        const StateMapCacheEntry *hit = nullptr;
        for (int c = 0; c < cachedCount; c++) {
            if (cached[c].code[0] == st.code[0] && cached[c].code[1] == st.code[1]) {
                hit = &cached[c];
                break;
            }
        }
        out += ",\"cached\":";
        out += hit ? "true" : "false";
        // A map cached at an older resolution still renders, just softly, so it
        // is reported rather than hidden: the page offers to refresh it instead
        // of making the user clear the whole cache to get a sharper one.
        if (hit) {
            out += ",\"w\":";
            out += String((unsigned)hit->w);
            out += ",\"h\":";
            out += String((unsigned)hit->h);
        }
        out += "}";
        sendChunkIfBig(out, 900);
    }

    out += "],\"detail\":{";
    out += "\"cellDeg\":";
    out += String(kDetailMapCellDeg, 1);
    out += ",\"imageW\":";
    out += String(kDetailMapImageW);
    out += ",\"imageH\":";
    out += String(kDetailMapImageH);
    out += ",\"cached\":[";

    bool firstDetail = true;
    if (ready && detailMapEnsureDir()) {
        File dir = storageFs().open("/camillia/detail_maps");
        if (dir && dir.isDirectory()) {
            for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
                if (f.isDirectory()) { f.close(); continue; }

                const char *nm = f.name();
                if (!nm) { f.close(); continue; }
                char key[11] = {0};
                if (!detailMapKeyFromFilename(nm, key)) {
                    f.close();
                    continue;
                }

                uint8_t hdr[24] = {0};
                const bool readOk = (f.read(hdr, sizeof(hdr)) == sizeof(hdr));
                f.close();
                if (!readOk) continue;

                static const uint8_t kPngSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
                if (memcmp(hdr, kPngSig, sizeof(kPngSig)) != 0) continue;
                if (!(hdr[12] == 'I' && hdr[13] == 'H' && hdr[14] == 'D' && hdr[15] == 'R')) continue;

                const uint32_t w = ((uint32_t)hdr[16] << 24) | ((uint32_t)hdr[17] << 16)
                                 | ((uint32_t)hdr[18] << 8) | hdr[19];
                const uint32_t h = ((uint32_t)hdr[20] << 24) | ((uint32_t)hdr[21] << 16)
                                 | ((uint32_t)hdr[22] << 8) | hdr[23];
                if (w == 0 || h == 0) continue;

                if (!firstDetail) out += ",";
                firstDetail = false;
                int lod = -1;
                detailMapReadLod(key, lod);
                out += "{\"key\":\"";
                out += key;
                out += "\",\"w\":";
                out += String((unsigned)w);
                out += ",\"h\":";
                out += String((unsigned)h);
                if (lod >= 0) {
                    out += ",\"lod\":";
                    out += String(lod);
                }
                out += "}";
                sendChunkIfBig(out, 900);
            }
            dir.close();
        } else if (dir) {
            dir.close();
        }
    }

    out += "]}}";
    sendChunk(out);
    server.sendContent("");
}

// POST /state-map-upload   (multipart, one state per request)
//
// The state code travels in the *filename* ("PA.png"), not a form field: the
// upload callback runs while the body is still streaming, long before
// server.arg() can see any field that follows the file part. The bounds do come
// from form fields, read in the completion handler below once the body is in.
//
// Bytes go straight to the file as they arrive. The /import handler above
// buffers into a static 8 KB array because a YAML config has to be parsed whole;
// a PNG does not, so this path holds one File handle and nothing else — cheaper
// than the upload that already ships.
static File   stateMapUploadFile;
static char   stateMapUploadCode[3] = "";
static bool   stateMapUploadOk = false;
static size_t stateMapUploadBytes = 0;
static File   detailMapUploadFile;
static char   detailMapUploadKey[11] = "";
static bool   detailMapUploadOk = false;
static size_t detailMapUploadBytes = 0;

// Case-folded by hand rather than with strncasecmp(): that is POSIX, not C, and
// nothing else in this firmware relies on it being pulled in.
static const UsStateMapSpec *stateMapSpecForCode(const char *code) {
    if (!code || !code[0] || !code[1]) return nullptr;
    auto up = [](char c) -> char { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; };
    const char a = up(code[0]);
    const char b = up(code[1]);
    for (int i = 0; i < kUsStateMapCount; i++) {
        if (kUsStateMaps[i].code[0] == a && kUsStateMaps[i].code[1] == b) {
            return &kUsStateMaps[i];
        }
    }
    return nullptr;
}

static void handleStateMapUpload() {
#if !HAS_FILE_STORAGE
    return;
#else
    HTTPUpload &upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        stateMapUploadOk = false;
        stateMapUploadBytes = 0;
        stateMapUploadCode[0] = '\0';
        if (stateMapUploadFile) stateMapUploadFile.close();

        // Not logged in, no storage, or a filename that is not a state we know:
        // accept the body and drop it. Refusing mid-stream leaves the socket
        // holding a partial request the browser reports as a network error
        // rather than the refusal it is; the completion handler sends the real
        // status once the body is drained.
        if (!isLoggedIn() || !stateMapEnsureDir()) return;

        char code[3] = "";
        strncpy(code, upload.filename.c_str(), 2);
        code[2] = '\0';
        const UsStateMapSpec *spec = stateMapSpecForCode(code);
        if (!spec) return;

        strncpy(stateMapUploadCode, spec->code, 2);
        stateMapUploadCode[2] = '\0';

        // Written to its final name directly. A half-written file is caught by
        // the PNG sniff on the way out and removed, so a temp-and-rename dance
        // would buy nothing on a filesystem this simple.
        String path = nodesStateMapPath(stateMapUploadCode);
        if (storageFs().exists(path.c_str())) storageFs().remove(path.c_str());
        stateMapUploadFile = storageFs().open(path.c_str(), FILE_WRITE);
        return;
    }

    if (upload.status == UPLOAD_FILE_WRITE) {
        if (!stateMapUploadFile) return;   // keep draining the socket
        size_t wrote = stateMapUploadFile.write(upload.buf, upload.currentSize);
        if (wrote != upload.currentSize) {
            // Card full or gone. Stop writing but keep reading, same reason as
            // the refusals above.
            stateMapUploadFile.close();
            return;
        }
        stateMapUploadBytes += wrote;
        return;
    }

    if (upload.status == UPLOAD_FILE_END) {
        if (!stateMapUploadFile) return;
        stateMapUploadFile.close();
        stateMapUploadOk = (stateMapUploadBytes > 0);
        return;
    }

    // UPLOAD_FILE_ABORTED
    if (stateMapUploadFile) stateMapUploadFile.close();
#endif
}

// POST /detail-map-upload   (multipart, one fixed 0.1-degree cell per request)
static void handleDetailMapUpload() {
#if !HAS_FILE_STORAGE
    return;
#else
    HTTPUpload &upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        detailMapUploadOk = false;
        detailMapUploadBytes = 0;
        detailMapUploadKey[0] = '\0';
        if (detailMapUploadFile) detailMapUploadFile.close();

        if (!isLoggedIn() || !detailMapEnsureDir()) return;
        detailMapResetIfStale();

        char key[11] = {0};
        if (!detailMapKeyFromFilename(upload.filename.c_str(), key)) return;
        memcpy(detailMapUploadKey, key, sizeof(detailMapUploadKey));

        String path = nodesDetailMapPath(detailMapUploadKey);
        if (storageFs().exists(path.c_str())) storageFs().remove(path.c_str());
        detailMapUploadFile = storageFs().open(path.c_str(), FILE_WRITE);
        return;
    }

    if (upload.status == UPLOAD_FILE_WRITE) {
        if (!detailMapUploadFile) return;
        size_t wrote = detailMapUploadFile.write(upload.buf, upload.currentSize);
        if (wrote != upload.currentSize) {
            detailMapUploadFile.close();
            return;
        }
        detailMapUploadBytes += wrote;
        return;
    }

    if (upload.status == UPLOAD_FILE_END) {
        if (!detailMapUploadFile) return;
        detailMapUploadFile.close();
        detailMapUploadOk = (detailMapUploadBytes > 0);
        return;
    }

    if (detailMapUploadFile) detailMapUploadFile.close();
#endif
}

static int removeMapTileTree(const char *path) {
#if !HAS_FILE_STORAGE
    (void)path;
    return 0;
#else
    if (!path || !path[0] || !storageFs().exists(path)) return 0;
    File dir = storageFs().open(path);
    if (!dir) return 0;
    if (!dir.isDirectory()) {
        dir.close();
        bool isTile = String(path).endsWith(".png");
        return storageFs().remove(path) && isTile ? 1 : 0;
    }

    int removed = 0;
    while (true) {
        File child = dir.openNextFile();
        if (!child) break;
        String childPath = child.name();
        bool childIsDir = child.isDirectory();
        child.close();
        if (!childPath.startsWith("/")) {
            String fullPath = path;
            if (!fullPath.endsWith("/")) fullPath += "/";
            fullPath += childPath;
            childPath = fullPath;
        }
        if (childIsDir) removed += removeMapTileTree(childPath.c_str());
        else {
            bool isTile = childPath.endsWith(".png");
            if (storageFs().remove(childPath.c_str()) && isTile) removed++;
        }
    }
    dir.close();
    storageFs().rmdir(path);
    return removed;
#endif
}

// POST /clear-maps
// Deletes the current slippy-tile cache and legacy map files without touching
// other card content.
static void handlePostClearMaps() {
#if !HAS_FILE_STORAGE
    server.send(503, "text/plain", "No storage on this board");
#else
    if (!isLoggedIn()) { redirect("/login"); return; }
    if (!stateMapStorageReady()) {
        redirectHomeWithFlash("No storage detected - nothing to clear");
        return;
    }

    nodesMapTileBumpCacheEpoch();
    int removedState = 0;
    int removedDetail = 0;
    int removedTiles = removeMapTileTree(kMapTileCacheRoot);
    for (int i = 0; i < kUsStateMapCount; i++) {
        String png = nodesStateMapPath(kUsStateMaps[i].code);
        if (storageFs().exists(png.c_str()) && storageFs().remove(png.c_str())) removedState++;
        String meta = nodesStateMapMetaPath(kUsStateMaps[i].code);
        if (storageFs().exists(meta.c_str())) storageFs().remove(meta.c_str());
    }

    if (detailMapEnsureDir()) {
        File dir = storageFs().open("/camillia/detail_maps");
        if (dir && dir.isDirectory()) {
            for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
                String path = f.name();
                bool isDir = f.isDirectory();
                const char *nm = f.name();
                char key[11] = {0};
                bool isCellPng = (nm && detailMapKeyFromFilename(nm, key));
                bool owned = !isDir && legacyDetailMapArtifactOwned(nm);
                f.close();
                if (!owned) continue;
                if (!path.startsWith("/")) path = String("/camillia/detail_maps/") + path;
                if (storageFs().exists(path.c_str()) && storageFs().remove(path.c_str())) {
                    if (isCellPng) removedDetail++;
                }
            }
            dir.close();
        } else if (dir) {
            dir.close();
        }
    }

    if (storageFs().exists(kStateMapMarkerPath)) storageFs().remove(kStateMapMarkerPath);
    if (storageFs().exists(kStateMapLegacyMarkerPath)) storageFs().remove(kStateMapLegacyMarkerPath);
    if (storageFs().exists(kDetailMapMarkerPath)) storageFs().remove(kDetailMapMarkerPath);

    char msg[128];
    snprintf(msg, sizeof(msg),
             "Cleared %d map tile%s, %d legacy state map%s and %d legacy detail cell%s",
             removedTiles, (removedTiles == 1 ? "" : "s"),
             removedState, (removedState == 1 ? "" : "s"),
             removedDetail, (removedDetail == 1 ? "" : "s"));
    redirectHomeWithFlash(msg);
#endif
}

static void handleStateMapUploadDone() {
#if !HAS_FILE_STORAGE
    server.send(503, "application/json", "{\"ok\":false,\"error\":\"no storage on this board\"}");
#else
    if (!isLoggedIn()) {
        server.send(403, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}");
        return;
    }
    if (!stateMapUploadCode[0]) {
        server.send(400, "application/json",
                    "{\"ok\":false,\"error\":\"unknown or missing state code\"}");
        return;
    }

    String path = nodesStateMapPath(stateMapUploadCode);

    // The sniff is the whole validation. Anything that is not a PNG here is a
    // truncated or failed upload, and leaving it on the card would have the
    // device retry a decode that can never succeed.
    if (!stateMapUploadOk || !nodesFileLooksLikePng(path.c_str())) {
        if (storageFs().exists(path.c_str())) storageFs().remove(path.c_str());
        String meta = nodesStateMapMetaPath(stateMapUploadCode);
        if (storageFs().exists(meta.c_str())) storageFs().remove(meta.c_str());
        stateMapUploadCode[0] = '\0';
        server.send(400, "application/json",
                    "{\"ok\":false,\"error\":\"upload was not a complete PNG\"}");
        return;
    }

    // Bounds are what the browser actually composited, which is not the state's
    // nominal box — tiles do not stop at state lines. Recording the real extent
    // is what puts the pin in the right place; without it the device falls back
    // to the nominal box and the position drifts by miles.
    const UsStateMapSpec *spec = stateMapSpecForCode(stateMapUploadCode);
    bool haveBounds = server.hasArg("latMin") && server.hasArg("latMax")
                      && server.hasArg("lonMin") && server.hasArg("lonMax");
    float latMin = spec ? spec->latMin : 0.0f;
    float latMax = spec ? spec->latMax : 0.0f;
    float lonMin = spec ? spec->lonMin : 0.0f;
    float lonMax = spec ? spec->lonMax : 0.0f;
    if (haveBounds) {
        latMin = server.arg("latMin").toFloat();
        latMax = server.arg("latMax").toFloat();
        lonMin = server.arg("lonMin").toFloat();
        lonMax = server.arg("lonMax").toFloat();
        // A degenerate or inverted box would divide by zero in the pin math.
        if (!(latMax > latMin) || !(lonMax > lonMin)) haveBounds = false;
    }
    if (haveBounds) {
        nodesWriteStateMapMeta(stateMapUploadCode, latMin, latMax, lonMin, lonMax);
    }

    char out[160];
    snprintf(out, sizeof(out),
             "{\"ok\":true,\"code\":\"%s\",\"bytes\":%u,\"bounds\":%s}",
             stateMapUploadCode,
             (unsigned)stateMapUploadBytes,
             haveBounds ? "true" : "false");
    stateMapUploadCode[0] = '\0';
    server.send(200, "application/json", out);
#endif
}

static void handleDetailMapUploadDone() {
#if !HAS_FILE_STORAGE
    server.send(503, "application/json", "{\"ok\":false,\"error\":\"no storage on this board\"}");
#else
    if (!isLoggedIn()) {
        server.send(403, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}");
        return;
    }
    if (!detailMapUploadKey[0]) {
        server.send(400, "application/json",
                    "{\"ok\":false,\"error\":\"unknown or missing detail cell key\"}");
        return;
    }

    String path = nodesDetailMapPath(detailMapUploadKey);
    if (!detailMapUploadOk || !nodesFileLooksLikePng(path.c_str())) {
        if (storageFs().exists(path.c_str())) storageFs().remove(path.c_str());
        String meta = nodesDetailMapMetaPath(detailMapUploadKey);
        if (storageFs().exists(meta.c_str())) storageFs().remove(meta.c_str());
        detailMapUploadKey[0] = '\0';
        server.send(400, "application/json",
                    "{\"ok\":false,\"error\":\"upload was not a complete PNG\"}");
        return;
    }

    int lat10 = 0, lon10 = 0;
    if (!nodesDetailMapParseKey(detailMapUploadKey, lat10, lon10)) {
        if (storageFs().exists(path.c_str())) storageFs().remove(path.c_str());
        detailMapUploadKey[0] = '\0';
        server.send(400, "application/json",
                    "{\"ok\":false,\"error\":\"invalid detail cell key\"}");
        return;
    }

    float latMin = 0.0f, latMax = 0.0f, lonMin = 0.0f, lonMax = 0.0f;
    nodesDetailMapBoundsFromIndex(lat10, lon10, latMin, latMax, lonMin, lonMax);

    int lod = 1;   // legacy/default: streets
    if (server.hasArg("lod")) {
        lod = server.arg("lod").toInt();
        if (lod < 0) lod = 0;
        if (lod > 2) lod = 2;
    }

    String metaPath = nodesDetailMapMetaPath(detailMapUploadKey);
    if (storageFs().exists(metaPath.c_str())) storageFs().remove(metaPath.c_str());
    File meta = storageFs().open(metaPath.c_str(), FILE_WRITE);
    if (meta) {
        meta.printf("%.6f,%.6f,%.6f,%.6f,%d\n", (double)latMin, (double)latMax,
                    (double)lonMin, (double)lonMax, lod);
        meta.close();
    }
    detailMapWriteVersionMarker();

    char out[196];
    snprintf(out, sizeof(out),
             "{\"ok\":true,\"key\":\"%s\",\"bytes\":%u,\"lod\":%d,\"latMin\":%.4f,\"latMax\":%.4f,\"lonMin\":%.4f,\"lonMax\":%.4f}",
             detailMapUploadKey,
             (unsigned)detailMapUploadBytes,
             lod,
             (double)latMin, (double)latMax, (double)lonMin, (double)lonMax);
    detailMapUploadKey[0] = '\0';
    server.send(200, "application/json", out);
#endif
}

static constexpr int kMapTileStatusBatchMax = 64;
static constexpr size_t kMapTileUploadMaxBytes = 256U * 1024U;
static File mapTileUploadFile;
static int mapTileUploadZoom = -1;
static int32_t mapTileUploadX = -1;
static int32_t mapTileUploadY = -1;
static bool mapTileUploadOk = false;
static size_t mapTileUploadBytes = 0;
static char mapTileUploadPath[96] = {};
static char mapTileUploadTempPath[104] = {};

struct MapTileFetchDiag {
    int zoom = -1;
    int32_t tileX = -1;
    int32_t tileY = -1;
    const char *stage = "request";
    uint32_t startedAtMs = 0;
    uint32_t headersMs = 0;
    uint32_t bodyMs = 0;
    int upstreamCode = 0;
    int expectedBytes = -1;
    size_t writtenBytes = 0;
    int remainingBytes = -1;
    bool blocked = false;
    bool connected = false;
    bool idleTimedOut = false;
    bool pngValid = false;
};

static void sendMapTileFetchError(int responseCode,
                                  const char *error,
                                  const MapTileFetchDiag &diag) {
    const size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t largestInternal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const uint32_t totalMs = millis() - diag.startedAtMs;
    char response[384];
    snprintf(response, sizeof(response),
             "{\"ok\":false,\"error\":\"%s\",\"stage\":\"%s\","
             "\"upstream\":%d,\"expected\":%d,\"written\":%u,\"remaining\":%d,"
             "\"blocked\":%s,\"connected\":%s,\"idleTimeout\":%s,\"pngValid\":%s,"
             "\"headersMs\":%u,\"bodyMs\":%u,\"totalMs\":%u,"
             "\"heap\":%u,\"largestHeap\":%u}",
             error, diag.stage, diag.upstreamCode, diag.expectedBytes,
             (unsigned)diag.writtenBytes, diag.remainingBytes,
             diag.blocked ? "true" : "false",
             diag.connected ? "true" : "false",
             diag.idleTimedOut ? "true" : "false",
             diag.pngValid ? "true" : "false",
             (unsigned)diag.headersMs, (unsigned)diag.bodyMs, (unsigned)totalMs,
             (unsigned)freeInternal, (unsigned)largestInternal);
    Serial.printf("[map-web] fail stage=%s error=%s z=%d x=%ld y=%ld upstream=%d "
                  "expected=%d written=%u remaining=%d blocked=%d connected=%d "
                  "idle_timeout=%d png=%d headers=%ums body=%ums total=%ums "
                  "heap=%u largest=%u\n",
                  diag.stage, error, diag.zoom, (long)diag.tileX, (long)diag.tileY,
                  diag.upstreamCode, diag.expectedBytes, (unsigned)diag.writtenBytes,
                  diag.remainingBytes, diag.blocked, diag.connected,
                  diag.idleTimedOut, diag.pngValid,
                  (unsigned)diag.headersMs, (unsigned)diag.bodyMs, (unsigned)totalMs,
                  (unsigned)freeInternal, (unsigned)largestInternal);
    server.send(responseCode, "application/json", response);
}

// GET /map-tile-status?keys=13_123_456.png,...
// Status is intentionally plan-scoped and bounded. Returning an entire cache
// index would grow without limit as users pan online and fill gaps.
static void handleGetMapTileStatus() {
    if (!isLoggedIn()) {
        server.send(403, "application/json", "{\"error\":\"unauthorized\"}");
        return;
    }
    if (!server.hasArg("keys")) {
        server.send(400, "application/json", "{\"error\":\"missing keys\"}");
        return;
    }

    String keys = server.arg("keys");
    if (keys.length() > 1800) {
        server.send(413, "application/json", "{\"error\":\"status batch too large\"}");
        return;
    }

    const bool ready = stateMapStorageReady();
    char bits[kMapTileStatusBatchMax + 1] = {};
    int count = 0;
    int cursor = 0;
    while (cursor <= (int)keys.length()) {
        int comma = keys.indexOf(',', cursor);
        if (comma < 0) comma = (int)keys.length();
        if (comma == cursor || count >= kMapTileStatusBatchMax) {
            server.send(400, "application/json", "{\"error\":\"invalid status batch\"}");
            return;
        }

        String key = keys.substring(cursor, comma);
        int zoom = 0;
        int32_t tileX = 0;
        int32_t tileY = 0;
        bool present = false;
        if (nodesMapTileParseUploadFilename(key.c_str(), zoom, tileX, tileY)) {
            String path = nodesMapTilePath(zoom, tileX, tileY);
            if (ready && !path.isEmpty() && storageFs().exists(path.c_str())) {
                present = nodesMapTileFileValid(path.c_str());
                if (!present) storageFs().remove(path.c_str());
            }
        }
        bits[count++] = present ? '1' : '0';
        if (comma >= (int)keys.length()) break;
        cursor = comma + 1;
    }
    bits[count] = '\0';

    char response[160];
    snprintf(response, sizeof(response),
             "{\"storage\":%s,\"count\":%d,\"bits\":\"%s\"}",
             ready ? "true" : "false", count, bits);
    server.send(200, "application/json", response);
}

static void resetMapTileUploadState(bool removeTemp) {
#if HAS_FILE_STORAGE
    if (mapTileUploadFile) mapTileUploadFile.close();
    if (removeTemp && mapTileUploadTempPath[0]
        && storageFs().exists(mapTileUploadTempPath)) {
        storageFs().remove(mapTileUploadTempPath);
    }
#else
    LV_UNUSED(removeTemp);
#endif
    mapTileUploadZoom = -1;
    mapTileUploadX = -1;
    mapTileUploadY = -1;
    mapTileUploadOk = false;
    mapTileUploadBytes = 0;
    mapTileUploadPath[0] = '\0';
    mapTileUploadTempPath[0] = '\0';
}

static void handleMapTileUpload() {
#if !HAS_FILE_STORAGE
    return;
#else
    HTTPUpload &upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        resetMapTileUploadState(true);
        if (!isLoggedIn() || !stateMapStorageReady()) return;

        int zoom = 0;
        int32_t tileX = 0;
        int32_t tileY = 0;
        if (!nodesMapTileParseUploadFilename(upload.filename.c_str(), zoom, tileX, tileY)) return;
        if (!nodesMapTileEnsureParentDirs(zoom, tileX)) return;

        String path = nodesMapTilePath(zoom, tileX, tileY);
        String tempPath = nodesMapTileUploadTempPath(zoom, tileX, tileY);
        if (path.isEmpty() || tempPath.isEmpty()
            || path.length() >= sizeof(mapTileUploadPath)
            || tempPath.length() >= sizeof(mapTileUploadTempPath)) {
            return;
        }
        snprintf(mapTileUploadPath, sizeof(mapTileUploadPath), "%s", path.c_str());
        snprintf(mapTileUploadTempPath, sizeof(mapTileUploadTempPath), "%s", tempPath.c_str());
        mapTileUploadZoom = zoom;
        mapTileUploadX = tileX;
        mapTileUploadY = tileY;
        if (storageFs().exists(mapTileUploadTempPath)) storageFs().remove(mapTileUploadTempPath);
        mapTileUploadFile = storageFs().open(mapTileUploadTempPath, FILE_WRITE);
        return;
    }

    if (upload.status == UPLOAD_FILE_WRITE) {
        if (!mapTileUploadFile) return;
        if (mapTileUploadBytes + upload.currentSize > kMapTileUploadMaxBytes) {
            mapTileUploadFile.close();
            return;
        }
        size_t written = mapTileUploadFile.write(upload.buf, upload.currentSize);
        if (written != upload.currentSize) {
            mapTileUploadFile.close();
            return;
        }
        mapTileUploadBytes += written;
        return;
    }

    if (upload.status == UPLOAD_FILE_END) {
        if (!mapTileUploadFile) return;
        mapTileUploadFile.close();
        mapTileUploadOk = mapTileUploadBytes >= 24;
        return;
    }

    resetMapTileUploadState(true);
#endif
}

static void handleMapTileUploadDone() {
#if !HAS_FILE_STORAGE
    server.send(503, "application/json", "{\"ok\":false,\"error\":\"no storage\"}");
#else
    if (!isLoggedIn()) {
        resetMapTileUploadState(true);
        server.send(403, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}");
        return;
    }
    if (!nodesMapTileCoordsValid(mapTileUploadZoom, mapTileUploadX, mapTileUploadY)
        || !mapTileUploadPath[0] || !mapTileUploadTempPath[0]) {
        resetMapTileUploadState(true);
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid tile key\"}");
        return;
    }
    if (!mapTileUploadOk || !nodesMapTileFileValid(mapTileUploadTempPath)) {
        resetMapTileUploadState(true);
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid tile PNG\"}");
        return;
    }

    bool alreadyCached = storageFs().exists(mapTileUploadPath)
                         && nodesMapTileFileValid(mapTileUploadPath);
    if (alreadyCached) {
        storageFs().remove(mapTileUploadTempPath);
    } else {
        if (storageFs().exists(mapTileUploadPath)) storageFs().remove(mapTileUploadPath);
        if (!storageFs().rename(mapTileUploadTempPath, mapTileUploadPath)) {
            resetMapTileUploadState(true);
            server.send(500, "application/json", "{\"ok\":false,\"error\":\"tile rename failed\"}");
            return;
        }
    }

    char response[192];
    snprintf(response, sizeof(response),
             "{\"ok\":true,\"z\":%d,\"x\":%ld,\"y\":%ld,\"bytes\":%u,\"cached\":%s}",
             mapTileUploadZoom, (long)mapTileUploadX, (long)mapTileUploadY,
             (unsigned)mapTileUploadBytes, alreadyCached ? "true" : "false");
    resetMapTileUploadState(false);
    server.send(200, "application/json", response);
#endif
}

static uint8_t mapTileFetchBuffer[1024] = {};

// POST /map-tile-fetch?z=13&x=123&y=456
// The browser cannot identify itself to the upstream tile service through the
// operator proxy, so Web Config asks the device to fetch one tile at a time
// using Camillia's stable application User-Agent.
static void handlePostMapTileFetch() {
    MapTileFetchDiag diag;
    diag.startedAtMs = millis();
#if !HAS_FILE_STORAGE
    diag.stage = "storage";
    sendMapTileFetchError(503, "no storage", diag);
#else
    if (!isLoggedIn()) {
        diag.stage = "auth";
        sendMapTileFetchError(403, "unauthorized", diag);
        return;
    }
    if (!server.hasArg("z") || !server.hasArg("x") || !server.hasArg("y")) {
        sendMapTileFetchError(400, "missing tile coordinates", diag);
        return;
    }

    int zoom = server.arg("z").toInt();
    int32_t tileX = (int32_t)server.arg("x").toInt();
    int32_t tileY = (int32_t)server.arg("y").toInt();
    diag.zoom = zoom;
    diag.tileX = tileX;
    diag.tileY = tileY;
    if (!nodesMapTileCoordsValid(zoom, tileX, tileY)) {
        sendMapTileFetchError(400, "invalid tile coordinates", diag);
        return;
    }
    if (!stateMapStorageReady() || !nodesMapTileEnsureParentDirs(zoom, tileX)) {
        diag.stage = "storage";
        sendMapTileFetchError(503, "storage unavailable", diag);
        return;
    }

    String path = nodesMapTilePath(zoom, tileX, tileY);
    String tempPath = nodesMapTileUploadTempPath(zoom, tileX, tileY);
    if (storageFs().exists(path.c_str()) && nodesMapTileFileValid(path.c_str())) {
        const uint32_t totalMs = millis() - diag.startedAtMs;
        char response[176];
        snprintf(response, sizeof(response),
                 "{\"ok\":true,\"cached\":true,\"z\":%d,\"x\":%ld,\"y\":%ld,"
                 "\"totalMs\":%u}",
                 zoom, (long)tileX, (long)tileY, (unsigned)totalMs);
        server.send(200, "application/json", response);
        Serial.printf("[map-web] ok cached=1 z=%d x=%ld y=%ld total=%ums\n",
                      zoom, (long)tileX, (long)tileY, (unsigned)totalMs);
        return;
    }
    if (storageFs().exists(path.c_str())) storageFs().remove(path.c_str());
    if (storageFs().exists(tempPath.c_str())) storageFs().remove(tempPath.c_str());

    char url[192];
    snprintf(url, sizeof(url), "http://maps.camillia.sumat.org/%d/%ld/%ld.png",
             zoom, (long)tileX, (long)tileY);
    WiFiClient client;
    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout(5000);
    http.setTimeout(12000);
    if (!http.begin(client, url)) {
        client.stop();
        diag.stage = "proxy-connect";
        sendMapTileFetchError(502, "proxy connection failed", diag);
        return;
    }
    char userAgent[112];
    snprintf(userAgent, sizeof(userAgent),
             "Camillia-MT/%s (+https://github.com/oumike/camillia-mt)",
             APP_VERSION);
    http.addHeader("User-Agent", userAgent);
    const char *responseHeaders[] = {"x-blocked"};
    http.collectHeaders(responseHeaders, 1);

    const uint32_t headersStartedAtMs = millis();
    int upstreamCode = http.GET();
    diag.headersMs = millis() - headersStartedAtMs;
    diag.upstreamCode = upstreamCode;
    diag.expectedBytes = http.getSize();
    diag.blocked = http.hasHeader("x-blocked");
    if (upstreamCode != HTTP_CODE_OK || diag.blocked) {
        diag.stage = "upstream-status";
        http.end();
        client.stop();
        sendMapTileFetchError(502, "tile service rejected request", diag);
        return;
    }

    int remaining = http.getSize();
    if (remaining > (int)kMapTileUploadMaxBytes) {
        diag.stage = "upstream-size";
        diag.remainingBytes = remaining;
        http.end();
        client.stop();
        sendMapTileFetchError(502, "tile response too large", diag);
        return;
    }
    File output = storageFs().open(tempPath.c_str(), FILE_WRITE);
    if (!output) {
        diag.stage = "storage-open";
        diag.remainingBytes = remaining;
        http.end();
        client.stop();
        sendMapTileFetchError(503, "tile file open failed", diag);
        return;
    }

    WiFiClient *stream = http.getStreamPtr();
    size_t written = 0;
    bool ok = true;
    bool idleTimedOut = false;
    const uint32_t bodyStartedAtMs = millis();
    uint32_t idleDeadline = millis() + 12000UL;
    while (http.connected() && (remaining > 0 || remaining < 0)) {
        int available = stream->available();
        if (available <= 0) {
            if ((int32_t)(millis() - idleDeadline) >= 0) {
                idleTimedOut = true;
                break;
            }
            delay(1);
            continue;
        }
        size_t wanted = (size_t)available;
        if (wanted > sizeof(mapTileFetchBuffer)) wanted = sizeof(mapTileFetchBuffer);
        if (remaining > 0 && wanted > (size_t)remaining) wanted = (size_t)remaining;
        if (written + wanted > kMapTileUploadMaxBytes) {
            ok = false;
            break;
        }
        int read = stream->readBytes(mapTileFetchBuffer, wanted);
        if (read <= 0) {
            if ((int32_t)(millis() - idleDeadline) >= 0) {
                idleTimedOut = true;
                break;
            }
            delay(1);
            continue;
        }
        size_t fileWritten = output.write(mapTileFetchBuffer, (size_t)read);
        if (fileWritten != (size_t)read) {
            ok = false;
            break;
        }
        written += fileWritten;
        if (remaining > 0) remaining -= read;
        idleDeadline = millis() + 12000UL;
        if (remaining == 0) break;
        delay(1);
    }
    output.close();
    diag.bodyMs = millis() - bodyStartedAtMs;
    diag.stage = "body";
    diag.writtenBytes = written;
    diag.remainingBytes = remaining;
    diag.connected = http.connected();
    diag.idleTimedOut = idleTimedOut;
    diag.pngValid = written >= 24 && nodesMapTileFileValid(tempPath.c_str());
    http.end();
    client.stop();

    if (!ok || written < 24 || remaining > 0 || !diag.pngValid) {
        storageFs().remove(tempPath.c_str());
        sendMapTileFetchError(502, "incomplete tile response", diag);
        return;
    }
    if (!storageFs().rename(tempPath.c_str(), path.c_str())) {
        diag.stage = "storage-rename";
        storageFs().remove(tempPath.c_str());
        sendMapTileFetchError(500, "tile rename failed", diag);
        return;
    }

    const uint32_t totalMs = millis() - diag.startedAtMs;
    char response[320];
    snprintf(response, sizeof(response),
             "{\"ok\":true,\"cached\":false,\"z\":%d,\"x\":%ld,\"y\":%ld,"
             "\"bytes\":%u,\"upstream\":%d,\"expected\":%d,"
             "\"headersMs\":%u,\"bodyMs\":%u,\"totalMs\":%u,\"heap\":%u}",
             zoom, (long)tileX, (long)tileY, (unsigned)written,
             upstreamCode, diag.expectedBytes,
             (unsigned)diag.headersMs, (unsigned)diag.bodyMs, (unsigned)totalMs,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    server.send(200, "application/json", response);
    Serial.printf("[map-web] ok cached=0 z=%d x=%ld y=%ld upstream=%d "
                  "expected=%d written=%u headers=%ums body=%ums total=%ums\n",
                  zoom, (long)tileX, (long)tileY, upstreamCode,
                  diag.expectedBytes, (unsigned)written,
                  (unsigned)diag.headersMs, (unsigned)diag.bodyMs, (unsigned)totalMs);
#endif
}
#endif  // HAS_STATE_MAPS

