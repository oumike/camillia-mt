// SPDX-License-Identifier: GPL-3.0-or-later
// Part of web_config.cpp: API: clearing data, factory reset, wardrive, export, assets, files.
// Included in order from web_config.cpp as one translation unit; not built on its own.

static void handlePostClearMessages() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    Channels.clearAllMessages(true);
    DMs.clearAll(true);
    redirectHomeWithFlash("All stored messages cleared.");
}

// ── Reset Chat Colors ─────────────────────────────────────────
//
// Nothing per-node is stored: the color is a hash of the node id, and this
// re-rolls the salt that hash is mixed with. gOnSave persists it; the chat
// repaints on its own because the salt is part of the render signature.
static void handlePostResetChatColors() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    if (!gCfg) { redirect("/"); return; }
    gCfg->chatColorSalt = cfgNextChatColorSalt(gCfg->chatColorSalt);
    if (gOnSave) gOnSave();
    redirectHomeWithFlash("Chat colors reassigned.");
}

// ── Saved WiFi networks ───────────────────────────────────────
// Applied straight through rather than queued: these handlers run inside
// webCfgLoop() on the main loop thread, which is the thread that owns NVS and
// the WiFi state everywhere else.
//
// Not built for the CardputerZero, where the OS owns the network: the routes
// are never registered, so a post to them is a 404 and changes nothing.
#if !defined(PLATFORM_LINUX)
static void handlePostWifiAdd() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    ssid.trim();
    if (ssid.isEmpty()) {
        redirectHomeWithFlash("Network name cannot be empty.");
        return;
    }
    if (cfgSavedWifiRemember(ssid.c_str(), pass.c_str())) {
        redirectHomeWithFlash("Network remembered.");
    } else {
        // Refused for a reason worth naming rather than a silent no-op: the
        // active network is already listed, and "AP" is the picker's sentinel.
        redirectHomeWithFlash("Not added - that is the active network, or a reserved name.");
    }
}

static void handlePostWifiForget() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    String ssid = server.arg("ssid");
    ssid.trim();
    if (cfgSavedWifiRemove(ssid.c_str())) {
        redirectHomeWithFlash("Network forgotten.");
    } else {
        redirectHomeWithFlash("That network is not in the remembered list.");
    }
}

static void handlePostWifiUse() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    String ssid = server.arg("ssid");
    ssid.trim();
    if (!cfgSavedWifiActivate(ssid.c_str())) {
        redirectHomeWithFlash("That network is not in the remembered list.");
        return;
    }
    // Answer before the radio moves. The switch re-associates the station, so a
    // browser reading this over WiFi is about to lose the device; sending the
    // page first at least lets the message land.
    redirectHomeWithFlash("Switched network. Reconnecting - this page may become "
                          "unreachable until you join the new network.");
}
#endif  // !PLATFORM_LINUX

#if HAS_ADMIN_TERMINAL
// ── Remote admin ──────────────────────────────────────────────
// The browser shares the device's one session rather than running a rival with
// its own key: opening the terminal here on a node the device already has open
// continues the same conversation.

static void handleGetAdminData() {
    if (!isLoggedIn()) { server.send(401, "text/plain", "auth"); return; }

    // Shaped like /chat-data: the client sends the revision it last rendered and
    // gets nothing back when there is nothing new, so an open terminal is cheap
    // to poll.
    String out = "{\"open\":";
    out += webCfgAdminSessionOpen() ? "true" : "false";
    out += ",\"node\":\"";
    char idbuf[16];
    snprintf(idbuf, sizeof(idbuf), "!%08lx", (unsigned long)webCfgAdminNodeId());
    out += idbuf;

    // The name the rest of the UI would show for this node: long, then short,
    // then nothing -- the browser falls back to the id, which is all a node
    // heard only as a relayed packet ever gives us.
    //
    // Resolved here rather than passed in from the button that opened the
    // terminal: it is set over the air by whoever owns that node, so it needs
    // escaping, and the escaping already exists on this path. It also means a
    // browser attaching to a session the device opened gets the name too.
    out += "\",\"name\":\"";
    {
        const NodeEntry *n = Nodes.find(webCfgAdminNodeId());
        const char *name = nullptr;
        if (n) {
            if (n->longName[0])       name = n->longName;
            else if (n->shortName[0]) name = n->shortName;
        }
        for (const char *c = name ? name : ""; *c; c++) {
            if (*c == '"' || *c == '\\') { out += '\\'; out += *c; }
            else if ((uint8_t)*c < 0x20)   { out += ' '; }
            else                           { out += *c; }
        }
    }

    out += "\",\"rev\":";
    out += String((unsigned long)webCfgAdminRevision());
    out += ",\"busy\":";
    out += webCfgAdminBusy() ? "true" : "false";
    out += ",\"confirm\":";
    out += webCfgAdminAwaitingConfirm() ? "true" : "false";
    out += ",\"lines\":[";
    const int n = webCfgAdminLineCount();
    for (int i = 0; i < n; i++) {
        uint8_t kind = 0;
        const char *text = webCfgAdminLine(i, kind);
        if (!text) continue;
        if (i) out += ",";
        out += "{\"k\":";
        out += String((int)kind);
        out += ",\"t\":\"";
        for (const char *c = text; *c; c++) {
            if (*c == '"' || *c == '\\') { out += '\\'; out += *c; }
            else if ((uint8_t)*c < 0x20)   { out += ' '; }
            else                           { out += *c; }
        }
        out += "\"}";
    }
    out += "]}";
    server.send(200, "application/json", out);
}

// The command table, served from the firmware rather than duplicated in the
// page. One help text for the device terminal and the browser was the point of
// putting the parser in admin_client; a copy here would drift the first time a
// command changed.
static void handleGetAdminHelp() {
    if (!isLoggedIn()) { server.send(401, "text/plain", "auth"); return; }
    String out;
    for (int i = 0; i < AdminClient::Session::helpLineCount(); i++) {
        out += AdminClient::Session::helpLine(i);
        out += "\n";
    }
    server.send(200, "text/plain", out);
}

static void handlePostAdminSend() {
    if (!isLoggedIn()) { server.send(401, "text/plain", "auth"); return; }
    const String line = server.arg("line");
    const String node = server.arg("node");
    if (node.length()) {
        const uint32_t id = (uint32_t)strtoul(
            node.startsWith("!") ? node.c_str() + 1 : node.c_str(), nullptr, 16);
        // Opening is gated on the peer being CONFIRMED, in one place shared with
        // the device, so a hand-made POST cannot reach a node the UI would not
        // have offered.
        if (id && !webCfgAdminOpen(id)) {
            server.send(403, "text/plain", "not an administerable peer");
            return;
        }
    }
    webCfgAdminSubmit(line.c_str());
    handleGetAdminData();
}

// Answers with a line of text rather than a redirect. A redirect would reload
// the page and drop the user back on the Config tab, which is where the tab
// state resets to -- losing their place to report the result of a probe that has
// not even happened yet.
static void handlePostAdminPeerVerify() {
    if (!isLoggedIn()) { server.send(401, "text/plain", "auth"); return; }
    const String node = server.arg("node");
    const uint32_t id = (uint32_t)strtoul(
        node.startsWith("!") ? node.c_str() + 1 : node.c_str(), nullptr, 16);

    if (!id || !AdminPeerList.find(id)) {
        server.send(404, "text/plain", "not listed");
        return;
    }
    if (!webCfgAdminVerify(id)) {
        // Either another probe is still out, or the node has no public key --
        // which is the one case that will never come good however often it is
        // asked, because PKI is the only path the remote accepts.
        server.send(409, "text/plain",
                    "busy, or that node has sent us no public key");
        return;
    }
    server.send(200, "text/plain", "probing");
}

// Restarts the favourites sweep. Same answer shape as Verify -- a line of text,
// not a redirect -- so the page keeps the tab the user is standing on.
static void handlePostAdminRescan() {
    if (!isLoggedIn()) { server.send(401, "text/plain", "auth"); return; }

    uint32_t waitSecs = 0;
    const int r = webCfgAdminRescanFavorites(waitSecs);
    char msg[96];
    switch (r) {
        case ADMIN_RESCAN_COOLING:
            snprintf(msg, sizeof(msg), "Too soon \xe2\x80\x94 try again in %lus",
                     (unsigned long)waitSecs);
            // 429 rather than 409: this one comes good on its own, and saying so
            // in the status is what lets the page treat it as a wait instead of
            // an error worth reporting twice.
            server.send(429, "text/plain", msg);
            return;
        case ADMIN_RESCAN_NOTHING:
            server.send(409, "text/plain",
                        "No favourite node has sent us a public key");
            return;
        default:
            break;
    }
    // The sweep is paced at one node every few seconds, so this reports what it
    // set going rather than what it found. The table shows the results as they
    // land, on the next reload.
    snprintf(msg, sizeof(msg), "Asking %d favourite%s\xe2\x80\xa6",
             r, r == 1 ? "" : "s");
    server.send(200, "text/plain", msg);
}
#endif  // HAS_ADMIN_TERMINAL

#if HAS_SD_MALWARE_SCAN
// ── SD card malware scan ──────────────────────────────────────

static void handlePostSdScan() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    // Same call the other SD actions here use. storageMounted() is not it: on
    // the SPI-SD boards the mount happens in config_io.cpp and never sets that
    // flag, so it answers "no card" for a card that is present.
    if (!sdBegin()) {
        gSdScanHits = 0;
        snprintf(gSdScanResult, sizeof(gSdScanResult), "no card mounted");
        redirectHomeWithFlash("SD scan: no card is mounted.");
        return;
    }

    uint16_t scanned = 0;
    gSdScanHits = storageScanCard(nullptr, 0, &scanned);
    snprintf(gSdScanResult, sizeof(gSdScanResult),
             "%u file%s examined, %u match%s (see the serial log for names)",
             (unsigned)scanned, (scanned == 1) ? "" : "s",
             (unsigned)gSdScanHits, (gSdScanHits == 1) ? "" : "es");

    char flash[96];
    if (gSdScanHits) {
        snprintf(flash, sizeof(flash),
                 "SD scan: %u file(s) match Windows malware.", (unsigned)gSdScanHits);
    } else {
        snprintf(flash, sizeof(flash),
                 "SD scan: nothing matching Windows malware found.");
    }
    redirectHomeWithFlash(flash);
}

static void handlePostSdRepair() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    // Only reachable after a scan found something, and storageRepairCard()
    // guards itself anyway -- but a card pulled in between would otherwise
    // report "0 removed", which reads as the feature failing rather than as
    // there being no card to work on.
    if (!sdBegin()) {
        gSdScanHits = 0;
        snprintf(gSdScanResult, sizeof(gSdScanResult), "no card mounted");
        redirectHomeWithFlash("SD repair: no card is mounted.");
        return;
    }

    uint16_t failed = 0;
    const uint16_t removed = storageRepairCard(&failed);

    // Re-derived rather than decremented: the button must disappear because a
    // fresh walk finds nothing, not because a counter was adjusted to say so.
    uint16_t scanned = 0;
    gSdScanHits = storageScanCard(nullptr, 0, &scanned);
    snprintf(gSdScanResult, sizeof(gSdScanResult),
             "%u removed, %u failed, %u still matching",
             (unsigned)removed, (unsigned)failed, (unsigned)gSdScanHits);

    char flash[96];
    snprintf(flash, sizeof(flash),
             "SD repair: %u removed%s. Reformatting is the only certain fix.",
             (unsigned)removed,
             failed ? ", some could not be deleted" : "");
    redirectHomeWithFlash(flash);
}
#endif  // HAS_SD_MALWARE_SCAN

// ── Clear Nodes ───────────────────────────────────────────────

// Delete the on-SD node-database files. Legacy paths: nothing in this firmware
// writes them any more, but a card carrying them from an older build would
// otherwise resurrect nodes that were just cleared. The eviction archive goes
// too only when asked for (Clear Nodes' "Include archived nodes" box), as on
// the device. Returns true when the archive was cleared.
static bool clearNodeDbFilesOnSd(bool includeArchive) {
#if HAS_FILE_STORAGE
    if (!sdBegin()) return false;

    const bool archiveCleared = includeArchive && nodeArchiveClear();

    const char *nodeFiles[] = {
        "/camillia/nodes.db",
        "/camillia/nodes.bin",
        "/camillia/nodes.json",
        "/camillia/node_db.bin",
        "/camillia/node_db.json",
    };
    for (size_t i = 0; i < sizeof(nodeFiles) / sizeof(nodeFiles[0]); i++) {
        if (storageFs().exists(nodeFiles[i])) {
            storageFs().remove(nodeFiles[i]);
        }
    }

    File nodesDir = storageFs().open("/camillia/nodes");
    if (nodesDir && nodesDir.isDirectory()) {
        File f = nodesDir.openNextFile();
        while (f) {
            String fp = String("/camillia/nodes/") + f.name();
            f.close();
            storageFs().remove(fp.c_str());
            f = nodesDir.openNextFile();
        }
        nodesDir.close();
        storageFs().rmdir("/camillia/nodes");
    }
    return archiveCleared;
#else
    (void)includeArchive;
    return false;
#endif
}

// The archive alone, for a clear that keeps favorites: the legacy files above
// are whole-table snapshots and would take the favorites with them.
static bool clearNodeArchiveFileOnSd() {
#if HAS_FILE_STORAGE
    return sdBegin() && nodeArchiveClear();
#else
    return false;
#endif
}

// Mirrors the device's Clear Nodes confirmation: "favorites" and "archive" each
// widen the clear, and each has to ask for itself, so a post carrying neither —
// an older bookmark, a script — gets the recoverable variant. "all" is what the
// old Clear Nodes (All) button posted, and still means both.
static void handlePostClearNodes() {
    if (!isLoggedIn()) { redirect("/login"); return; }

    const bool all      = server.hasArg("all");
    const bool inclFavs = all || server.hasArg("favorites");
    const bool inclArch = (all || server.hasArg("archive")) && nodeArchiveSlotExists();
    bool archiveCleared = false;

    char msg[128];
#if NODE_ARCHIVE_ON_DISK
    const char *archIncluded = ", archive included";
    const char *archCleared  = ", archive cleared";
    const char *archFailed   = "; archive NOT cleared";
#else
    const char *archIncluded = ", SD archive included";
    const char *archCleared  = ", SD archive cleared";
    const char *archFailed   = "; SD archive NOT cleared (no card)";
#endif
    if (inclFavs) {
        Nodes.clearPersisted();
        archiveCleared = clearNodeDbFilesOnSd(inclArch);
        snprintf(msg, sizeof(msg), "Node database cleared%s. Rebooting now...",
                 !inclArch ? "" : archiveCleared ? archIncluded : archFailed);
    } else {
        const int removed = Nodes.clearNonFavorites();
        // Not clearNodeDbFilesOnSd(): its legacy files are whole-table
        // snapshots and would take the favorites with them.
        if (inclArch) archiveCleared = clearNodeArchiveFileOnSd();
        snprintf(msg, sizeof(msg), "Cleared %d nodes, kept %d favorites%s. Rebooting now...",
                 removed, Nodes.count(),
                 !inclArch ? "" : archiveCleared ? archCleared : archFailed);
    }
    scheduleReboot(200);
    redirectHomeWithFlash(msg);
}

// ── Node favorite toggle ──────────────────────────────────────
// Answers JSON rather than redirecting like the forms above it: the Nodes tab
// has a streamed option list and a map behind it, and rebuilding that page to
// flip one bit would cost seconds on a full table and drop the user back on the
// Config tab. The page updates itself from the response instead.
static void handlePostNodeFavorite() {
    if (!isLoggedIn()) {
        server.send(403, "application/json", "{\"error\":\"unauthorized\"}");
        return;
    }

    // Hex node id, with or without the leading '!' the page displays.
    String idArg = server.arg("id");
    if (idArg.startsWith("!")) idArg.remove(0, 1);
    const uint32_t nodeId = (uint32_t)strtoul(idArg.c_str(), nullptr, 16);
    if (nodeId == 0) {
        server.send(400, "application/json", "{\"error\":\"bad node id\"}");
        return;
    }

    const bool want = (server.arg("fav") == "1");
    NodeEntry *n = Nodes.find(nodeId);
    if (!n || !n->nodeId) {
        server.send(404, "application/json", "{\"error\":\"no such node\"}");
        return;
    }

    // setFavorite() returns false when the flag already matches. That is not a
    // failure for a toggle whose request may have been retried — report the
    // state the node is actually in, which is the state that was asked for.
    (void)Nodes.setFavorite(nodeId, want);

    char body[40];
    snprintf(body, sizeof(body), "{\"ok\":true,\"fav\":%d}", n->favorite ? 1 : 0);
    server.send(200, "application/json", body);
}

// ── Factory Reset ─────────────────────────────────────────────

static void handlePostFactoryReset() {
    if (!isLoggedIn()) { redirect("/login"); return; }

    Channels.clearAllMessages(true);
    DMs.clearAll(true);

    // Everything Camillia keeps in NVS: the whole partition, unless Launcher shares it
    cfgFactoryEraseNvs();
    Nodes.clearPersisted();

    clearNodeDbFilesOnSd(/*includeArchive=*/true);

    // Delete saved DM conversations from SD card (if supported on this board)
#if HAS_FILE_STORAGE
    {
        File dir = storageFs().open("/camillia/dms");
        if (dir && dir.isDirectory()) {
            File f = dir.openNextFile();
            while (f) {
                String fp = String("/camillia/dms/") + f.name();
                f.close();
                storageFs().remove(fp.c_str());
                f = dir.openNextFile();
            }
            dir.close();
            storageFs().rmdir("/camillia/dms");
        }
    }
#endif

    scheduleReboot(900);
    redirectHomeWithFlash("Factory reset complete. Rebooting now...");
}

// ── Export / Import ───────────────────────────────────────────

// Fields in one CSV line, honouring quotes (a long name may hold commas).
static int csvFieldCount(const char *line) {
    int n = 1;
    bool q = false;
    for (const char *p = line; *p; p++) {
        if (*p == '"') q = !q;
        else if (*p == ',' && !q) n++;
    }
    return n;
}

// Export every node the device knows about as CSV: the live table first, then
// any previously archived (evicted) nodes from the SD file. Both share the
// column schema from node_db (nodeCsvHeader/nodeCsvFormatEntry) so the two
// halves line up; a leading "source" column distinguishes them, and
// "archivedEpoch" is populated only for archived rows.
static void handleGetNodesCsv() {
    if (!isLoggedIn()) { redirect("/login"); return; }

    char fileName[64];
    snprintf(fileName, sizeof(fileName), "camillia-nodes-%s.csv",
             (gCfg && gCfg->nodeShort[0]) ? gCfg->nodeShort : "node");
    char cd[128];
    snprintf(cd, sizeof(cd), "attachment; filename=\"%s\"", fileName);
    server.sendHeader("Content-Disposition", cd);
    server.sendHeader("Cache-Control", "no-store");

    // See handleGetStateMapStatus(): the abort flag outlives the request that
    // set it, so a chunked sender that does not clear it here stays silent
    // after any client walks away mid-download.
    gSendAborted = false;

    // Chunked: the live table alone can be MAX_NODES rows, too big to assemble
    // in one String on a device this size.
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/csv", "");

    String out;
    out.reserve(1024);
    out  = "source,archivedEpoch,";
    out += nodeCsvHeader();
    out += "\n";

    const int liveCount = Nodes.count();
    for (int i = 0; i < liveCount; i++) {
        NodeEntry *n = Nodes.getByRank(i);
        if (!n || n->nodeId == 0) continue;
        char rec[512];
        nodeCsvFormatEntry(*n, rec, sizeof(rec));
        out += "live,,";
        out += rec;
        out += "\n";
        if (out.length() > 1024) { sendChunk(out); }
    }
    if (out.length()) { sendChunk(out); }

    // Archived rows are streamed through verbatim: each already begins with its
    // archivedEpoch followed by the shared columns, so only "archived," is
    // prepended. The file's own header line is skipped.
    //
    // An archive outlives firmware updates, so it can hold rows written before
    // columns were appended to the schema. Those are padded with empty fields
    // to the current width, so every row of the export has as many columns as
    // its header and a spreadsheet does not have to guess.
    const int archFields = 1 + csvFieldCount(nodeCsvHeader());   // + archivedEpoch
    const char *archPath = nodeArchiveFilePath();
    if (archPath && sdBegin() && storageFs().exists(archPath)) {
        File af = storageFs().open(archPath, FILE_READ);
        if (af) {
            bool firstLine = true;
            while (af.available()) {
                String line = af.readStringUntil('\n');
                line.trim();
                if (!line.length()) continue;
                if (firstLine) {
                    firstLine = false;
                    if (line.startsWith("archivedEpoch")) continue;   // header
                }
                out += "archived,";
                out += line;
                for (int n = csvFieldCount(line.c_str()); n < archFields; n++) out += ',';
                out += "\n";
                if (out.length() > 1024) { sendChunk(out); }
            }
            af.close();
        }
    }

    if (out.length()) sendChunk(out);
    server.sendContent("");   // terminate the chunked response
}

// The wardrive log, streamed from storage as-is. Flushed first so whatever the
// main loop has not written yet is in the download too.
static void handleGetWardriveCsv() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    const char *path = wardriveLogFilePath();
    wardriveLogFlush();
    if (!path || !sdBegin() || !storageFs().exists(path)) {
        server.send(404, "text/plain", "No wardrive log yet (needs a GPS fix and heard nodes).");
        return;
    }
    File f = storageFs().open(path, FILE_READ);
    if (!f) { server.send(500, "text/plain", "Could not open the wardrive log."); return; }

    char fileName[64];
    snprintf(fileName, sizeof(fileName), "camillia-wardrive-%s.csv",
             (gCfg && gCfg->nodeShort[0]) ? gCfg->nodeShort : "node");
    char cd[128];
    snprintf(cd, sizeof(cd), "attachment; filename=\"%s\"", fileName);
    server.sendHeader("Content-Disposition", cd);
    server.sendHeader("Cache-Control", "no-store");
    gSendAborted = false;
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/csv", "");

    // Straight from the file in fixed blocks: the log grows for as long as a
    // drive lasts, so it is never assembled in RAM. sendSliced() frames each
    // block and reports an abandoned response.
    static char buf[1024];   // static: off the WebServer task's stack
    bool ok = true;
    while (ok && f.available()) {
        const int n = f.read((uint8_t *)buf, sizeof(buf));
        if (n <= 0) break;
        ok = sendSliced(buf, (size_t)n, "wardrive csv");
    }
    f.close();
    if (ok) server.sendContent("");
}

static void handlePostClearWardrive() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    redirectHomeWithFlash(wardriveLogClear() ? "Wardrive log cleared."
                                             : "Could not clear the wardrive log (no storage?).");
}

// Export every stored message as CSV, streamed straight to the browser. Web
// config only, and deliberately never written to the card: the device already
// persists chat to keep it across reboots, and a second copy sitting in the
// filesystem would be one more thing to manage on a device whose storage is
// shared with the radio. A backup you take is a backup you have.
//
// Wrapped channel messages are put back together here. ChannelMgr::_wordWrap()
// stores one line per wrap and pushes them last-segment-first (the device draws
// newest at the top), so a raw dump of the ring would come out with every
// multi-line message reversed. Same reassembly the web chat view does.
static void csvAppendQuoted(String &out, const char *text) {
    out += '"';
    for (const char *p = text ? text : ""; *p; p++) {
        if (*p == '"') out += '"';        // doubled, per RFC 4180
        if (*p == '\r') continue;         // CR would confuse the line ending
        out += *p;
    }
    out += '"';
}

static const char *msgAckName(uint8_t ack) {
    switch (ack) {
        case DisplayLine::PENDING:     return "PENDING";
        case DisplayLine::ACKED:       return "ACKED";
        case DisplayLine::ACKED_RELAY: return "ACKED_RELAY";
        case DisplayLine::NAKED:       return "NAKED";
        case DisplayLine::TX_FAILED:   return "TX_FAILED";
        default:                       return "";
    }
}

// Local calendar time for an epoch, or empty when the clock was not set when
// the message arrived. Never invents a date for a 0 epoch.
static void msgIsoTime(uint32_t epoch, char *out, size_t outLen) {
    if (epoch < 1700000000UL) { out[0] = '\0'; return; }
    time_t t = (time_t)epoch;
    struct tm lt;
    localtime_r(&t, &lt);
    strftime(out, outLen, "%Y-%m-%d %H:%M:%S", &lt);
}

static void handleGetMessagesCsv() {
    if (!isLoggedIn()) { redirect("/login"); return; }

    char fileName[64];
    snprintf(fileName, sizeof(fileName), "camillia-messages-%s.csv",
             (gCfg && gCfg->nodeShort[0]) ? gCfg->nodeShort : "node");
    char cd[128];
    snprintf(cd, sizeof(cd), "attachment; filename=\"%s\"", fileName);
    server.sendHeader("Content-Disposition", cd);
    server.sendHeader("Cache-Control", "no-store");

    // Same reset as the node CSV above, for the same reason.
    gSendAborted = false;

    // Chunked for the same reason as the node CSV: every channel of history
    // plus every DM is far too much to assemble in one String here.
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/csv", "");

    String out;
    out.reserve(1024);
    out = "source,channel,peer,epoch,localTime,sender,packetId,ack,text\n";

    char iso[24];

    for (int ch = 0; ch < MESH_CHANNELS; ch++) {
        Channel &c = Channels.get(ch);
        if (!c.lines || c.count <= 0) continue;
        const char *chanName = c.name ? c.name : "";
        const int oldest = max(0, c.count - MAX_MSG_LINES);

        int i = oldest;
        while (i < c.count) {
            const DisplayLine &head0 = c.lines[i % MAX_MSG_LINES];
            const uint32_t pid = head0.packetId;
            int j = i + 1;
            if (pid != 0) {
                while (j < c.count && c.lines[j % MAX_MSG_LINES].packetId == pid) j++;
            }
            // Back to front: the last stored segment is the start of the
            // message, and it is the line carrying the prefix and the ack.
            const DisplayLine &head = c.lines[(j - 1) % MAX_MSG_LINES];
            String text = head.text;
            for (int k = j - 2; k >= i; k--) {
                const char *seg = c.lines[k % MAX_MSG_LINES].text;
                while (*seg == ' ') seg++;      // drop the continuation indent
                if (*seg) { text += ' '; text += seg; }
            }

            msgIsoTime(head.epoch, iso, sizeof(iso));
            char meta[128];
            snprintf(meta, sizeof(meta), "channel,%d %s,,%lu,%s,!%08lX,%lu,%s,",
                     ch, chanName,
                     (unsigned long)head.epoch, iso,
                     (unsigned long)head.senderNodeId,
                     (unsigned long)head.packetId,
                     msgAckName((uint8_t)head.ack));
            out += meta;
            csvAppendQuoted(out, text.c_str());
            out += "\n";
            if (out.length() > 1024) { sendChunk(out); }
            i = j;
        }
    }
    if (out.length()) { sendChunk(out); }

    // DMs store one line per message — DmMgr does no wrapping, it lets the UI
    // wrap the label — so these need no reassembly.
    const int convCount = DMs.count();
    for (int ci = 0; ci < convCount; ci++) {
        DmConv *conv = DMs.getByRank(ci);
        if (!conv || !conv->lines || conv->count <= 0) continue;
        char peer[32];
        snprintf(peer, sizeof(peer), "!%08lX %s",
                 (unsigned long)conv->nodeId, conv->shortName);
        const int oldest = max(0, conv->count - MAX_DM_LINES);
        for (int i = oldest; i < conv->count; i++) {
            const DmLine &dl = conv->lines[i % MAX_DM_LINES];
            msgIsoTime(dl.epoch, iso, sizeof(iso));
            char meta[160];
            snprintf(meta, sizeof(meta), "dm,,%s,%lu,%s,,%lu,%s,",
                     peer,
                     (unsigned long)dl.epoch, iso,
                     (unsigned long)dl.packetId,
                     msgAckName((uint8_t)dl.ack));
            out += meta;
            csvAppendQuoted(out, dl.text);
            out += "\n";
            if (out.length() > 1024) { sendChunk(out); }
        }
    }

    if (out.length()) sendChunk(out);
    server.sendContent("");   // terminate the chunked response
}

static void handleGetExport() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    if (!gCfg) { server.send(500, "text/plain", "No config"); return; }
    String yaml;
    char fileName[64];
    char cd[128];
    buildExportFileName(gCfg->nodeShort, fileName, sizeof(fileName));
    snprintf(cd, sizeof(cd), "attachment; filename=\"%s\"", fileName);
    cfgToYaml(*gCfg, yaml);
    server.sendHeader("Content-Disposition", cd);
    server.send(200, "text/x-yaml", yaml);
}

static void handlePostExportSd() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    if (!gCfg) { server.send(500, "text/plain", "No config"); return; }
    // The user just asked; a card inserted since the last failed probe must
    // not be refused by the cooldown (same as the on-device Export).
    sdForceRescan();
    const bool ok = cfgExport(*gCfg);
    redirectHomeWithFlash(ok ? "Config exported to /camillia/config.yaml on the SD card."
                             : "Export to SD failed: no card mounted, or the write failed.");
}

// Static, not heap: 8 KB is more than AP mode could allocate contiguously, and
// the buffer exists either way since both route sets share these handlers.
static char   importBuf[8192];
static size_t importLen = 0;
static bool   importOk  = false;
static bool   importOverflow = false;

static void handleImportUpload() {
    // Same gate as handleImportDone, so an unauthenticated upload never
    // leaves a file armed for a later cookie-bearing POST to apply.
    if (!isLoggedIn()) { importOk = false; importLen = 0; return; }
    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        importLen = 0;
        importOk  = false;
        importOverflow = false;
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        // Refuse the file rather than fill the buffer and parse what fit: a
        // truncated YAML still parses, so the old clamp applied the first N
        // settings of the file and silently dropped the rest.
        if (importOverflow) return;              // keep draining the socket
        const size_t space = sizeof(importBuf) - importLen - 1;
        if (upload.currentSize > space) {
            importOverflow = true;
            return;
        }
        memcpy(importBuf + importLen, upload.buf, upload.currentSize);
        importLen += upload.currentSize;
    } else if (upload.status == UPLOAD_FILE_END) {
        if (importOverflow) return;
        importBuf[importLen] = '\0';
        importOk = true;
    }
}

// AP mode answers inline instead of redirecting home. A 302 to "/" makes the
// browser pull the whole lite config page again — the largest allocation this
// mode makes — and on success it would do that while a reboot is already
// scheduled. A couple hundred bytes says the same thing for none of the heap.
static void sendImportResult(const char *msg) {
    if (!webCfgUseLite()) {
        redirectHomeWithFlash(msg);
        return;
    }
    String page;
    page.reserve(320);
    page = "<!doctype html><meta name='viewport' content='width=device-width,"
           "initial-scale=1'><body style='font-family:sans-serif;background:#12161c;"
           "color:#e6e6e6;padding:1.2em'><p>";
    appendAttr(page, msg);
    page += "</p><p><a href='/' style='color:#6cf'>Back to config</a></p></body>";
    server.send(200, "text/html", page);
}

static void handleImportDone() {
    if (!isLoggedIn()) { importOk = false; importLen = 0; redirect("/login"); return; }
    if (!gCfg) { redirect("/"); return; }
    if (importOverflow) {
        sendImportResult("Import failed: file larger than 8 KB.");
        return;
    }
    if (!importOk || importLen == 0) {
        sendImportResult("Import failed: no data received.");
        return;
    }
    if (!cfgImportFromBuf(importBuf, importLen, *gCfg)) {
        sendImportResult("Import failed: parse error.");
        return;
    }
    if (!gCfg->webCfgPass[0]) {
        strncpy(gCfg->webCfgPass, kDefaultWebPass, sizeof(gCfg->webCfgPass) - 1);
        gCfg->webCfgPass[sizeof(gCfg->webCfgPass) - 1] = '\0';
    }

    // Apply imported WiFi credentials to runtime and NVS.
    strncpy(gWifiSsid, gCfg->wifiSsid, sizeof(gWifiSsid) - 1);
    gWifiSsid[sizeof(gWifiSsid) - 1] = '\0';
    strncpy(gWifiPass, gCfg->wifiPass, sizeof(gWifiPass) - 1);
    gWifiPass[sizeof(gWifiPass) - 1] = '\0';
    {
        Preferences prefs;
        prefs.begin("camillia", false);
        prefs.putString("wifiSsid", gWifiSsid);
        prefs.putString("wifiPass", gWifiPass);
        prefs.end();
    }

#if FEATURE_LICENSED_MODE
    // Licensed now, or by the imported config: its channels come in keyless,
    // stripped before gOnSave() persists them and puts them on the radio.
    const bool licKeysStripped =
        (lic::active() || gCfg->licensedMode) && licStripChannelsForImport();
#else
    const bool licKeysStripped = false;
#endif
    if (gOnSave) gOnSave();
    scheduleReboot(900);
    // Plain text: both result paths HTML-escape the message when rendering it.
    const char *warn = cfgImportWarning();
    // The licensed note goes first: the flash message is 128 bytes.
    const char *licNote = licKeysStripped
        ? " Licensed mode: channel imported without its key." : "";
    char msg[sizeof(gFlashMsg)];
    if (warn && warn[0]) {
        snprintf(msg, sizeof(msg), "Import complete.%s %s. Rebooting now...", licNote, warn);
    } else {
        snprintf(msg, sizeof(msg), "Import complete.%s Rebooting now...", licNote);
    }
    sendImportResult(msg);
}

// The full page's stylesheet. Cached for a year: the page links it as
// /app.css?v=<hash of its bytes>, so any change is a new URL. No login, like the
// favicon: it is the same bytes for everyone and the login page uses it.
static void handleGetAppCss() {
    server.sendHeader("Cache-Control", "public, max-age=31536000, immutable");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/css", "");
    sendFlash(kAppCss);
    server.sendContent("");
}

static void handleGetFavicon() {
    // Cached hard: it is a fixed asset on a device whose whole job is to answer
    // a handful of requests without stalling, and re-serving it on every page
    // load costs a round trip this server can feel.
    server.sendHeader("Cache-Control", "public, max-age=604800, immutable");
    server.send(200, "image/svg+xml", kFavicon);
}

static void handleGetLogout() {
    sessionToken[0] = '\0';
    server.sendHeader("Set-Cookie", "sess=; Path=/; Max-Age=0");
    redirect("/login");
}

#if HAS_WEB_FILES
// ── Files tab ─────────────────────────────────────────────────
// Browse, upload, download, copy and delete on whatever storageFs() is: the SD
// card, or internal flash on boards without a slot. Full route set only, like
// chat and the live feed.
//
// Every request checks the device setting itself, not just whether the tab was
// drawn: switching Web Files off on the device has to lock a page that is
// already open, and nothing stops a script calling these directly.
//
// All of it runs on the main loop, as every handler here does. A large
// download, upload or copy holds the UI and radio for as long as it takes; the
// alternative is a second task touching the filesystem the firmware is
// writing, which none of the backends here are safe against.

static const size_t kFsMaxPath = 200;   // well inside FAT and LittleFS limits

// Download and copy borrow the config import buffer rather than adding one of
// their own: it is idle outside an /import, requests are handled one at a time,
// and a new static buffer would come out of the Cardputer's internal RAM, which
// has none to spare.
static uint8_t *fsIoBuf() { return (uint8_t *)importBuf; }
static const size_t kFsIoBufLen = sizeof(importBuf);

static bool filesAllowed() {
    return gCfg && cfgWebFilesEnabled(*gCfg) && isLoggedIn();
}

static void fsReply(int code, const char *error) {
    String out = "{\"ok\":false,\"error\":\"";
    appendJsonEscaped(out, error);
    out += "\"}";
    server.send(code, "application/json", out);
}

static void fsReplyOk() {
    server.send(200, "application/json", "{\"ok\":true}");
}

// Checks a path from the browser and puts it in one form: absolute, no empty,
// "." or ".." segments, no trailing slash except on "/" itself. Rejecting "..":
// everything this can reach is already reachable, but a path that means
// something other than what it says has no business here.
static bool fsCleanPath(const String &in, String &out) {
    if (in.isEmpty() || in[0] != '/' || in.length() > kFsMaxPath) return false;
    String p = in;
    while (p.length() > 1 && p.endsWith("/")) p.remove(p.length() - 1);
    size_t segStart = 1;
    for (size_t i = 1; i <= p.length(); i++) {
        const char c = (i < p.length()) ? p[i] : '/';
        if ((unsigned char)c < 0x20 || c == '\\') return false;
        if (c != '/') continue;
        const String seg = p.substring(segStart, i);
        if (p.length() > 1 && (seg.isEmpty() || seg == "." || seg == "..")) return false;
        segStart = i + 1;
    }
    out = p;
    return true;
}

static String fsJoin(const String &dir, const String &name) {
    return (dir == "/") ? ("/" + name) : (dir + "/" + name);
}

static String fsParent(const String &path) {
    const int slash = path.lastIndexOf('/');
    return (slash <= 0) ? String("/") : path.substring(0, slash);
}

// File::name() is the bare name on current cores, a full path on old ones.
static String fsBaseName(const char *name) {
    const char *slash = strrchr(name ? name : "", '/');
    return String(slash ? slash + 1 : (name ? name : ""));
}

static bool fsIsDir(const String &path) {
    File f = storageFs().open(path);
    const bool dir = f && f.isDirectory();
    if (f) f.close();
    return dir;
}

// Ready to use, and allowed to. Answers the request itself when either fails.
static bool fsBeginRequest() {
    if (!filesAllowed()) { fsReply(403, "unauthorized"); return false; }
    // sdBegin(), not storageBegin(): see stateMapStorageReady().
    if (!sdBegin()) { fsReply(503, "no storage"); return false; }
    return true;
}

// GET /fs-list?path=/dir
// Streamed: a map-tile folder holds thousands of entries, far more than one
// String should carry. Capped all the same, because a browser asked to draw
// that many rows is no better off.
static void handleGetFsList() {
    if (!fsBeginRequest()) return;
    String path;
    if (!fsCleanPath(server.hasArg("path") ? server.arg("path") : String("/"), path)) {
        fsReply(400, "bad path");
        return;
    }
    File dir = storageFs().open(path);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        fsReply(404, "no such folder");
        return;
    }

    gSendAborted = false;
    server.sendHeader("Cache-Control", "no-store");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");

    String out;
    out.reserve(1200);
    out = "{\"ok\":true,\"fs\":\"";
    appendJsonEscaped(out, storageName());
    out += "\",\"total\":";
    out += String((unsigned long long)storageTotalBytes());
    out += ",\"path\":\"";
    appendJsonEscaped(out, path.c_str());
    out += "\",\"entries\":[";

    static const int kMaxEntries = 2000;
    int count = 0;
    bool truncated = false;
    while (!gSendAborted) {
        File f = dir.openNextFile();
        if (!f) break;
        if (count >= kMaxEntries) { truncated = true; f.close(); break; }
        const String name = fsBaseName(f.name());
        const bool isDir = f.isDirectory();
        const size_t size = isDir ? 0 : f.size();
        f.close();
        if (count) out += ',';
        out += "{\"n\":\"";
        appendJsonEscaped(out, name.c_str());
        out += isDir ? "\",\"d\":1,\"s\":0}" : "\",\"d\":0,\"s\":";
        if (!isDir) { out += String((unsigned long)size); out += '}'; }
        count++;
        sendChunkIfBig(out, 1024);
        yield();
    }
    dir.close();
    out += "],\"truncated\":";
    out += truncated ? "true}" : "false}";
    sendChunk(out);
    server.sendContent("");
}

// GET /fs-get?path=/file
// Content-Length up front so the browser can show progress, then raw writes:
// writeAllRaw() bounds every stall, where streamFile() can sit in the core's
// own ten-second retry loop.
static void handleGetFsGet() {
    if (!fsBeginRequest()) return;
    String path;
    if (!fsCleanPath(server.arg("path"), path) || path == "/") {
        fsReply(400, "bad path");
        return;
    }
    File f = storageFs().open(path, FILE_READ);
    if (!f) { fsReply(404, "no such file"); return; }
    if (f.isDirectory()) { f.close(); fsReply(400, "is a folder"); return; }

    String name = fsBaseName(f.name());
    name.replace("\"", "_");
    String cd = "attachment; filename=\"" + name + "\"";
    server.sendHeader("Content-Disposition", cd);
    server.sendHeader("Cache-Control", "no-store");
    server.setContentLength(f.size());
    server.send(200, "application/octet-stream", "");

    for (;;) {
        const size_t n = f.read(fsIoBuf(), kFsIoBufLen);
        if (n == 0) break;
        if (!writeAllRaw((const char *)fsIoBuf(), n)) {
            Serial.printf("[web] files: download of %s abandoned\n", path.c_str());
            server.client().stop();
            break;
        }
        yield();
    }
    f.close();
}

// POST /fs-put?dir=/dir[&overwrite=1]  (multipart, one file)
// Written to "<name>.part" as it arrives and renamed into place only once it
// is complete, so a dropped upload or a full disk never leaves a truncated
// file under the real name, nor costs the one it was replacing.
static File        fsUpFile;
static String      fsUpPath;
static String      fsUpTemp;
static size_t      fsUpBytes = 0;
static bool        fsUpDone  = false;
static const char *fsUpError = nullptr;

static void fsUploadReset(bool removeTemp) {
    if (fsUpFile) fsUpFile.close();
    if (removeTemp && fsUpTemp.length()) storageFs().remove(fsUpTemp);
    fsUpPath  = "";
    fsUpTemp  = "";
    fsUpBytes = 0;
    fsUpDone  = false;
    fsUpError = nullptr;
}

static void fsUploadFail(const char *error) {
    if (fsUpFile) fsUpFile.close();
    if (fsUpTemp.length()) storageFs().remove(fsUpTemp);
    fsUpError = error;
}

static void handleFsPutUpload() {
    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        fsUploadReset(true);
        if (!filesAllowed()) { fsUpError = "unauthorized"; return; }
        if (!sdBegin()) { fsUpError = "no storage"; return; }
        String dir;
        if (!fsCleanPath(server.arg("dir"), dir) || !fsIsDir(dir)) {
            fsUpError = "no such folder";
            return;
        }
        // Browsers send the bare name; strip any path some client might add.
        String name = upload.filename;
        const int cut = max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
        if (cut >= 0) name = name.substring(cut + 1);
        String path;
        if (name.isEmpty() || !fsCleanPath(fsJoin(dir, name), path)
            || path.length() + 5 > kFsMaxPath) {
            fsUpError = "bad file name";
            return;
        }
        if (storageFs().exists(path)) {
            if (fsIsDir(path)) { fsUpError = "a folder has that name"; return; }
            if (server.arg("overwrite") != "1") { fsUpError = "exists"; return; }
        }
        fsUpPath = path;
        fsUpTemp = path + ".part";
        if (storageFs().exists(fsUpTemp)) storageFs().remove(fsUpTemp);
        fsUpFile = storageFs().open(fsUpTemp, FILE_WRITE);
        if (!fsUpFile) { fsUploadFail("cannot create file"); return; }
        return;
    }
    if (upload.status == UPLOAD_FILE_WRITE) {
        if (!fsUpFile) return;              // failed earlier: keep draining
        if (fsUpFile.write(upload.buf, upload.currentSize) != upload.currentSize) {
            fsUploadFail("write failed (storage full?)");
            return;
        }
        fsUpBytes += upload.currentSize;
        return;
    }
    if (upload.status == UPLOAD_FILE_END) {
        if (!fsUpFile) return;
        fsUpFile.close();
        fsUpDone = true;
        return;
    }
    // UPLOAD_FILE_ABORTED
    fsUploadFail("upload aborted");
}

static void handleFsPutDone() {
    if (!filesAllowed()) { fsUploadReset(true); fsReply(403, "unauthorized"); return; }
    if (fsUpError) {
        const char *err = fsUpError;
        const int code = !strcmp(err, "exists") ? 409 : 400;
        fsUploadReset(true);
        fsReply(code, err);
        return;
    }
    if (!fsUpDone) { fsUploadReset(true); fsReply(400, "no file received"); return; }
    if (storageFs().exists(fsUpPath)) storageFs().remove(fsUpPath);
    if (!storageFs().rename(fsUpTemp, fsUpPath)) {
        fsUploadReset(true);
        fsReply(500, "rename failed");
        return;
    }
    Serial.printf("[web] files: uploaded %s (%u bytes)\n",
                  fsUpPath.c_str(), (unsigned)fsUpBytes);
    fsUpTemp = "";                          // renamed: nothing left to clean up
    fsUploadReset(false);
    fsReplyOk();
}

// Removes a folder and everything under it, holding one directory handle at a
// time. Recursing with a handle open per level would run LittleFS out of its
// five open files three folders down. Instead: read a small batch of names,
// close the folder, act on the batch, and descend into the first subfolder
// found; an emptied folder is removed and the walk climbs back to its parent.
static bool fsRemoveTree(const String &root) {
    fs::FS &fs = storageFs();
    String cur = root;
    static const int kBatch = 16;
    for (uint32_t guard = 0; guard < 200000; guard++) {
        File dir = fs.open(cur);
        if (!dir) {
            // LittleFS can drop a folder once its last file goes. Gone is the
            // outcome wanted either way.
            if (fs.exists(cur)) return false;
            if (cur == root) return true;
            cur = fsParent(cur);
            continue;
        }
        if (!dir.isDirectory()) { dir.close(); return fs.remove(cur); }

        String names[kBatch];
        bool   isDir[kBatch];
        int n = 0;
        while (n < kBatch) {
            File f = dir.openNextFile();
            if (!f) break;
            names[n] = fsJoin(cur, fsBaseName(f.name()));
            isDir[n] = f.isDirectory();
            f.close();
            n++;
        }
        dir.close();

        if (n == 0) {
            if (!fs.rmdir(cur) && fs.exists(cur)) return false;
            if (cur == root) return true;
            cur = fsParent(cur);
            continue;
        }
        String descend;
        for (int i = 0; i < n; i++) {
            if (isDir[i]) {
                if (descend.isEmpty()) descend = names[i];
            } else if (!fs.remove(names[i])) {
                return false;
            }
        }
        if (descend.length()) cur = descend;
        yield();
    }
    return false;
}

// POST /fs-delete?path=/file-or-folder  (a folder goes with everything in it)
static void handlePostFsDelete() {
    if (!fsBeginRequest()) return;
    String path;
    if (!fsCleanPath(server.arg("path"), path) || path == "/") {
        fsReply(400, "bad path");
        return;
    }
    if (!storageFs().exists(path)) { fsReply(404, "not found"); return; }
    const bool ok = fsIsDir(path) ? fsRemoveTree(path) : storageFs().remove(path);
    if (!ok) { fsReply(500, "delete failed"); return; }
    Serial.printf("[web] files: deleted %s\n", path.c_str());
    fsReplyOk();
}

// POST /fs-mkdir?path=/parent/new
static void handlePostFsMkdir() {
    if (!fsBeginRequest()) return;
    String path;
    if (!fsCleanPath(server.arg("path"), path) || path == "/") {
        fsReply(400, "bad path");
        return;
    }
    if (storageFs().exists(path)) { fsReply(409, "exists"); return; }
    if (!fsIsDir(fsParent(path))) { fsReply(404, "no such folder"); return; }
    if (!storageFs().mkdir(path) || !fsIsDir(path)) {
        fsReply(500, "could not create folder");
        return;
    }
    fsReplyOk();
}

// POST /fs-copy?from=/file&to=/other/path[&overwrite=1]
// Files only. Same ".part" then rename as an upload, for the same reasons.
static void handlePostFsCopy() {
    if (!fsBeginRequest()) return;
    String from, to;
    if (!fsCleanPath(server.arg("from"), from) || !fsCleanPath(server.arg("to"), to)
        || from == "/" || to == "/" || to.length() + 5 > kFsMaxPath) {
        fsReply(400, "bad path");
        return;
    }
    if (from == to) { fsReply(400, "source and destination are the same"); return; }
    if (!storageFs().exists(from)) { fsReply(404, "no such file"); return; }
    if (fsIsDir(from)) { fsReply(400, "only files can be copied"); return; }
    if (storageFs().exists(to)) {
        if (fsIsDir(to)) { fsReply(409, "a folder has that name"); return; }
        if (server.arg("overwrite") != "1") { fsReply(409, "exists"); return; }
    }
    if (!fsIsDir(fsParent(to))) { fsReply(404, "no such folder"); return; }

    const String temp = to + ".part";
    if (storageFs().exists(temp)) storageFs().remove(temp);
    File src = storageFs().open(from, FILE_READ);
    File dst = storageFs().open(temp, FILE_WRITE);
    if (!src || !dst) {
        if (src) src.close();
        if (dst) dst.close();
        storageFs().remove(temp);
        fsReply(500, "cannot open file");
        return;
    }
    bool ok = true;
    for (;;) {
        const size_t n = src.read(fsIoBuf(), kFsIoBufLen);
        if (n == 0) break;
        if (dst.write(fsIoBuf(), n) != n) { ok = false; break; }
        yield();
    }
    src.close();
    dst.close();
    if (!ok) {
        storageFs().remove(temp);
        fsReply(500, "write failed (storage full?)");
        return;
    }
    if (storageFs().exists(to)) storageFs().remove(to);
    if (!storageFs().rename(temp, to)) {
        storageFs().remove(temp);
        fsReply(500, "rename failed");
        return;
    }
    Serial.printf("[web] files: copied %s -> %s\n", from.c_str(), to.c_str());
    fsReplyOk();
}
#endif  // HAS_WEB_FILES

// ── Public API ────────────────────────────────────────────────

