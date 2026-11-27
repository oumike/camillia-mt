// SPDX-License-Identifier: GPL-3.0-or-later
// Part of web_config.cpp: the main config page.
// Included in order from web_config.cpp as one translation unit; not built on its own.

static void sendConfigPage(const char *msg = "", bool lite = false) {
    if (!gCfg) { server.send(500, "text/plain", "No config"); return; }
    logWifiHeapDiag(lite ? "serving config page (lite)" : "serving config page");

    // Use chunked transfer to avoid building one giant String
    gSendAborted = false;
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");

    // Bounds the socket's SO_RCVTIMEO/SO_SNDTIMEO. Note this does NOT bound how
    // long a chunk write can block: WiFiClient::write() runs its own select()
    // retry loop with a hardcoded 1 s timeout and ignores this value. It only
    // helps the read side.
    // Same guard as sendSliced(): a setsockopt on a closed socket is an error
    // line, not a timeout.
    if (server.client().connected()) server.client().setTimeout(3);

    char tmp[96];
    // Stream the head straight from flash rather than seeding the String with
    // it: even kLiteHead is ~1 KB, and a contiguous
    // allocation that size is exactly what a fragmented heap can't provide.
    if (lite) sendFlash(kLiteHead); else sendFullHead();
    if (gSendAborted) {
        server.client().stop();
        logWifiHeapDiag(lite ? "lite page abandoned" : "config page abandoned");
        return;
    }
    String html;
#if HAS_UI_THEMES
    uint8_t themePreset = themePresetFromConfig(*gCfg);
#endif
    // Zero in lite mode: the node loops below accumulate tens of KB of options
    // and detail panels, which AP mode has no room for and lite never shows.
    int totalNodes = lite ? 0 : Nodes.count();
    uint8_t battPct = readBatteryPctWeb();
    const char *battCls = (battPct >= 60) ? "metric-good" : ((battPct >= 25) ? "metric-warn" : "metric-bad");
    bool gpsEn = gpsIsEnabled();
    bool gpsFix = gpsHasFix();
    bool gpsNmea = gpsHasNmeaStream();
    uint8_t gpsSat = gpsSats();
    const char *gpsCls = !gpsEn ? "metric-bad" : (gpsFix ? "metric-good" : (gpsNmea ? "metric-warn" : "metric-bad"));
    char battChip[24];
    // battCls above stays percentage-derived in both modes: the chip's color is
    // a state-of-charge signal, same as the on-device header dot.
    if (gCfg && gCfg->battDisplayMode == BATT_DISPLAY_VOLTAGE) {
        snprintf(battChip, sizeof(battChip), "BAT %.2fV", (double)batteryReadVoltage());
    } else {
        snprintf(battChip, sizeof(battChip), "BAT %u%%", (unsigned)battPct);
    }
    // Our own public key, base64, for the header chip. It is the first thing
    // remote administration needs -- it goes in the other node's
    // security.admin_key -- and until now it existed only inside the YAML
    // export, which is a strange place to have to go to read your own identity.
    char pubKeyB64[48] = "";
    bool havePubKey = false;
    for (int i = 0; i < 32 && !havePubKey; i++) havePubKey = (myPubKey[i] != 0);
    if (havePubKey) base64Encode(myPubKey, 32, pubKeyB64);

    char gpsChip[48];
    if (!gpsEn) snprintf(gpsChip, sizeof(gpsChip), "GPS OFF");
    else if (gpsFix) snprintf(gpsChip, sizeof(gpsChip), "GPS FIX %u", (unsigned)gpsSat);
    else if (!gpsNmea) snprintf(gpsChip, sizeof(gpsChip), "GPS NO DATA");
    else snprintf(gpsChip, sizeof(gpsChip), "GPS SEARCH %u", (unsigned)gpsSat);
    int mapPointCount = 0;
    uint32_t nowMs = millis();
    auto unzz = [](uint32_t v) -> int32_t {
        return (int32_t)((v >> 1) ^ (uint32_t)-(int32_t)(v & 1));
    };
    auto coordInRange = [](float lat, float lon) -> bool {
        return !(lat < -90.0f || lat > 90.0f || lon < -180.0f || lon > 180.0f);
    };
    float mapMeLat = gCfg->latI * 1e-7f;
    float mapMeLon = gCfg->lonI * 1e-7f;
    bool mapHasMe = coordInRange(mapMeLat, mapMeLon);
    auto extractNodeCoords = [&](const NodeEntry *n, float &lat, float &lon) -> bool {
        if (!n) return false;
        bool hasCoords = (n->latI != 0 || n->lonI != 0);
        if (!n->hasPosition && !hasCoords) return false;
        lat = n->latI * 1e-7f;
        lon = n->lonI * 1e-7f;
        if (coordInRange(lat, lon)) return true;
        // Backward compatibility: older firmware decoded signed lat/lon without zigzag.
        int32_t latRecovered = unzz((uint32_t)n->latI);
        int32_t lonRecovered = unzz((uint32_t)n->lonI);
        float latRec = latRecovered * 1e-7f;
        float lonRec = lonRecovered * 1e-7f;
        if (!coordInRange(latRec, lonRec)) return false;
        lat = latRec;
        lon = lonRec;
        return true;
    };
    const bool useImperialUnits = (gCfg && gCfg->displayUnits != 0);
    // Only the map-point count is needed up front (for the header); the
    // option/detail/point lists are streamed at the emit points below.
    for (int i = 0; i < totalNodes; i++) {
        NodeEntry *n = Nodes.getByRank(i);
        if (!n) continue;
        float lat = 0.0f, lon = 0.0f;
        if (extractNodeCoords(n, lat, lon)) mapPointCount++;
    }

    // Stream the "Nodes Seen" <option> list directly to the client, batching
    // into ~1 KB chunks so no large contiguous String is ever allocated.
    //
    // This used to be the no-PSRAM path only; boards with PSRAM accumulated the
    // whole list into Strings and appended them to the page at the end. That
    // does not survive a full node table: at 250 nodes the detail panels alone
    // come to ~114 KB, and `html += nodeDetails` then asks for a ~126 KB
    // contiguous String. The grow does not produce usable memory at that size —
    // the String reported length 126863 with only 12791 real bytes and the rest
    // reading as zeros, which went out as ~30 KB of NUL bytes and a page the
    // browser rendered as garbage. Streaming has no such ceiling, so every
    // board now uses it and the accumulate path is gone.
    auto streamNodeOptions = [&]() {
        String buf;
        // Rank order already puts favorites first (see cmpNodes in node_db.cpp),
        // so the boundary is simply the first non-favorite after at least one
        // favorite — no second pass, and nothing to keep in sync if the sort
        // keys change. A disabled <option> is the one separator every browser
        // renders the same way; <option> borders are not stylable.
        bool sawFavorite = false;
        bool sawDivider  = false;
        for (int i = 0; i < totalNodes; i++) {
            if (gSendAborted) break;
            NodeEntry *n = Nodes.getByRank(i);
            if (!n) continue;
            if (n->favorite) {
                sawFavorite = true;
            } else if (sawFavorite && !sawDivider) {
                buf += "<option disabled data-fav-divider='1'>"
                       "&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;"
                       "&mdash;&mdash;&mdash;&mdash;</option>";
                sawDivider = true;
            }
            const char *shortName = n->shortName[0] ? n->shortName : "----";
            const char *longName  = n->longName[0]  ? n->longName  : "(unnamed)";
            char optId[16];
            snprintf(optId, sizeof(optId), "!%08X", n->nodeId);
            // The value stays the rank index (it is what selectNode() turns into
            // an nd-<rank> lookup); the id and flag are what lets the favorite
            // toggle re-sort this list in place without touching the values.
            buf += "<option value='";
            buf += String(i);
            buf += "' data-nid='";
            buf += optId;
            buf += "' data-fav='";
            buf += n->favorite ? "1" : "0";
            // For the dropdown's filters: a NodeInfo name heard, and a
            // position the map can place.
            buf += "' data-known='";
            buf += n->hasName ? "1" : "0";
            {
                float lat = 0.0f, lon = 0.0f;
                buf += "' data-loc='";
                buf += extractNodeCoords(n, lat, lon) ? "1" : "0";
            }
            buf += "'>";
            appendAttr(buf, shortName);
            buf += " -- ";
            appendAttr(buf, longName);
            buf += "</option>";
            if (buf.length() > 1024) sendChunk(buf);
        }
        if (buf.length()) sendChunk(buf);
    };

    // Stream the JSON array of map points ([{lat,lon},...]) in ~1 KB chunks.
    auto streamMapPoints = [&]() {
        String buf = "[";
        int cnt = 0;
        char t[64];
        for (int i = 0; i < totalNodes; i++) {
            if (gSendAborted) break;
            NodeEntry *n = Nodes.getByRank(i);
            if (!n) continue;
            float lat = 0.0f, lon = 0.0f;
            if (!extractNodeCoords(n, lat, lon)) continue;
            if (cnt > 0) buf += ",";
            snprintf(t, sizeof(t), "{\"lat\":%.7f,\"lon\":%.7f}", lat, lon);
            buf += t;
            cnt++;
            if (buf.length() > 1024) sendChunk(buf);
        }
        buf += "]";
        sendChunk(buf);
    };

    // Stream the per-node detail panels in ~1 KB chunks. Mirrors the PSRAM
    // accumulation path below; keep the two in sync if you change the markup.
    auto streamNodeDetails = [&]() {
        String buf;
        char tmp[96];
        for (int i = 0; i < totalNodes; i++) {
            if (gSendAborted) break;
            NodeEntry *n = Nodes.getByRank(i);
            if (!n) continue;

            float lat = 0.0f, lon = 0.0f;
            bool hasLocation = extractNodeCoords(n, lat, lon);

            char idBuf[16];
            snprintf(idBuf, sizeof(idBuf), "!%08X", n->nodeId);
            const char *shortName = n->shortName[0] ? n->shortName : "----";
            const char *longName  = n->longName[0]  ? n->longName  : "(unnamed)";
            const char *chanName = "-";
            if (n->chanIdx >= 0 && n->chanIdx < MAX_CHANNELS) {
                const ChannelKey &ck = CHANNEL_KEYS[n->chanIdx];
                const char *nm = ck.name_buf[0] ? ck.name_buf : ck.name;
                if (nm && nm[0]) chanName = nm;
            }

            char heardBuf[40];
            if (n->lastHeardMs == 0 || nowMs < n->lastHeardMs) {
                snprintf(heardBuf, sizeof(heardBuf), "unknown");
            } else {
                snprintf(heardBuf, sizeof(heardBuf), "%lus ago",
                         (unsigned long)((nowMs - n->lastHeardMs) / 1000UL));
            }

            char locBuf[96];
            if (hasLocation) {
                snprintf(locBuf, sizeof(locBuf), "%.6f, %.6f (alt %dm)", lat, lon, (int)n->alt);
            } else {
                snprintf(locBuf, sizeof(locBuf), "unknown");
            }

            char posSeenBuf[40];
            if (n->lastPosMs == 0 || nowMs < n->lastPosMs) {
                snprintf(posSeenBuf, sizeof(posSeenBuf), "never");
            } else {
                snprintf(posSeenBuf, sizeof(posSeenBuf), "%lus ago",
                         (unsigned long)((nowMs - n->lastPosMs) / 1000UL));
            }

            char posStateBuf[48];
            if (n->lastPosMs == 0) {
                snprintf(posStateBuf, sizeof(posStateBuf), "no POSITION packets");
            } else if (n->hasPosition) {
                snprintf(posStateBuf, sizeof(posStateBuf), "position valid");
            } else {
                snprintf(posStateBuf, sizeof(posStateBuf), "packet seen, no fix");
            }

            char rawPosBuf[64];
            snprintf(rawPosBuf, sizeof(rawPosBuf), "%ld, %ld", (long)n->latI, (long)n->lonI);

            char telemBuf[180];
            if (n->hasTelemetry) {
                telemBuf[0] = '\0';
                if (n->hasDeviceTelemetry) {
                    snprintf(telemBuf + strlen(telemBuf), sizeof(telemBuf) - strlen(telemBuf),
                             "%.0f%% / %.2fV", n->battPct, n->voltage);
                }
                if (n->hasEnvironmentTelemetry) {
                    if (telemBuf[0]) {
                        snprintf(telemBuf + strlen(telemBuf), sizeof(telemBuf) - strlen(telemBuf), " | ");
                    }
                    if (useImperialUnits) {
                        float tempF = n->temperatureC * (9.0f / 5.0f) + 32.0f;
                        float pressureInHg = n->pressureHpa * 0.0295299831f;
                        snprintf(telemBuf + strlen(telemBuf), sizeof(telemBuf) - strlen(telemBuf),
                                 "%.1fF %.1f%% %.2finHg",
                                 (double)tempF,
                                 (double)n->humidityPct,
                                 (double)pressureInHg);
                    } else {
                        snprintf(telemBuf + strlen(telemBuf), sizeof(telemBuf) - strlen(telemBuf),
                                 "%.1fC %.1f%% %.1fhPa",
                                 (double)n->temperatureC,
                                 (double)n->humidityPct,
                                 (double)n->pressureHpa);
                    }
                }
                if (!telemBuf[0]) {
                    snprintf(telemBuf, sizeof(telemBuf), "none");
                }
            } else {
                snprintf(telemBuf, sizeof(telemBuf), "none");
            }

            char linkBuf[72];
            if (n->lastHeardMs != 0) {
                snprintf(linkBuf, sizeof(linkBuf), "SNR %.1f dB, hops %u", n->snr, (unsigned)n->hops);
            } else {
                snprintf(linkBuf, sizeof(linkBuf), "unknown");
            }

            char latAttr[24], lonAttr[24];
            snprintf(latAttr, sizeof(latAttr), "%.7f", lat);
            snprintf(lonAttr, sizeof(lonAttr), "%.7f", lon);
            buf += "<div class='node-detail' id='nd-";
            buf += String(i);
            buf += "' data-loc='";
            buf += hasLocation ? "1" : "0";
            buf += "' data-lat='";
            buf += latAttr;
            buf += "' data-lon='";
            buf += lonAttr;
            buf += "'>";
            buf += "<div class='node-card'>";
            buf += "<div class='node-head'>";
            buf += "<div class='node-title'>";
            appendAttr(buf, shortName);
            buf += " -- ";
            appendAttr(buf, longName);
            // Says why this node sorted to the top of the list, and — more to
            // the point — that it is exempt from eviction and from the
            // keep-favorites clear.
            //
            // Always emitted, shown or hidden by style: the toggle below flips
            // this without a page rebuild, and toggling a display is a great
            // deal less fragile than inserting an element into a title whose
            // text came from another node's name.
            buf += "<span class='fav-tag' data-fav-tag='";
            buf += idBuf;
            buf += "'";
            if (!n->favorite) buf += " style='display:none'";
            buf += ">(favorite)</span>";
            buf += "</div>";   // .node-title
            buf += "<button type='button' class='chat-mini fav-btn' data-fav-btn='";
            buf += idBuf;
            buf += "' data-fav='";
            buf += n->favorite ? "1" : "0";
            buf += "' onclick='toggleNodeFavorite(this)'>";
            buf += n->favorite ? "Unfavorite" : "Favorite";
            buf += "</button>";
#if HAS_ADMIN_TERMINAL
            // Beside Favorite, for any listed peer the remote has not refused --
            // the same mayOpenTerminal() the device's actions modal and the
            // peers table ask, decided once, server side.
            //
            // Not built at all for anything else, rather than rendered disabled:
            // a greyed "Admin" on a stranger's node is an invitation to try, and
            // trying means an unauthorized admin packet across the mesh.
            if (AdminPeerList.mayOpenTerminal(n->nodeId)) {
                buf += "<button type='button' class='chat-mini' onclick=\"adminOpen('";
                buf += idBuf;
                buf += "')\">Admin</button>";
            }
#endif
            buf += "</div>";   // .node-head
            buf += "<div class='node-meta'><b>ID:</b> ";
            buf += idBuf;
            buf += "  <b>Channel:</b> ";
            appendAttr(buf, chanName);
            buf += "  <b>Last Heard:</b> ";
            buf += heardBuf;
            buf += "</div>";
            buf += "<div class='node-meta'><b>Location:</b> ";
            buf += locBuf;
            buf += "</div>";
            buf += "<div class='node-meta'><b>Position Pkt:</b> ";
            buf += posSeenBuf;
            buf += "  <b>State:</b> ";
            buf += posStateBuf;
            buf += "  <b>Raw:</b> ";
            buf += rawPosBuf;
            buf += "</div>";
            buf += "<div class='node-meta'><b>Telemetry:</b> ";
            buf += telemBuf;
            buf += "  <b>Link:</b> ";
            buf += linkBuf;
            buf += "</div>";
            buf += "</div>";   // .node-card
            buf += "</div>";   // .node-detail
            if (buf.length() > 1024) sendChunk(buf);
        }
        if (buf.length()) sendChunk(buf);
    };
    // Everything above this point is the node list: on the full page it walks
    // every known node and builds the dropdown and detail panels in RAM before
    // a single byte goes out. If a render stalls between "serving config page"
    // and here, it is that build, not the socket.
    if (!lite) logWifiHeapDiag("node list built");

    // This board freed its chat buffers so Wi-Fi could start, so the device is
    // dropping messages for as long as this page is being served. Say so
    // prominently — it is not recoverable by anything on this page.
    static const char kChatPaused[] =
        "Chat is paused. This device needed its message memory to run "
        "Wi-Fi, so messages sent to it are not being received or stored "
        "while web config is on. Turn web config off on the device to "
        "resume messaging.";

    if (lite) {
        html += "<h2>Camillia &mdash; Web Config Lite";
        if (gCfg && gCfg->webCfgAuthEnabled)
            html += " <a class='logout' href='/logout'>Logout</a>";
        html += "</h2>";
        if (msg[0]) { html += "<p class='msg'>"; appendAttr(html, msg); html += "</p>"; }
        if (gWebBuffersReclaimed) {
            html += "<p style='color:#d81f2a;font-weight:700;border:1px solid #d81f2a;"
                    "border-radius:4px;padding:.5em;margin:.5em 0'>";
            html += kChatPaused;
            html += "</p>";
        }
        html += "<p>Access-point mode. All settings are here; the live feed, "
                "chat and node map need the device on your WiFi network. "
                "<a href='/setup'>Quick WiFi setup</a></p>";
    } else {
        // Header: who this is and how it is doing. The node's short name in the
        // brand face, its long name, then battery / GPS / key chips.
        html += "<header class='app-hd'><span class='nm'>";
        appendAttr(html, gCfg ? gCfg->nodeShort : "");
        html += "</span><span class='ln'>";
        appendAttr(html, gCfg ? gCfg->nodeLong : "");
        html += "</span><span class='chips'>";
        if (havePubKey) {
            html += "<button type='button' class='chip key-chip' id='pubkey-chip'"
                    " data-key='";
            html += pubKeyB64;
            html += "' onclick='copyPubKey(this)' title='Copy the public key of this node'>"
                    "KEY ";
            // Enough of it to recognise, not so much that it crowds the header.
            // The whole key travels in data-key; this is only the label.
            html += String(pubKeyB64).substring(0, 10);
            html += "&hellip;</button>";
        }
        html += "<span class='chip ";
        html += battCls;
        html += "'>";
        html += battChip;
        html += "</span><span class='chip ";
        html += gpsCls;
        html += "'>";
        html += gpsChip;
        html += "</span>";
        if (gCfg && gCfg->webCfgAuthEnabled)
            html += "<a class='logout' href='/logout'>Logout</a>";
        html += "</span></header>";

        // Wide screens: the rail. Config's section links are filled in by
        // appBuildSectionNav() (kAppJs) from the sections on the page, so a
        // section a board leaves out never leaves a dead link behind.
        html += "<div class='app'><nav class='app-rail' aria-label='Pages'>"
                "<a data-tab='config' href='#config' class='active' aria-current='page'>Config</a>"
                "<div id='rail-secs'></div>"
                "<a data-tab='live' href='#live'>Live</a>"
#if !defined(DEVICE_CARDPUTER_LORA_HAT)
                "<a data-tab='chat' href='#chat'>Chat</a>"
#endif
                "<a data-tab='map' href='#map'>Nodes</a>"
                "<a data-tab='utils' href='#utils'>Utilities</a>";
#if HAS_VNC_HOST
        html += "<a data-tab='remote' href='#remote'>Remote</a>";
#endif
#if HAS_WEB_FILES
        if (gCfg && cfgWebFilesEnabled(*gCfg)) html += "<a data-tab='files' href='#files'>Files</a>";
#endif
        html += "</nav><main class='app-main'>";
        if (msg[0]) { html += "<p class='notice'>"; appendAttr(html, msg); html += "</p>"; }
        // The full page on the device's own AP: the browser has no internet, so
        // say what that leaves out before someone wonders why the map is grey.
        if (gApMode) {
            html += "<p class='notice'>Connected to this device's own network. The node "
                    "map, map downloads and update checks need the device on your WiFi. "
                    "<a href='/setup'>Quick WiFi setup</a></p>";
        }
        if (gWebBuffersReclaimed) {
            html += "<p class='notice-err'>";
            html += kChatPaused;
            html += "</p>";
        }
        // Phones: the current section, and the sheet that jumps between them.
        html += "<div class='secbar'><b id='sec-current'>Config</b>"
                "<button type='button' class='btn-2' onclick=\"appOpenSheet('sheet-secs')\">"
                "Sections &#9662;</button></div>";
        html += "<div class='tab-panel active' id='tab-config'><div class='tab-pane-center'>";
    }
    sendChunk(html);

    // Saving reboots to apply (handlePostSave), so it asks first, like every
    // other restarting action on this page. The one exception it cannot see
    // from here is a font-size-only change, which applies without restarting --
    // the wording hedges rather than promising a reboot that may not happen.
    html += "<form id='cfg-form' method='POST' action='/save'"
#if FEATURE_LICENSED_MODE
            // licOk() (Licensed mode section) asks the generic question too,
            // or the licensed-mode one in its place when the box was flipped.
            " onsubmit=\"return licOk()\">";
#else
            " onsubmit=\"return confirm('Saving may restart the device to apply "
            "these changes. Continue?')\">";
#endif

    // ── Node Identity ─────────────────────────────────────────
    section(html, lite, "Node Identity", "identity", "How this node appears to others on the mesh.");
    // maxlength counts UTF-16 code units, not bytes, so it is a UI hint only —
    // the byte cap is enforced by copyTruncate in the POST handler.
    html += "<label>Long Name (max 24 bytes)"
            "<input name='long' type='text' maxlength='24' value='";
    appendAttr(html, gCfg->nodeLong); html += "'";
#if FEATURE_LICENSED_MODE
    // Composed from call sign and suffix below while licensed.
    if (gCfg->licensedMode) html += " readonly";
#endif
    html += "></label>";
    html += "<label>Short Name (max 4 chars)"
            "<input name='short' type='text' maxlength='4' value='";
    appendAttr(html, gCfg->nodeShort); html += "'></label>";
    // Read-only and fixed at build time: shown so what NODEINFO advertises can
    // be checked without a second node (issue #163).
    snprintf(tmp, sizeof(tmp), "%s (%u)", MY_HW_MODEL_NAME, (unsigned)MY_HW_MODEL);
    html += "<p class='hint'>Hardware model sent to other nodes: <b>";
    html += tmp;
#if MY_HW_MODEL == MESH_HW_MODEL_PRIVATE_HW
    html += "</b>. Meshtastic has no ID for this board yet, so other nodes show "
            "it as unknown hardware.</p>";
#else
    html += "</b></p>";
#endif
    // The two ways to change this node's id: derive it from the public key
    // (Meshtastic 2.8's way), or fix it outright. Both are a change of identity,
    // so each says so rather than leaving the consequence to be discovered.
    //
    // On lite too: it is the only page the Cardputer serves, and these two are
    // settings it has no other way to reach. Both are small; the save handler
    // already applies each only when its control was on the page.
    {
        // Marker so the POST handler can apply this checkbox (an unchecked box
        // submits nothing) only when the control was actually on the page.
        html += "<input type='hidden' name='nodeid_present' value='1'>";
        html += "<label>"
                "<input type='checkbox' name='nodeid_pubkey' value='1'";
        if (gCfg->nodeIdFromPubKey) html += " checked";
        html += "> Derive node ID from public key (Meshtastic 2.8 style)</label>";
        html += "<p class='hint'>Changes this node's address on the mesh. Other "
                "nodes will see a new node and keep the old one until it ages "
                "out, and direct messages people already have with you stay "
                "attached to the old address. Your own chat history is carried "
                "across. Reboot to apply.</p>";

        // Blank is off: the id is derived as above. Wins over the key option.
        html += "<label>Node ID Override<input name='node_id_ovr' type='text' maxlength='11' "
                "placeholder='blank = off' value='";
        if (gCfg->nodeIdOverride) {
            snprintf(tmp, sizeof(tmp), "!%08lx", (unsigned long)gCfg->nodeIdOverride);
            html += tmp;
        }
        html += "'></label>";
        html += "<p class='hint' style='color:var(--warn,#a15c00)'><b>Warning:</b> this replaces "
                "this node's ID (e.g. <code>!1a2b3c4d</code>), overriding both the "
                "default and the public-key option above. Every other node will see a "
                "new node, and existing DMs and favorites stay with the old ID. Two "
                "nodes with the same ID disrupt each other on the mesh. Leave blank to "
                "turn it off. Reboot to apply.</p>";

        // Values are Meshtastic 2.8.1's PacketSignaturePolicy, in its order.
        html += "<label>Signed packets<select name='sig_policy'>";
        for (int i = 0; i < SIG_POLICY_COUNT; i++) {
            snprintf(tmp, sizeof(tmp), "%d", i);
            html += "<option value='"; html += tmp; html += "'";
            if (sigPolicyCoerce(gCfg->packetSigPolicy) == i) html += " selected";
            html += ">"; html += sigPolicyName((uint8_t)i); html += "</option>";
        }
        html += "</select></label>";
        html += "<p class='hint'>Which received packets to accept, by their "
                "signature, as Meshtastic 2.8.1 decides it. <b>Compatible</b> "
                "(the default) accepts unsigned packets, so it hears every 2.7 "
                "node. <b>Balanced</b> also drops an unsigned broadcast from a "
                "node that has signed before. <b>Strict</b> accepts only signed "
                "or PKI-encrypted packets, and ignores every 2.7 node and every "
                "node not yet heard signing. A signature that fails to verify is "
                "dropped under all three.</p>";
    }
    sectionEnd(html, lite);
    sendChunk(html);

#if FEATURE_LICENSED_MODE
    // ── Licensed (ham) mode ───────────────────────────────────
    // Same transaction as the device's Licensed mode screen: enabling, disabling
    // or changing the call sign saves and reboots (handlePostSave defers it).
    section(html, lite, "Licensed (Ham) Mode", "licensed",
            "Amateur radio operation under your own call sign.");
    {
        const bool licOn = gCfg->licensedMode;
        // The suffix is whatever follows "CALL//" in the long name, as the
        // device screen stages it.
        char suffix[sizeof(gCfg->nodeLong)] = "";
        const size_t callLen = strlen(gCfg->callSign);
        if (callLen && strncmp(gCfg->nodeLong, gCfg->callSign, callLen) == 0
            && gCfg->nodeLong[callLen] == '/' && gCfg->nodeLong[callLen + 1] == '/') {
            utf8util::copyTruncate(suffix, sizeof(suffix), gCfg->nodeLong + callLen + 2);
        }
        html += "<input type='hidden' name='lic_present' value='1'>";
        html += "<label><input type='checkbox' id='lic-en' name='lic_en' value='1'";
        if (licOn) html += " checked";
        html += "> Licensed mode</label>";
        html += "<div class='row2'><label>Call Sign (max 15)"
                "<input name='lic_call' type='text' maxlength='15' value='";
        appendAttr(html, gCfg->callSign);
        html += "'></label><label>Long Name Suffix"
                "<input name='lic_suffix' type='text' maxlength='21' value='";
        appendAttr(html, suffix);
        html += "'></label></div>";
        snprintf(tmp, sizeof(tmp), "%d", constrain((int)gCfg->loraPower, 1, (int)MESH_POWER));
        html += "<label>Licensed TX Power (dBm, 1&ndash;22)"
                "<input name='lic_pwr' type='number' min='1' max='22' value='";
        html += tmp; html += "'></label>";
        if (gCfg->licTxHold) {
            html += "<label><input type='checkbox' name='lic_resume' value='1'>"
                    " Resume transmitting</label>"
                    "<p class='hint'>Transmit was stopped when the device left an "
                    "amateur band. Tick this and save to transmit again.</p>";
        }
        html += "<p class='hint'>The long name becomes CALL//suffix. While licensed, "
                "every channel key is removed and stays empty, encrypted DMs, remote "
                "admin and relaying for unlicensed nodes stop, and NodeInfo goes out "
                "every 10 min to identify the station. Turning this on or off, or "
                "changing the call sign, reboots the device. You must hold an amateur "
                "licence for the band you use. <b>Camillia does not verify your "
                "licence.</b></p>";
        // Texts match the device's Licensed mode confirmations.
        html += "<script>function licOk(){var c=document.getElementById('lic-en');"
                "if(c&&c.checked!=";
        html += licOn ? "true" : "false";
        html += ")return confirm(c.checked?'";
        appendJsStr(html, "Every channel's key will be removed. Encrypted DMs, remote admin "
                          "and relaying for unlicensed nodes will stop. The device will "
                          "reboot. You must hold an amateur licence for the band you use. "
                          "Camillia does not verify your licence.");
        html += "':'";
        appendJsStr(html, "Channel keys will not come back. The device will reboot.");
        const RegionPlan *rp = regionPlan(gCfg->region);
        if (rp && rp->licensedOnly) {
            appendJsStr(html, " Region will change to " MY_REGION
                              " and transmit stays off until you resume.");
        }
        html += "');"
                "return confirm('Saving may restart the device to apply these changes. Continue?');}"
                "</script>";
    }
    sectionEnd(html, lite);
    sendChunk(html);
#endif

    // ── Device ────────────────────────────────────────────────
    section(html, lite, "Device", "device", "Role, rebroadcasting and clock.");
    html += "<div class='row2'>";
    html += "<label>Role<select name='role'>";
    // See cfgCoerceDeviceRole for which roles this firmware offers and why the
    // infrastructure ones are absent; values are the canonical Meshtastic enum
    // positions (kept intact for wire compatibility and rebroadcast gating).
    static const struct { uint8_t v; const char *l; } kRoles[] = {
        {0,"CLIENT"},{1,"CLIENT_MUTE"},{5,"TRACKER"},{8,"CLIENT_HIDDEN"},{12,"CLIENT_BASE"}
    };
    for (int i = 0; i < (int)(sizeof(kRoles) / sizeof(kRoles[0])); i++) {
        // CLIENT_BASE is suggested only on base-station boards, but a node that
        // already has it (an import, the customizer) still shows it: leaving it
        // out would make the next Save quietly turn the node back into CLIENT.
        if (kRoles[i].v == 12 && !UI_OFFER_CLIENT_BASE && gCfg->deviceRole != 12) continue;
        snprintf(tmp, sizeof(tmp), "%d", kRoles[i].v);
        html += "<option value='"; html += tmp; html += "'";
        if (gCfg->deviceRole == kRoles[i].v) html += " selected";
        html += ">"; html += kRoles[i].l; html += "</option>";
    }
    html += "</select></label>";
    html += "<label>Rebroadcast<select name='rebroadcast'>";
    static const struct { uint8_t v; const char *l; } kRebroad[] = {
        {0,"ALL"},{1,"ALL_SKIP_DECODING"},{2,"LOCAL_ONLY"},{3,"KNOWN_ONLY"},{4,"CORE_PORTNUMS_ONLY"}
    };
    for (int i = 0; i < 5; i++) {
        snprintf(tmp, sizeof(tmp), "%d", kRebroad[i].v);
        html += "<option value='"; html += tmp; html += "'";
        if (gCfg->rebroadcastMode == kRebroad[i].v) html += " selected";
        html += ">"; html += kRebroad[i].l; html += "</option>";
    }
    html += "</select></label></div>";
    // What the role actually does here, said plainly, because on this firmware
    // it is mostly an advertised identity rather than a behaviour switch — and
    // someone picking TRACKER expecting upstream's power-saving duty cycle
    // should find that out on this page, not from a battery that never lasts.
    html += "<p class='hint'>The role goes out in this node's NodeInfo, so other "
            "nodes and the map see how it means to be treated. "
            "<b>CLIENT</b> is an ordinary node. <b>CLIENT_MUTE</b> does not relay "
            "other people's traffic. <b>CLIENT_HIDDEN</b> only speaks when spoken "
            "to. <b>CLIENT_BASE</b> is a powered base station (an attic or "
            "window node) for your own nodes: it always relays packets from or to "
            "the nodes you have <i>favorited</i>, after every ordinary relay has "
            "had its turn, and relays everything else like a CLIENT. On this "
            "role a favorite is a routing choice, not just a pin, so "
            "auto-favorite by distance is ignored. <b>TRACKER</b> is a node whose job is reporting where it is: it "
            "still relays like a CLIENT, and it needs <i>Share Location</i> on "
            "with a <i>GPS Broadcast Interval</i> short enough to be worth "
            "tracking (Meshtastic suggests 60&nbsp;s). It does <b>not</b> put the "
            "device to sleep between fixes the way upstream's tracker does with "
            "<code>is_power_saving</code> &mdash; this firmware has no such mode, "
            "so a tracker here costs the same battery as a client. (GPS Duty "
            "Cycle, under Position, sleeps the GPS receiver only.)</p>";
    sendChunkIfBig(html);   // AP mode renders this section with very little heap
    // GPS Broadcast Interval lives in the Position section, next to the poll
    // interval it is easily confused with.
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)gCfg->nodeInfoIntervalS);
#if FEATURE_LICENSED_MODE
    if (gCfg->licensedMode) {
        // Fixed at the station-ID interval; handlePostSave keeps the stored value.
        html += "<label>NodeInfo Interval<input type='text' value='10 min while licensed' disabled></label>";
    } else
#endif
    {
        html += "<label>NodeInfo Interval (s)<input name='nodeinfo_intv' type='number' min='60' value='";
        html += tmp; html += "'></label>";
    }
#if !HAS_SYSTEM_SETTINGS   // the OS's clock and zone
    // Timezone dropdown
    {
        bool tzMatched = false;
        for (int i = 0; i < kTzCount && !tzMatched; i++)
            if (strcmp(gCfg->tzDef, kTzOptions[i].posix) == 0) tzMatched = true;
        html += "<label>Timezone<select name='tzdef'>";
        if (!tzMatched && gCfg->tzDef[0]) {
            html += "<option value='"; appendAttr(html, gCfg->tzDef);
            html += "' selected>Custom: "; appendAttr(html, gCfg->tzDef); html += "</option>";
        }
        for (int i = 0; i < kTzCount; i++) {
            html += "<option value='"; html += kTzOptions[i].posix; html += "'";
            if (strcmp(gCfg->tzDef, kTzOptions[i].posix) == 0) html += " selected";
            html += ">"; html += kTzOptions[i].label; html += "</option>";
            sendChunkIfBig(html);
        }
        html += "</select></label>";
    }
    html += "<div class='row2'>";
    html += "<label>NTP Server<input name='ntp_server' type='text' maxlength='47' value='";
    appendAttr(html, gCfg->ntpServer);
    html += "' placeholder='pool.ntp.org'></label>";
    html += "</div>";
#endif

#if HAS_NODE_LOS
    // Elevation endpoint for Node Actions -> LOS. Plain http:// only, and the
    // help text says so rather than leaving the operator to discover it as a
    // connection failure: this firmware carries no TLS client, so an https://
    // address cannot be reached at all.
    html += "<div class='row2'>";
    html += "<label>Elevation Server (LOS)<input name='los_elev' type='text' maxlength='95' value='";
    appendAttr(html, gCfg->losElevServer);
    html += "' placeholder='http://host:port'></label>";
    html += "</div>";
    html += "<p class='hint'>Terrain line-of-sight fetches ground elevation from "
            "<code>&lt;server&gt;/elev?locations=lat,lon|...</code>, expecting a CSV of "
            "metres. Must be <b>http://</b> — there is no TLS client in this firmware. "
            "Leave empty to disable LOS.</p>";
#endif

#if HAS_WEATHER
    // Weather proxy for Tools -> Weather. Same shape and same constraint as the
    // elevation endpoint above, and a separate service — see docs/WEATHER.md.
    html += "<div class='row2'>";
    html += "<label>Weather Server<input name='weather_srv' type='text' maxlength='95' value='";
    appendAttr(html, gCfg->weatherServer);
    html += "' placeholder='http://host:port'></label>";
    html += "</div>";
    html += "<p class='hint'>The weather screen fetches current conditions from "
            "<code>&lt;server&gt;/weather?lat=&amp;lon=&amp;units=</code>. Must be "
            "<b>http://</b> — there is no TLS client in this firmware. The position is "
            "rounded to about a kilometre before it is sent. Leave empty to disable "
            "the weather screen.</p>";
#endif

    // ── Time and Date ─────────────────────────────────────────
    // Automatic means NTP when there's a network path and GPS otherwise. Manual
    // stops both from touching the clock and uses the fields below, which are
    // read as local time in the timezone selected above.
    {
#if !HAS_SYSTEM_SETTINGS
        const bool manual = (gCfg->timeSource == TIME_SOURCE_MANUAL);
#endif
        // How a time reads everywhere the device shows one — the chat header,
        // message timestamps, the sleep/lock clock. Exported files keep 24-hour
        // regardless: those are read by tools, not people.
        html += "<label>Clock Format<select name='clock_format'>"
                "<option value='0'";
        if (gCfg->clockFormat != CLOCK_FORMAT_12H) html += " selected";
        html += ">24-hour (14:32)</option>"
                "<option value='1'";
        if (gCfg->clockFormat == CLOCK_FORMAT_12H) html += " selected";
        html += ">12-hour (2:32 PM)</option>"
                "</select></label>";
#if !HAS_SYSTEM_SETTINGS   // the OS keeps the clock; nothing here sets it
        html += "<label>Time Source<select name='time_source' id='time_source'>";
        html += "<option value='0'";
        if (!manual) html += " selected";
        html += ">Automatic (Internet / GPS)</option>";
        html += "<option value='1'";
        if (manual) html += " selected";
        html += ">Manual</option></select></label>";

        // Seeded from the device clock so the fields open on something sane.
        char dbuf[16] = "";
        char tbuf[8] = "";
        time_t nowSec = time(nullptr);
        if (nowSec >= 1700000000) {
            struct tm lt;
            localtime_r(&nowSec, &lt);
            strftime(dbuf, sizeof(dbuf), "%Y-%m-%d", &lt);
            strftime(tbuf, sizeof(tbuf), "%H:%M", &lt);
        }

        html += "<div id='manual_row' class='row2'";
        if (!manual) html += " style='display:none'";
        html += ">";
        html += "<label>Date<input name='manual_date' type='date' value='";
        html += dbuf;
        html += "'></label>";
        html += "<label>Time (24h)<input name='manual_time' type='time' value='";
        html += tbuf;
        html += "'></label></div>";
        sendChunkIfBig(html);   // AP mode renders this with very little heap
        html += "<p class='hint' id='manual_hint'";
        if (!manual) html += " style='display:none'";
        html += ">Saving applies this time to the device clock. "
                "The clock is not battery-backed, so it restarts unset after a power cycle.</p>";
        html += "<p class='hint'><button type='button' id='browser_time'>Use this browser's time"
                "</button></p>";
        html += "<script>"
                "(function(){"
                "var s=document.getElementById('time_source');"
                "var m=document.getElementById('manual_row');"
                "var h=document.getElementById('manual_hint');"
                "function u(){var on=s.value=='1';m.style.display=on?'':'none';"
                "h.style.display=on?'':'none';}"
                "s.addEventListener('change',u);u();"
                "document.getElementById('browser_time').addEventListener('click',function(){"
                "var n=new Date(),p=function(v){return(v<10?'0':'')+v;};"
                "document.getElementsByName('manual_date')[0].value="
                "n.getFullYear()+'-'+p(n.getMonth()+1)+'-'+p(n.getDate());"
                "document.getElementsByName('manual_time')[0].value="
                "p(n.getHours())+':'+p(n.getMinutes());"
                "s.value='1';u();});"
                "})();"
                "</script>";
#endif
    }
    sectionEnd(html, lite);
    sendChunk(html);

    // ── Position ──────────────────────────────────────────────
    section(html, lite, "Position", "position", "GPS and what location this node shares.");
    html += "<label>"
            "<input type='checkbox' name='shareLocation' value='1'";
    if (gCfg->shareLocation) html += " checked";
    html += "> Share Location (broadcast position to the mesh)</label>";
    html += "<p class='hint'>Off suppresses every position transmission, including "
            "the manual announce, whatever the settings below say. Nothing else on this "
            "page stops the broadcast: GPS Enabled only chooses where the coordinates "
            "come from.</p>";
    html += "<label>Location Precision<select name='pos_precision'>";
    for (int i = 0; i < kPositionPrecisionCount; i++) {
        snprintf(tmp, sizeof(tmp), "%u", (unsigned)kPositionPrecisions[i].bits);
        html += "<option value='"; html += tmp; html += "'";
        if (gCfg->positionPrecision == kPositionPrecisions[i].bits) html += " selected";
        html += ">"; html += kPositionPrecisions[i].label; html += "</option>";
    }
    html += "</select></label>";
    html += "<p class='hint'>Rounds the coordinates before they are transmitted, so "
            "the mesh sees the area you are in rather than the spot you are standing on. "
            "The device still uses your exact position locally. Receiving nodes are told "
            "how coarse the value is, so other Meshtastic clients can show it as an area. "
            "Applies to every channel this node shares position on.</p>";
#if defined(DEVICE_CARDPUTER_ZERO)
    // The system runs the GPS here; only what is shared of it is ours.
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)gCfg->posIntervalS);
    html += "<label>GPS Broadcast Interval (s)<input name='pos_intv' type='number' min='60' value='";
    html += tmp; html += "'></label>";
    html += "<p class='hint'>How often your position is sent to the mesh. The "
            "manual coordinates below are used until the GPS has a fix.</p>";
#else
    html += "<label>"
            "<input type='checkbox' name='gpsEnabled' value='1'";
    if (gCfg->gpsEnabled) html += " checked";
    html += "> GPS Enabled (L76K hardware GPS)</label>";
    html += "<p class='hint'>When GPS is enabled, position is sourced from the GPS module. "
            "The manual coordinates below are used as fallback until a fix is acquired.</p>";
    html += "<div class='row2'>";
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)gCfg->gpsPollIntervalS);
    html += "<label>GPS Poll Interval (s)<input name='gps_poll_s' type='number' min='0' max='3600' step='1' value='";
    html += tmp; html += "'></label>";
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)gCfg->posIntervalS);
    html += "<label>GPS Broadcast Interval (s)<input name='pos_intv' type='number' min='60' value='";
    html += tmp; html += "'></label></div>";
    html += "<p class='hint'>Poll is how often this device reads its own GPS "
            "(0 = every loop). Broadcast is how often that position is sent to "
            "the mesh.</p>";
    html += "<label>GPS Standby Between Polls<select name='gps_duty'>"
            "<option value='0'"; if (!gCfg->gpsDutyCycleEnabled) html += " selected"; html += ">Off (always on)</option>"
            "<option value='1'"; if ( gCfg->gpsDutyCycleEnabled) html += " selected"; html += ">On</option>"
            "</select></label>";
    html += "<p class='hint'>Parks the GPS receiver between polls to save power. "
            "Needs a poll interval long enough for it to reacquire; test it on your "
            "unit before relying on it.</p>";
#endif
    html += "<div class='row2'>";
    snprintf(tmp, sizeof(tmp), "%.7f", gCfg->latI * 1e-7);
    html += "<label>Latitude&deg; (fallback)<input name='lat' type='number' step='0.0000001' value='";
    html += tmp; html += "'></label>";
    snprintf(tmp, sizeof(tmp), "%.7f", gCfg->lonI * 1e-7);
    html += "<label>Longitude&deg; (fallback)<input name='lon' type='number' step='0.0000001' value='";
    html += tmp; html += "'></label></div>";
    snprintf(tmp, sizeof(tmp), "%d", (int)gCfg->alt);
    html += "<label>Altitude m (fallback)<input name='alt' type='number' value='";
    html += tmp; html += "' style='max-width:120px'></label>";
    sectionEnd(html, lite);
    sendChunk(html);

    // ── LoRa Radio ────────────────────────────────────────────
    section(html, lite, "LoRa Radio", "lora", "Must match the other nodes you want to hear.");
    // The live frequency/BW/SF/CR readout is driven by kPresetJs. Lite skips
    // both, so its selects carry no onchange handler to call.
    html += lite ? "<div class='row2'><label>Region<select name='region' id='sel-rgn'>"
                 : "<div class='row2'><label>Region<select name='region' id='sel-rgn' onchange='applyPreset()'>";
    sendChunk(html);
    sendFlash(kRegionOptions);
    // The amateur-band regions, offered as the device picker offers them.
    {
#if FEATURE_LICENSED_MODE
        const bool licensed = gCfg->licensedMode;
#else
        const bool licensed = false;
#endif
        for (uint8_t i = 0; i < kRegionCount; i++) {
            const RegionPlan &r = kRegions[i];
            if (!r.licensedOnly || !regionSelectable(r.code, licensed)) continue;
            snprintf(tmp, sizeof(tmp), "<option value='%s'>%s (%g&ndash;%g MHz, licensed)</option>",
                     r.code, r.code, (double)r.freqStart, (double)r.freqEnd);
            html += tmp;
        }
    }
    html += lite ? "</select></label><label>Modem Preset<select id='sel-pst' name='modem_preset'>"
                 : "</select></label><label>Modem Preset<select id='sel-pst' name='modem_preset' onchange='applyPreset()'>";
    sendChunk(html);
    sendFlash(kPresetOptions);
    html += "</select></label></div>";
    sendChunk(html);
    sendFlash(kCustomLoraFields);
    // Both option lists are static constants, so the current values are applied
    // here rather than by marking one <option> selected. This runs before
    // kPresetJs registers its DOMContentLoaded handler, so the first
    // applyPreset() already sees these values.
    html += "<script>document.getElementById('sel-rgn').value='";
    appendJsStr(html, gCfg->region); html += "';"
            "document.getElementById('sel-pst').value='";
    html += gCfg->loraUsePreset
                ? kPresets[gCfg->modemPreset < PRESET_COUNT ? gCfg->modemPreset : 0].name
                : "Custom";
    html += "';";
    // A value with no matching <option> — a bandwidth this radio cannot do, or
    // an SF/CR out of range from an imported config — would leave the select
    // blank. Coerce to what applyPresetParams() would land on anyway. Set
    // through a one-line helper so the four assignments still fit in tmp[96].
    html += "function sv(i,v){document.getElementById(i).value=v;}";
    snprintf(tmp, sizeof(tmp), "sv('sel-cbw',%u);sv('sel-csf',%u);sv('sel-ccr',%u);sv('in-cslot',%u);",
             (unsigned)loraCoerceBwCode(gCfg->loraCustomBwKhz),
             (unsigned)constrain((int)gCfg->loraCustomSf, LORA_SF_MIN, LORA_SF_MAX),
             (unsigned)constrain((int)gCfg->loraCustomCr, LORA_CR_MIN, LORA_CR_MAX),
             (unsigned)gCfg->loraCustomSlot);
    html += tmp;
    // Lite carries no kPresetJs, so the custom fields' onchange would throw on
    // every edit. Stub it: with no live readout to update there is nothing for
    // it to do, and the block it would show/hide is always visible here.
    if (lite) html += "function applyPreset(){}";
    html += "</script>";
    html += "<p class='hint'>Frequency and modem parameters are derived automatically "
            "from region and preset, or from the fields above when the preset is Custom.</p>";
    sendChunk(html);
    if (!lite) {
        sendFlash(kPresetJs);
        sendFlash(kLoraReadout);
        appendCommunityPresetPicker(html);
        sendChunk(html);
    }
    html += "<div class='row2'>";
    snprintf(tmp, sizeof(tmp), "%d", gCfg->loraPower);
    html += "<label>TX Power (dBm, 1&ndash;22)<input name='pwr' type='number' min='1' max='22' value='";
    html += tmp; html += "'></label>";
    snprintf(tmp, sizeof(tmp), "%d", gCfg->loraHopLimit);
    html += "<label>Hop Limit (1&ndash;7)<input name='hop' type='number' min='1' max='7' value='";
    html += tmp; html += "'></label></div>";
    html += "<label>RX Boosted Gain<select name='rx_boost'>"
            "<option value='1'"; if ( gCfg->loraRxBoostedGain) html += " selected"; html += ">On</option>"
            "<option value='0'"; if (!gCfg->loraRxBoostedGain) html += " selected"; html += ">Off</option>"
            "</select></label>";
    html += "<p class='hint'>About 2 dB more receive sensitivity for roughly 2 mA "
            "more current, all the time the radio listens. Applies after the reboot "
            "that saving does.</p>";
    html += "<label>"
            "<input type='checkbox' name='ok_to_mqtt' value='1'";
    if (gCfg->okToMqtt) html += " checked";
    html += "> OK to MQTT &mdash; allow MQTT-connected nodes to forward your packets upstream</label>";
    html += "<label>"
            "<input type='checkbox' name='ignore_mqtt' value='1'";
    if (gCfg->ignoreMqtt) html += " checked";
    html += "> Ignore MQTT &mdash; drop received packets that arrived via MQTT</label>";
#if defined(DEVICE_TDISPLAY_P4)
    // Antenna: the board's on-board antenna or its external socket. Moving to
    // external asks first, exactly as the device does -- transmitting into an
    // empty socket can damage the radio -- and Cancel puts the select back so
    // nothing is submitted. Moving back to internal never asks. data-cur is
    // the saved value, so re-picking External when it already is asks nothing.
    {
        const bool ext = cfgP4AntennaExternal(*gCfg);
        html += "<label>Antenna<select name='antenna' data-cur='";
        html += ext ? "1" : "0";
        html += "' onchange=\"if(this.value=='1'&&this.dataset.cur!='1'&&!confirm("
                "'Switching to external can damage the radio if the antenna is not attached. "
                " Please attach the external antenna before pressing OK.'))this.value='0';\">"
                "<option value='0'";
        if (!ext) html += " selected";
        html += ">Internal</option><option value='1'";
        if (ext) html += " selected";
        html += ">External</option></select></label>";
    }
#endif
    sectionEnd(html, lite);
    sendChunk(html);

    // ── Channels ──────────────────────────────────────────────
    section(html, lite, "Channels", "channels", "Up to 8 channels; the first is primary.");
    html += "<p class='hint'>Key: base64 (e.g. \"AQ==\" or \"MA==\"). "
            "Hash is recomputed automatically on save. "
            "Uplink publishes this channel's traffic to MQTT; downlink re-broadcasts "
            "MQTT traffic for it onto LoRa. Location broadcasts this node's position "
            "on the channel &mdash; only where Share Location above is on, and never "
            "on an unnamed or disabled channel. AEAD switches the channel to "
            "Meshtastic 2.8.1's authenticated encryption: every node on it must "
            "turn it on too, and nodes on older firmware stop hearing it.</p>";
    // Marker so the POST handler applies per-channel checkbox state (an unchecked
    // box submits nothing) only when this section was rendered.
    html += "<input type='hidden' name='ch_flags' value='1'>";
    if (!lite) {
        html += "<p class='hint'>Use &#9650;/&#9660; to move a channel between "
                "slots. Channel 0 is the primary and stays where it is.</p>";
    }
    html += "<div id='ch-list'>";
    char b64buf[48];
    // Channel roles, not the device role list kRoles above — same function scope.
    static const char *kChanRoles[] = {"PRIMARY", "SECONDARY", "DISABLED"};
    // Field name, label, and current state for the per-channel toggles, so the
    // near-identical blocks this replaced cannot drift apart.
    struct ChanToggle { const char *suffix; const char *label; bool on; };
    for (int i = 0; i < MESH_CHANNELS; i++) {
        const ChannelKey &ch = CHANNEL_KEYS[i];
        base64Encode(ch.key, ch.keyLen, b64buf);
        html += "<div class='ch-row'>";

        // Header: slot number, reorder controls, Clear.
        snprintf(tmp, sizeof(tmp), "<div class='ch-hd'><span class='ch-no'>Channel %d</span>", i);
        html += tmp;
        if (!lite) {
            // Slot 0 is the primary and never moves, so its buttons are present
            // but disabled — dropping them would leave its header a different
            // shape from the others. Slot 1 cannot move up into 0, and the
            // last slot cannot move down: those limits are fixed by position, so
            // they are baked in here rather than recomputed after every swap.
            const bool canUp   = (i > 1);
            const bool canDown = (i > 0 && i < MESH_CHANNELS - 1);
            html += "<button type='button' class='ch-btn' data-mv='-1' title='Move up'";
            if (!canUp) html += " disabled";
            html += ">&#9650;</button>";
            html += "<button type='button' class='ch-btn' data-mv='1' title='Move down'";
            if (!canDown) html += " disabled";
            html += ">&#9660;</button>";
        }
        // Clear stays a self-contained inline handler: web config lite ships no
        // script at all, and this button works there too. It now unchecks the
        // toggles instead of blanking their value attribute — that left the box
        // ticked while submitting an empty value, which the POST handler read as
        // off, so the form and the device disagreed.
        html += "<button type='button' class='ch-clear' onclick=\""
                "this.closest('.ch-row').querySelectorAll('input').forEach(function(el){"
                "if(el.type=='checkbox')el.checked=false;else el.value='';})\">Clear</button></div>";

        // Name / Key / Role
        html += "<div class='ch-fields'>";
        snprintf(tmp, sizeof(tmp), "ch%d_name", i);
        html += "<label>Name<input name='"; html += tmp;
        html += "' type='text' maxlength='11' value='";
        appendAttr(html, ch.name);
        html += "'></label>";
        snprintf(tmp, sizeof(tmp), "ch%d_key", i);
        html += "<label>Key<input name='"; html += tmp;
        html += "' type='text' value='"; html += b64buf; html += "'";
#if FEATURE_LICENSED_MODE
        // Keys stay empty while licensed; handlePostSave ignores this field then.
        if (gCfg->licensedMode) html += " disabled";
#endif
        html += "></label>";
        snprintf(tmp, sizeof(tmp), "ch%d_role", i);
        html += "<label>Role<select name='"; html += tmp; html += "'>";
        for (int r = 0; r < 3; r++) {
            snprintf(tmp, sizeof(tmp), "<option value='%d'%s>%s</option>",
                     r, (ch.role == r) ? " selected" : "", kChanRoles[r]);
            html += tmp;
        }
        html += "</select></label>";

        // Hop limit: a select rather than a number box so "Default" is a value
        // you pick, not an empty field you have to know to leave alone. The
        // reorder script swaps `input,select` values between rows, so this moves
        // with its channel like everything else here.
        snprintf(tmp, sizeof(tmp), "ch%d_hop", i);
        html += "<label>Hops<select name='"; html += tmp; html += "'>";
        {
            char optbuf[96];
            snprintf(optbuf, sizeof(optbuf), "<option value=''%s>Default (%d)</option>",
                     chanHopLimitSet(ch.hopLimitPlus1) ? "" : " selected",
                     (int)gCfg->loraHopLimit);
            html += optbuf;
            for (int h = 0; h <= 7; h++) {
                const bool sel = chanHopLimitSet(ch.hopLimitPlus1)
                                 && chanHopLimitGet(ch.hopLimitPlus1) == (uint8_t)h;
                snprintf(optbuf, sizeof(optbuf), "<option value='%d'%s>%d%s</option>",
                         h, sel ? " selected" : "", h,
                         (h == 0) ? " (direct only)" : "");
                html += optbuf;
            }
        }
        html += "</select></label>";
        html += "</div>";

        // Toggles
        const ChanToggle toggles[] = {
            { "up",   "Uplink",   ch.uplinkEnabled },
            { "down", "Downlink", ch.downlinkEnabled },
            { "loc",  "Location", ch.shareLocation },
            { "aead", "AEAD",     ch.useAead },
        };
        html += "<div class='ch-tog'>";
        for (const ChanToggle &t : toggles) {
            snprintf(tmp, sizeof(tmp), "<label><input type='checkbox' name='ch%d_%s' value='1'%s>%s</label>",
                     i, t.suffix, t.on ? " checked" : "", t.label);
            html += tmp;
        }
        html += "</div></div>";
        sendChunkIfBig(html);
    }
    html += "</div>";
    if (!lite) {
        // Reordering swaps the two rows' field values rather than moving the rows
        // themselves: the slot number a channel occupies is what the mesh sees,
        // so the headers have to stay put and the definitions move between them.
        // That also keeps every ch<N>_* input name bound to its own slot, so the
        // POST handler needs no idea any of this happened.
        html += "<script>"
                "(function(){"
                "var L=document.getElementById('ch-list');"
                "L.addEventListener('click',function(e){"
                "var b=e.target.closest('button[data-mv]');if(!b)return;"
                "var rows=Array.prototype.slice.call(L.children);"
                "var i=rows.indexOf(b.closest('.ch-row')),j=i+(+b.dataset.mv);"
                "if(i<1||j<1||j>=rows.length)return;"
                "var a=rows[i].querySelectorAll('input,select');"
                "var c=rows[j].querySelectorAll('input,select');"
                "for(var k=0;k<a.length;k++){"
                "if(a[k].type=='checkbox'){var t=a[k].checked;a[k].checked=c[k].checked;c[k].checked=t;}"
                "else{var v=a[k].value;a[k].value=c[k].value;c[k].value=v;}}"
                "});"
                "})();"
                "</script>";
    }
    sectionEnd(html, lite);
    sendChunk(html);

    // ── MQTT ──────────────────────────────────────────────────
    section(html, lite, "MQTT", "mqtt", "Bridge the mesh to an MQTT broker over WiFi.");
    // Marker so the POST handler can apply checkbox state (unchecked boxes send
    // nothing) only when this section was actually rendered.
    html += "<input type='hidden' name='mqtt_present' value='1'>";
    html += "<label>"
            "<input type='checkbox' name='mqtt_en' value='1'";
    if (gCfg->mqttEnabled) html += " checked";
    html += "> Enable MQTT bridge</label>";
    html += "<label>Broker<input name='mqtt_server' type='text' maxlength='63' value='";
    appendAttr(html, gCfg->mqttServer);
    html += "'></label>";
    html += "<div class='row2'>";
    snprintf(tmp, sizeof(tmp), "%u", (unsigned)gCfg->mqttPort);
    html += "<label>Port<input name='mqtt_port' type='number' min='1' max='65535' value='";
    html += tmp; html += "'></label>";
    // No TLS checkbox: this firmware has no TLS client, so MQTT is always
    // plaintext (use port 1883). Channel payloads remain end-to-end encrypted
    // with the channel key regardless of transport.
    html += "</div>";
    html += "<p class='hint'>MQTT is plaintext only (port 1883) — this "
            "firmware has no TLS client. Channel payloads stay encrypted with "
            "your channel key.</p>";
    html += "<label>Username<input name='mqtt_user' type='text' maxlength='31' value='";
    appendAttr(html, gCfg->mqttUser);
    html += "'></label>"
            "<label>Password<input name='mqtt_pass' type='password' maxlength='47' value='";
    appendAttr(html, gCfg->mqttPass);
    html += "'></label>";
    html += "<label>Root topic<input name='mqtt_root' type='text' maxlength='47' value='";
    appendAttr(html, gCfg->mqttRoot);
    html += "'></label>";
    html += "<label>"
            "<input type='checkbox' name='mqtt_encrypt' value='1'";
    if (gCfg->mqttEncryption) html += " checked";
    html += "> Encrypt payloads (publish ciphertext to /2/e/)</label>";
    html += "<label>"
            "<input type='checkbox' name='mqtt_map_report' value='1'";
    if (gCfg->mqttMapReport) html += " checked";
    html += "> Map reporting (this node to /2/map/ every 15 min)</label>";
    html += "<p class='hint'>"
            "Requires WiFi credentials above. Default broker: mqtt.meshtastic.org:8883.</p>";
    sectionEnd(html, lite);
    sendChunk(html);

    // ── Display ───────────────────────────────────────────────
    section(html, lite, "Display", "display", "Theme, brightness and fonts on the device.");
    // Language of the on-device UI (issue #99). The page itself stays English.
    // Each name is written in its own language, as the device picker shows it;
    // the value is the index stored in RhinoConfig::uiLanguage. Changing it
    // takes the reboot every other setting here does.
#if I18N_ENABLED
    html += "<label>Language<select name='ui_lang'>";
    for (uint8_t v = 0; v < LANG_COUNT; v++) {
        snprintf(tmp, sizeof(tmp), "<option value='%u'%s>%s</option>",
                 (unsigned)v, (v == gCfg->uiLanguage) ? " selected" : "",
                 kUiLangNames[v]);
        html += tmp;
    }
    html += "</select></label>";
#endif
#if HAS_SPELLCHECK
    // Next to Language because it depends on it: the suggestions are English
    // and only offered while the device UI is. A select rather than a checkbox,
    // like Lock Screen, so the save can tell "off" from "not on this page".
    html += "<label>Spell Check<select name='spell_check'>"
            "<option value='0'"; if (!gCfg->spellCheckEnabled) html += " selected";
    html += ">Off</option>"
            "<option value='1'"; if ( gCfg->spellCheckEnabled) html += " selected";
    html += ">On (English UI only)</option></select></label>";
#endif
    // Brightness: a range input in the same 10% steps as the on-device slider,
    // with the value mirrored next to it since a bare range shows no number.
    // Not where the system sets it (HAS_SYSTEM_SETTINGS).
#if !HAS_SYSTEM_SETTINGS
    {
        char b[8];
        snprintf(b, sizeof(b), "%u", (unsigned)cfgCoerceBrightness(gCfg->brightness));
        html += "<label>Brightness <output id='briOut'>";
        html += b; html += "%</output>"
                "<input name='brightness' type='range' id='briIn'"
                " min='"; html += String(BRIGHTNESS_PCT_MIN);
        html += "' max='"; html += String(BRIGHTNESS_PCT_MAX);
        html += "' step='"; html += String(BRIGHTNESS_PCT_STEP);
        html += "' value='"; html += b;
        html += "' oninput=\"document.getElementById('briOut').textContent=this.value+'%'\">"
                "</label>";
    }
#endif
#if FEATURE_LOCK_SCREEN
    // The lock screen's own level, mirroring the second row of the on-device
    // brightness modal. Same range and step as above so the two read against
    // each other; the useful comparison is how much dimmer the glance surface
    // is than the UI.
    {
        char b[8];
        snprintf(b, sizeof(b), "%u",
                 (unsigned)cfgCoerceBrightness(gCfg->lockScreenBrightness));
        html += "<label>Lock Screen Brightness <output id='lbriOut'>";
        html += b; html += "%</output>"
                "<input name='lock_brightness' type='range' id='lbriIn'"
                " min='"; html += String(BRIGHTNESS_PCT_MIN);
        html += "' max='"; html += String(BRIGHTNESS_PCT_MAX);
        html += "' step='"; html += String(BRIGHTNESS_PCT_STEP);
        html += "' value='"; html += b;
        html += "' oninput=\"document.getElementById('lbriOut').textContent=this.value+'%'\">"
                "</label>";
        html += "<p class='hint'>"
                "Backlight level while the lock screen is showing, separate from "
                "the brightness above. Defaults to 10% — it is a glance surface, "
                "and on a lit panel this is most of what the lock screen costs "
                "in battery.</p>";
    }
#endif
#if HAS_RUNTIME_ORIENTATION
    // Same position as the on-device Config screen: after the lock-screen rows,
    // at the end of the run of settings about what the panel itself does.
    {
        // Two portraits, a half turn apart. Which one is right depends on
        // where the cable leaves the case and which hand is holding it, so it
        // is a choice rather than something the firmware can pick.
        html += "<label>Orientation<select name='orientation'>"
                "<option value='0'";
        if (gCfg->uiOrientation == 0) html += " selected";
        html += ">Landscape</option>"
                "<option value='1'";
        if (gCfg->uiOrientation == 1) html += " selected";
        html += ">Portrait</option>"
                "<option value='2'";
        if (gCfg->uiOrientation == 2) html += " selected";
        html += ">Portrait (180&deg;)</option>"
                "</select></label>";
        html += "<p class='hint'>"
                "Which way up the display runs. The panel is rotated once at "
                "startup, so this takes effect when the device reboots — which "
                "saving here does anyway.</p>";
    }
#endif
    html += "<div class='row2'>";
#if !HAS_SYSTEM_SETTINGS
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)gCfg->screenOnSecs);
    html += "<label>Screen Timeout (s)<input name='screen_on' type='number' min='0' value='";
    html += tmp; html += "'></label>";
#endif
#if FEATURE_LOCK_SCREEN
    // Two selects rather than a number box: both are short lists of stopping
    // points, and cfgCoerceLockScreenOffSecs() would silently move a typed 37
    // minutes anyway. Same values as the on-device slider, in the same order.
    html += "<label>Lock Screen<select name='lock_screen'>"
            "<option value='0'"; if (!gCfg->lockScreenEnabled) html += " selected";
    html += ">Off</option>"
            "<option value='1'"; if ( gCfg->lockScreenEnabled) html += " selected";
    html += ">On</option></select></label>";
    html += "<label>Lock Screen Off<select name='lock_screen_off'>";
    {
        static const struct { uint32_t secs; const char *label; } kOffs[] = {
            {   30, "30 sec" }, {   60, "1 min"  }, {  120, "2 min"  },
            {  300, "5 min"  }, {  600, "10 min" }, {  900, "15 min" },
            { 1200, "20 min" }, { 1500, "25 min" }, { 1800, "30 min" },
            { 2100, "35 min" }, { 2400, "40 min" }, { 2700, "45 min" },
            { 3000, "50 min" }, { 3300, "55 min" }, { 3600, "60 min" },
            {    0, "Stay on" },
        };
        for (const auto &o : kOffs) {
            snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)o.secs);
            html += "<option value='"; html += tmp; html += "'";
            if (gCfg->lockScreenOffSecs == o.secs) html += " selected";
            html += ">"; html += o.label; html += "</option>";
        }
    }
    html += "</select></label>";
#endif
        html += "<label>Units (Display &amp; Telemetry)<select name='disp_units'>"
            "<option value='0'"; if (!gCfg->displayUnits) html += " selected"; html += ">Metric (C / hPa)</option>"
            "<option value='1'"; if ( gCfg->displayUnits) html += " selected"; html += ">Imperial (F / inHg)</option>"
            "</select></label>";
    html += "<label>Battery Display<select name='batt_display'>"
            "<option value='0'";
    if (gCfg->battDisplayMode != BATT_DISPLAY_VOLTAGE) html += " selected";
    html += ">Percentage (87%)</option>"
            "<option value='1'";
    if (gCfg->battDisplayMode == BATT_DISPLAY_VOLTAGE) html += " selected";
    html += ">Voltage (3.94V)</option>"
            "</select></label></div>";
#if !HAS_SYSTEM_SETTINGS   // the system's battery gauge
    html += "<label>Battery Calibration (0.1% steps)<input name='batt_cal' type='number' ";
    snprintf(tmp, sizeof(tmp), "min='%d' max='%d' step='1' value='%d'></label>",
             BATT_CAL_TRIM_MIN, BATT_CAL_TRIM_MAX, (int)cfgCoerceBattCalTrim(gCfg->battCalTrim));
    html += tmp;
    html += "<p class='hint'>Corrects the battery voltage reading: +25 reads 2.5% "
            "higher. 0 is uncalibrated. Easier on the device (Config, Battery "
            "Calibration), which previews against a meter as you adjust.</p>";
#endif
#if HAS_SCROLL_INVERT
    // Trackball direction. Sits in Display rather than getting a section of its
    // own: it is a comfort preference like brightness, and one heading is heap
    // the AP-mode lite page cannot spare.
    html += "<div class='row2'>";
    html += "<label>Invert Scrolling (trackball)<select name='invert_scroll'>"
            "<option value='0'"; if (!gCfg->invertScroll) html += " selected"; html += ">Off</option>"
            "<option value='1'"; if ( gCfg->invertScroll) html += " selected"; html += ">On</option>"
            "</select></label></div>";
#endif
#if HAS_CHAT_STYLE_OPTIONS
    html += "<div class='row2'>";
#if defined(DEVICE_TDECK_PRO)
    html += "<label>Chat Type<select name='chat_style'>"
            "<option value='0'"; if (gCfg->chatStyle == CHAT_STYLE_CLASSIC) html += " selected"; html += ">Default</option>";
#else
    html += "<label>Chat Style<select name='chat_style'>"
            "<option value='0'"; if (gCfg->chatStyle == CHAT_STYLE_CLASSIC) html += " selected"; html += ">Classic</option>";
#endif
#if HAS_CHAT_BUBBLE_STYLE_OPTION
    html += "<option value='1'"; if (gCfg->chatStyle == CHAT_STYLE_BUBBLES) html += " selected"; html += ">Bubbles</option>";
#endif
    html += "<option value='2'"; if (gCfg->chatStyle == CHAT_STYLE_OUTLINE) html += " selected"; html += ">Outline</option>";
#if HAS_CHAT_IRC_STYLE_OPTION
    html += "<option value='3'"; if (gCfg->chatStyle == CHAT_STYLE_IRC) html += " selected"; html += ">IRC</option>";
#endif
    html += "</select></label></div>";
#endif
    html += "<div class='row2'>";
    html += "<label>Chat Names<select name='chat_names'>"
            "<option value='0'"; if (gCfg->chatNameStyle == CHAT_NAME_SHORT) html += " selected"; html += ">Short (ABCD)</option>"
            "<option value='1'"; if (gCfg->chatNameStyle == CHAT_NAME_LONG)  html += " selected"; html += ">Long (full name)</option>"
            "</select></label></div>";
#if HAS_UI_COLOR_OPTIONS
    html += "<div class='row2'>";
    html += "<label>Chat Colors<select name='chat_colors'>"
            "<option value='1'"; if ( gCfg->chatColorsEnabled) html += " selected"; html += ">On</option>"
            "<option value='0'"; if (!gCfg->chatColorsEnabled) html += " selected"; html += ">Off</option>"
            "</select></label>";
    html += "<label>My Message Color<select name='user_msg_color'>";
    snprintf(tmp, sizeof(tmp), "<option value='%u'%s>Default</option>",
             (unsigned)USER_MSG_COLOR_DEFAULT,
             gCfg->userMsgColor == USER_MSG_COLOR_DEFAULT ? " selected" : "");
    html += tmp;
    for (int i = 0; i < kUserMsgPaletteCount; i++) {
        snprintf(tmp, sizeof(tmp), "<option value='%d'%s>%s</option>", i,
                 gCfg->userMsgColor == i ? " selected" : "", kUserMsgPalette[i].name);
        html += tmp;
    }
    // Only offered while it is the setting: the custom colour itself is picked
    // on the device, and this keeps a save from quietly discarding it.
    if (gCfg->userMsgColor == USER_MSG_COLOR_CUSTOM) {
        snprintf(tmp, sizeof(tmp), "<option value='%u' selected>Custom (#%06lX)</option>",
                 (unsigned)USER_MSG_COLOR_CUSTOM, (unsigned long)gCfg->userMsgCustomRgb);
        html += tmp;
    }
    html += "</select></label></div>";
#endif
    // Font size chooser — a modal (rather than a plain select) so the sizes read
    // as a deliberate pick. The button shows the current value; the hidden input
    // is what actually submits with the form.
    {
        const uint8_t fs = (gCfg->fontSize <= FONT_SIZE_MAX) ? gCfg->fontSize : FONT_SIZE_MEDIUM;
        const char *fsName = (fs == FONT_SIZE_SMALL) ? "Small"
                             : (fs == FONT_SIZE_LARGE) ? "Large"
                             : (fs == FONT_SIZE_XLARGE) ? "Extra Large" : "Medium";
        if (lite) {
            // Plain select — the modal needs CSS and JS that lite doesn't carry.
            static const char *kFsNames[] = { "Small", "Medium", "Large", "Extra Large" };
            html += "<label>Font Size (chat &amp; DM)<select name='font_size'>";
            for (uint8_t v = 0; v <= FONT_SIZE_MAX && v < 4; v++) {
                snprintf(tmp, sizeof(tmp), "<option value='%u'%s>%s</option>",
                         (unsigned)v, (v == fs) ? " selected" : "", kFsNames[v]);
                html += tmp;
            }
            html += "</select></label>";
        } else {
            html += "<label>Font Size (chat &amp; DM)</label>";
            html += "<button type='button' class='chooser-btn' id='fsBtn' onclick='fsOpen()'>";
            html += fsName;
            html += "</button>";
            snprintf(tmp, sizeof(tmp), "<input type='hidden' name='font_size' id='fsInput' value='%u'>", (unsigned)fs);
            html += tmp;
            sendChunk(html);
            sendFlash(kFontModal);
        }
    }
#if HAS_UI_THEMES
    if (lite) {
        // Plain select instead of the swatch grid: the grid is ~7 KB of JS plus
        // its own stylesheet, and it only exists to preview colors lite doesn't use.
        html += "<label>Theme<select name='ui_theme_preset'>";
        for (uint8_t v = 0; v < kThemePresetCount; v++) {
            snprintf(tmp, sizeof(tmp), "<option value='%u'%s>%s</option>",
                     (unsigned)v, (v == themePreset) ? " selected" : "",
                     kThemePresetNames[v]);
            html += tmp;
            sendChunkIfBig(html);
        }
        // Custom themes get plain options here — no builder, which needs the
        // grid page, but they must at least be listed. Without them a device
        // wearing a custom theme opens this page with nothing selected, so the
        // browser posts back the first option and a save from AP mode silently
        // replaces the user's theme with Camillia Dark.
        for (int slot = 0; slot < UI_CUSTOM_THEME_SLOTS; slot++) {
            const UiCustomTheme *ct = uiCustomThemeGet(slot);
            if (!ct) continue;
            const uint8_t v = (uint8_t)(kThemePresetCustomBase + slot);
            snprintf(tmp, sizeof(tmp), "<option value='%u'%s>%s</option>",
                     (unsigned)v, (v == themePreset) ? " selected" : "", ct->name);
            html += tmp;
            sendChunkIfBig(html);
        }
        html += "</select></label>";
    } else {
        html += "<label>Theme</label>";
        {
            char themeHidden[110];
            snprintf(themeHidden, sizeof(themeHidden),
                     "<input type='hidden' name='ui_theme_preset' id='themeInput' value='%u'>",
                     (unsigned)themePreset);
            html += themeHidden;
        }
        // The saved custom themes, emitted ahead of the picker so its script can
        // build cards for them. Server-side rather than fetched, because this
        // page already streams in one pass and the AP-mode variant cannot afford
        // a second round trip.
        html += "<script>var CUSTOM=[";
        {
            bool first = true;
            for (int slot = 0; slot < UI_CUSTOM_THEME_SLOTS; slot++) {
                const UiCustomTheme *ct = uiCustomThemeGet(slot);
                if (!ct) continue;
                char row[160];
                snprintf(row, sizeof(row),
                         "%s{s:%d,n:'%s',m:%u,bg:'#%06lX',p:'#%06lX',p2:'#%06lX',a:'#%06lX'}",
                         first ? "" : ",", slot, ct->name, (unsigned)ct->mode,
                         (unsigned long)rgb565To888(ct->bgMain),
                         (unsigned long)rgb565To888(ct->panelBg),
                         (unsigned long)rgb565To888(ct->panelAlt),
                         (unsigned long)rgb565To888(ct->accent));
                html += row;
                first = false;
                sendChunkIfBig(html);
            }
        }
        {
            char tail[64];
            snprintf(tail, sizeof(tail), "];var CUSTOM_BASE=%u,CUSTOM_SLOTS=%u;</script>",
                     (unsigned)kThemePresetCustomBase, (unsigned)UI_CUSTOM_THEME_SLOTS);
            html += tail;
        }
        sendChunk(html);
        sendFlash(kThemePicker);
    }
#endif
    sectionEnd(html, lite);
    sendChunk(html);

    // ── Notifications ─────────────────────────────────────────
    // Every way the device can get your attention, in one place rather than
    // split between Display (volume) and Modules (alert tones). The name holds
    // on every board: where there is no audio hardware the section is the
    // notification LED and keyboard blink alone.
    section(html, lite, "Notifications", "notify", "Sounds, vibration and LED for new messages.");
#if HAS_VOLUME_CONTROL
    // Same shape as the brightness slider. Compiled out where the board alerts
    // through a passive buzzer: amplitude is not controllable there, so the
    // control would be present and do nothing.
    {
        char v[8];
        snprintf(v, sizeof(v), "%u", (unsigned)cfgCoerceVolume((int)gCfg->volumePct));
        html += "<label>Notification Volume <output id='volOut'>";
        html += v; html += "%</output>"
                "<input name='volume' type='range' id='volIn'"
                " min='"; html += String(VOLUME_PCT_MIN);
        html += "' max='"; html += String(VOLUME_PCT_MAX);
        html += "' step='"; html += String(VOLUME_PCT_STEP);
        html += "' value='"; html += v;
        html += "' oninput=\"document.getElementById('volOut').textContent=this.value+'%'\">"
                "</label>";
    }
#endif
// Every board that can make a sound, which is the same test the on-device row
// uses (CFG_ACTION_MSG_ALERT) and the same one the Splash Melody select below
// already used. It was a three-device list, so the Heltec — which alerts
// through a passive buzzer on BOARD_BUZZER rather than a codec — chirped with
// no way to turn it off from this page, even though its Config screen had the
// setting all along. The Mesh Deck stays out on HAS_AUDIO_ALERTS being 0.
//
// All four options mean something on every board here. The buzzer boards used to
// be the exception — Default, Chirpy and Bass all fell through to a single
// tone() call, so only Off changed anything — and they now have three patterns
// of their own, pitched for a piezo rather than a speaker.
#if HAS_AUDIO_ALERTS
    html += "<label>Notification Sound<select name='msg_alert_sound'>"
            "<option value='0'"; if (gCfg->msgAlertSound == MSG_ALERT_SOUND_DEFAULT) html += " selected"; html += ">Default</option>"
            "<option value='1'"; if (gCfg->msgAlertSound == MSG_ALERT_SOUND_CHIRPY) html += " selected"; html += ">Chirpy</option>"
            "<option value='2'"; if (gCfg->msgAlertSound == MSG_ALERT_SOUND_BASS) html += " selected"; html += ">Bass</option>"
            "<option value='3'"; if (gCfg->msgAlertSound == MSG_ALERT_SOUND_OFF) html += " selected"; html += ">Off</option>"
            "</select></label>";
#endif
#if HAS_HAPTIC
    // Same values as the on-device Vibration row (cfgHapticStyle()).
    {
        static const char *kNames[HAPTIC_CFG_COUNT] = {
            "Off", "Pulse", "Gentle", "Long", "Heartbeat"
        };
        const uint8_t cur = cfgHapticStyle(*gCfg);
        html += "<label>Vibration<select name='vibration'>";
        for (uint8_t i = 0; i < HAPTIC_CFG_COUNT; i++) {
            snprintf(tmp, sizeof(tmp), "<option value='%u'%s>%s</option>",
                     (unsigned)i, (i == cur) ? " selected" : "", kNames[i]);
            html += tmp;
        }
        html += "</select></label>";
    }
#endif
#if HAS_NOTIFY_LED
    // Kept in this section rather than given its own: it is the same question
    // as the alert tone — how the device gets your attention — and one more
    // heading is heap the AP-mode lite page cannot spare. Compiled out on boards
    // with no notification LED wired.
    html += "<label>Channel Message LED Color<select name='notify_led_channel_color'>";
    {
        const uint8_t selected = cfgCoerceNotifyLedColor((int)gCfg->notifyLedColorChannel);
        for (uint8_t i = 0; i < kNotifyLedColorCount; i++) {
            char opt[96];
            snprintf(opt, sizeof(opt), "<option value='%u'%s>%s</option>",
                     (unsigned)i,
                     (i == selected) ? " selected" : "",
                     kNotifyLedColorNames[i]);
            html += opt;
        }
    }
    html += "</select></label>";

    html += "<label>Direct Message LED Color<select name='notify_led_dm_color'>";
    {
        const uint8_t selected = cfgCoerceNotifyLedColor((int)gCfg->notifyLedColorDm);
        for (uint8_t i = 0; i < kNotifyLedColorCount; i++) {
            char opt[96];
            snprintf(opt, sizeof(opt), "<option value='%u'%s>%s</option>",
                     (unsigned)i,
                     (i == selected) ? " selected" : "",
                     kNotifyLedColorNames[i]);
            html += opt;
        }
    }
    html += "</select></label>";
#endif
#if HAS_KB_BLINK
    // Sits with the LED colors above for the same reason they sit here: it
    // answers the same question on a board whose only notification light is the
    // keyboard.
    html += "<label>Keyboard Blink on Message<select name='kb_blink'>"
            "<option value='1'"; if ( gCfg->kbBlinkEnabled) html += " selected"; html += ">Enabled</option>"
            "<option value='0'"; if (!gCfg->kbBlinkEnabled) html += " selected"; html += ">Disabled</option>"
            "</select></label>";
    // Flash counts. Separate per kind because with no notification LED on these
    // boards the length of the pattern is the only thing telling a DM from a
    // channel message.
    html += "<div class='row2'>";
    for (int f = 0; f < 2; f++) {
        const bool isDm = (f == 1);
        const uint8_t cur = cfgCoerceKbFlashes(
            (int)(isDm ? gCfg->kbBlinkDmFlashes : gCfg->kbBlinkChanFlashes));
        html += isDm ? "<label>Blink Flashes (DM)<select name='kb_flash_dm'>"
                     : "<label>Blink Flashes (Channel)<select name='kb_flash_chan'>";
        for (int n = KB_FLASHES_MIN; n <= KB_FLASHES_MAX; n++) {
            snprintf(tmp, sizeof(tmp), "<option value='%d'%s>%d flash%s</option>",
                     n, (n == cur) ? " selected" : "", n, (n == 1) ? "" : "es");
            html += tmp;
        }
        html += "</select></label>";
    }
    html += "</div>";
    html += "<p class='hint'>"
            "Only used while the screen is off.</p>";
#endif
#if HAS_LIGHT_NOTIFY
    // One field for whichever light the board has: the notification LED and the
    // keyboard backlight never coexist, and the question is the same either way.
    html += "<label>Notification Light Timeout<select name='light_timeout'>";
    for (int i = 0; i < kNotifyLightTimeoutCount; i++) {
        snprintf(tmp, sizeof(tmp), "<option value='%u'%s>%s</option>",
                 (unsigned)kNotifyLightTimeouts[i].secs,
                 (gCfg->notifyLightTimeoutS == kNotifyLightTimeouts[i].secs) ? " selected" : "",
                 kNotifyLightTimeouts[i].label);
        html += tmp;
    }
    html += "</select></label>";
    html += "<p class='hint'>"
            "How long the light keeps reminding you after a message arrives. The "
            "clock restarts on each new message, and reading the message stops it "
            "immediately either way. Never means it keeps reminding until read.</p>";
#endif
#if defined(DEVICE_TDECK_PRO) || defined(DEVICE_TDISPLAY_P4)
    // The on/off that Alt+B (T-Deck Pro) or F7 (P4) flips; the level below is
    // how bright "on" is.
    html += "<label>Keyboard Light On<select name='kb_light_on'>"
            "<option value='1'"; if ( gCfg->kbBacklightEnabled) html += " selected"; html += ">On</option>"
            "<option value='0'"; if (!gCfg->kbBacklightEnabled) html += " selected"; html += ">Off</option>"
            "</select></label>";
#endif
#if HAS_KB_BACKLIGHT_LEVEL
    html += "<label>Keyboard Light<select name='keypad_light'>";
    for (int i = 0; i < kKbBacklightLevelCount; i++) {
        snprintf(tmp, sizeof(tmp), "<option value='%u'%s>%s</option>",
                 (unsigned)kKbBacklightLevels[i].level,
                 (gCfg->kbBacklightLevel == kKbBacklightLevels[i].level) ? " selected" : "",
                 kKbBacklightLevels[i].label);
        html += tmp;
    }
    html += "</select></label>";
    html += "<p class='hint'>"
#if defined(DEVICE_M9)
            "How brightly the keypad lights itself after a keypress. The keypad "
            "LEDs belong to the keyboard's own controller, which lights them for "
            "about ten seconds at a time - this sets how bright that is, and "
            "cannot hold them on. Off stops it lighting at all, and the keyboard "
            "then stays dark until the device is next power-cycled.</p>";
#elif defined(DEVICE_TDECK_PRO)
            "How brightly the keyboard is lit. Alt+B still turns it on and off - "
            "this is how bright \"on\" is. Notification blinks still work at any "
            "level: they pulse away from whatever you pick and settle back on "
            "it.</p>";
#elif defined(DEVICE_TDISPLAY_P4)
            "How brightly the keyboard expansion is lit, while it is attached and "
            "the screen is on. F7 on the keyboard still turns it on and off, and "
            "F11 steps through these same levels.</p>";
#elif defined(DEVICE_TANMATSU)
            "How brightly the keyboard is lit while the screen is on. It goes "
            "dark when the screen sleeps and comes back at this level.</p>";
#else
            "How brightly the keyboard stays lit. Off is what this board has "
            "always done - the backlight rested dark and lit only to flag an "
            "unread message. Notification blinks still work at any level: they "
            "pulse away from whatever you pick and settle back on it.</p>";
#endif
#endif
#if HAS_KB_LIGHT_TIMEOUT
    html += "<label>Keyboard Light Timeout<select name='kb_light_timeout'>";
    for (int i = 0; i < kKbLightTimeoutCount; i++) {
        snprintf(tmp, sizeof(tmp), "<option value='%u'%s>%s</option>",
                 (unsigned)kKbLightTimeouts[i].secs,
                 (gCfg->kbLightTimeoutS == kKbLightTimeouts[i].secs) ? " selected" : "",
                 kKbLightTimeouts[i].label);
        html += tmp;
    }
    html += "</select></label>";
    html += "<p class='hint'>"
            "Turns the keyboard light off after this long with no key press or "
            "touch, the way Screen Timeout does for the screen. The next key or "
            "touch lights it again. Notification blinks still flash while it is "
            "off. Never keeps it lit for as long as the screen is. Takes effect "
            "immediately, no reboot.</p>";
#endif
#if HAS_POWER_LED_LEVEL
    html += "<label>Status Lights<select name='power_led_pct'>";
    {
        const uint8_t cur = cfgCoercePowerLedPct(gCfg->powerLedPct);
        for (int i = 0; i < kPowerLedLevelCount; i++) {
            snprintf(tmp, sizeof(tmp), "<option value='%u'%s>%s</option>",
                     (unsigned)kPowerLedLevels[i].pct,
                     (cur == kPowerLedLevels[i].pct) ? " selected" : "",
                     kPowerLedLevels[i].label);
            html += tmp;
        }
    }
    html += "</select></label>";
#endif
#if HAS_CHANNEL_PANEL_LOCK
    {
        const bool locked = cfgChannelPanelLocked(*gCfg);
        html += "<label>Channel Panel<select name='channel_panel_lock'>"
                "<option value='1'"; if ( locked) html += " selected"; html += ">Locked</option>"
                "<option value='0'"; if (!locked) html += " selected"; html += ">Unlocked</option>"
                "</select></label>";
        html += "<p class='hint'>"
                "Locked keeps the channel list beside the chat at all times. "
                "Unlocked hides it in a drawer that slides in over the chat when "
                "asked for. With arrow keys, Left opens the drawer and Left again "
                "locks it; Right from the chat unlocks it. "
#if defined(DEVICE_TDISPLAY_P4)
                "Landscape only: portrait always uses the drawer. "
#endif
                "Takes effect immediately, no reboot.</p>";
    }
#endif
#if HAS_NAV_BAR_TOGGLE
    html += "<label>Bottom Nav Bar<select name='nav_bar'>"
            "<option value='1'"; if ( gCfg->navBarEnabled) html += " selected"; html += ">Enabled</option>"
            "<option value='0'"; if (!gCfg->navBarEnabled) html += " selected"; html += ">Disabled</option>"
            "</select></label>";
    html += "<p class='hint'>"
            "Row of icons along the bottom of the screen - Home, Chats, DM, "
            "Nodes, Tools, Config, Help - carrying the GPS and Wi-Fi readouts "
            "and the unread marks. Disabled restores the keyboard shortcut "
            "hints that sit there instead. The keys reach every one of them "
            "either way; where the board has a touch panel or a browser Remote, "
            "the cells are clickable too. Takes effect immediately, no reboot.</p>";
#endif
#if HAS_NAV_ALIGN_SETTING
    {
        const NavAlign navAlign = cfgNavAlign(*gCfg);
        html += "<label>Nav Alignment<select name='nav_align'>"
                "<option value='left'";   if (navAlign == NAV_ALIGN_LEFT)   html += " selected"; html += ">Left</option>"
                "<option value='center'"; if (navAlign == NAV_ALIGN_CENTER) html += " selected"; html += ">Center</option>"
                "<option value='right'";  if (navAlign == NAV_ALIGN_RIGHT)  html += " selected"; html += ">Right</option>"
                "</select></label>";
    }
    html += "<p class='hint'>"
            "Where the nav bar sits. Center is the row of icons along the bottom "
            "of the screen. Left and Right condense it into a narrow column down "
            "that side, one icon per button, and give the screen its full height "
            "back. Takes effect immediately, no reboot.</p>";
#endif
#if HAS_AUDIO_ALERTS
    html += "<label>Splash Melody<select name='splash_melody'>"
            "<option value='1'"; if ( gCfg->splashMelodyEnabled) html += " selected"; html += ">Enabled</option>"
            "<option value='0'"; if (!gCfg->splashMelodyEnabled) html += " selected"; html += ">Disabled</option>"
            "</select></label>";
#endif
    sectionEnd(html, lite);
    sendChunk(html);

    // ── Power ─────────────────────────────────────────────────
    // Only power saving and the lite page's debug row: neither is on a device
    // where the system owns power (HAS_SYSTEM_SETTINGS), which is never lite.
#if !HAS_SYSTEM_SETTINGS
    section(html, lite, "Power", "power");
    html += "<label>Power Saving<select name='pwr_saving'>"
            "<option value='1'"; if ( gCfg->isPowerSaving) html += " selected"; html += ">Enabled</option>"
            "<option value='0'"; if (!gCfg->isPowerSaving) html += " selected"; html += ">Disabled</option>"
            "</select></label>";
    html += "<p class='hint'>Naps the CPU in light sleep while the screen is off, "
            "waking on any input or scheduled transmission. Never while web config is running.</p>";
    // The full page has this on the Utilities tab, which lite does not carry --
    // and lite is all the Cardputer ever serves -- so it rides in the main form.
    if (lite) {
        html += "<label>Serial Debug Logging<select name='dbg_lite'>"
                "<option value='0'"; if (!monitorDebugEnabled()) html += " selected"; html += ">Off</option>"
                "<option value='1'"; if ( monitorDebugEnabled()) html += " selected"; html += ">On</option>"
                "</select></label>";
    }
    sectionEnd(html, lite);
    sendChunk(html);
#endif

    // ── Modules ───────────────────────────────────────────────
    section(html, lite, "Modules", "modules", "Telemetry, beacons and other optional features.");
    // Telemetry
    html += "<h3 class='cfg-sub'>Telemetry</h3>";
    html += "<div class='row2'>";
    html += "<label>Device Telemetry<select name='tel_dev_en'>"
            "<option value='1'"; if ( gCfg->telDeviceEnabled) html += " selected"; html += ">Enabled</option>"
            "<option value='0'"; if (!gCfg->telDeviceEnabled) html += " selected"; html += ">Disabled</option>"
            "</select></label>";
    uint32_t telemetryIntervalMins = (gCfg->telDeviceIntervalS >= 60UL)
        ? (uint32_t)(gCfg->telDeviceIntervalS / 60UL)
        : 60UL;
    if (telemetryIntervalMins < 60UL) telemetryIntervalMins = 60UL;
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)telemetryIntervalMins);
    html += "<label>Broadcast Interval (min)<input name='tel_bcast_mins' type='number' min='60' value='";
    html += tmp; html += "'></label></div>";

    html += "<h3 class='cfg-sub'>Sensor Telemetry</h3>";
    html += "<div class='row2'>";
#if HAS_ENV_SENSOR_TELEMETRY
    // Three states, and they are worth telling apart. A board that supports the
    // feature but has nothing on the bus used to look identical to one that was
    // simply switched off — the setting sat there saying "Enabled" while nothing
    // was ever sent, which is exactly the doubt this is meant to remove.
    //
    // Same shape as the Archive dropped nodes checkbox above: the control is
    // disabled where it cannot do anything, and the reason takes the place of
    // the description. The stored preference is left untouched either way, so it
    // survives until a sensor is present.
    {
        const bool sensorFound = envHasSensor();
        const bool stillLooking = !sensorFound && !envProbeExhausted();

        // Left enabled even with no sensor detected. Disabling it was a mistake:
        // it is a perfectly reasonable thing to switch on before fitting a
        // sensor, and the setting is harmless without one — the announce path
        // simply finds nothing to read. The status line below says what is
        // actually there; it does not need to police the control as well.
        html += "<label>Environment Sensor Telemetry<select name='tel_env_en'>"
                "<option value='1'"; if ( gCfg->telEnvEnabled) html += " selected"; html += ">Enabled</option>"
                "<option value='0'"; if (!gCfg->telEnvEnabled) html += " selected"; html += ">Disabled</option>"
                "</select></label>";
        html += "<label>Sensor Interval<input type='text' value='Uses broadcast interval above' disabled></label>";

        html += "<p class='hint hint-wide'>";
        if (sensorFound) {
            html += "Detected <b>";
            html += envSensorName();
            html += "</b> on <code>";
            html += envProbeRouteName();
            html += "</code>.";
        } else if (stillLooking) {
            html += "Still looking for a sensor. Reload the page to see the result.";
        } else {
            html += "<b>No sensor detected</b> at boot, so nothing is sent even with "
                    "this enabled. You can still turn it on ready for one. Run "
                    "<code>i2c scan all</code> on the serial console to see every "
                    "address that answered, on every candidate bus.";
        }
        html += "</p>";
    }
#else
    html += "<label>Environment Sensor Telemetry<input type='text' value='Unavailable on this build' disabled></label>";
    html += "<label>Sensor Interval<input type='text' value='Unavailable on this build' disabled></label>";
    html += "<input type='hidden' name='tel_env_en' value='0'>";
#endif
    html += "</div>";

    html += "<h3 class='cfg-sub'>Mesh Beacons</h3>";
    html += "<label>Listen for beacons<select name='mesh_beacon_listen'>"
            "<option value='1'"; if ( gCfg->meshBeaconListen) html += " selected"; html += ">Enabled</option>"
            "<option value='0'"; if (!gCfg->meshBeaconListen) html += " selected"; html += ">Disabled</option>"
            "</select></label>";
    html += "<p class='hint'>"
            "Meshtastic 2.7 nodes can retune briefly to advertise a different mesh, "
            "naming a channel, preset and region. With this on, any such beacon that "
            "reaches this device is listed on the Beacons screen, under Live &rarr; "
            "Tools. Listening transmits nothing, and an offer is never applied to "
            "your radio. Off by default.</p>";

    {
        const uint8_t target = gCfg->meshBeaconTargetPreset < PRESET_COUNT
            ? gCfg->meshBeaconTargetPreset : (uint8_t)PRESET_LONG_FAST;
        html += "<div class='row2'>";
        html += "<label>Broadcast<select name='mesh_beacon_broadcast'>"
                "<option value='1'"; if ( gCfg->meshBeaconBroadcast) html += " selected"; html += ">Enabled</option>"
                "<option value='0'"; if (!gCfg->meshBeaconBroadcast) html += " selected"; html += ">Disabled</option>"
                "</select></label>";
        html += "<label>Target preset<select name='mesh_beacon_target'>";
        // Keep a stored target this radio cannot run selected, so a save of
        // unrelated fields never substitutes the first listed preset for it.
        if (!presetUsableOnThisRadio(target)) {
            snprintf(tmp, sizeof(tmp), "%u", (unsigned)target);
            html += "<option value='"; html += tmp; html += "' selected>";
            appendAttr(html, kPresets[target].name);
            html += " (not available on this radio)</option>";
        }
        for (uint8_t i = 0; i < PRESET_COUNT; i++) {
            if (!presetUsableOnThisRadio(i)) continue;
            snprintf(tmp, sizeof(tmp), "%u", (unsigned)i);
            html += "<option value='"; html += tmp; html += "'";
            if (i == target) html += " selected";
            html += ">"; appendAttr(html, kPresets[i].name); html += "</option>";
        }
        html += "</select></label>";
        html += "<label>Include key<select name='mesh_beacon_offer_psk'>"
                "<option value='1'"; if ( gCfg->meshBeaconOfferPsk) html += " selected"; html += ">Enabled</option>"
                "<option value='0'"; if (!gCfg->meshBeaconOfferPsk) html += " selected"; html += ">Disabled</option>"
                "</select></label>";
        html += "<label>Interval<select name='mesh_beacon_interval'>";
        static const uint8_t kIntervalsH[] = {1, 2, 3, 6, 12, 24};
        const uint8_t curH = clampBeaconIntervalH(gCfg->meshBeaconIntervalH);
        for (uint8_t h : kIntervalsH) {
            snprintf(tmp, sizeof(tmp), "%u", (unsigned)h);
            html += "<option value='"; html += tmp; html += "'";
            if (h == curH) html += " selected";
            html += ">"; html += tmp; html += " h</option>";
        }
        html += "</select></label>";
        html += "</div>";
        html += "<label>Message<input name='mesh_beacon_message' type='text' maxlength='60' value='";
        char msg[sizeof(gCfg->meshBeaconMessage)];
        memcpy(msg, gCfg->meshBeaconMessage, sizeof(msg));
        msg[sizeof(msg) - 1] = '\0';
        appendAttr(html, msg);
        html += "'></label>";

        const ChannelKey &c0 = CHANNEL_KEYS[0];
        const char *home = c0.name_buf[0] ? c0.name_buf : c0.name;
        if (!home || !home[0]) {
            const uint8_t pi = gCfg->modemPreset < PRESET_COUNT ? gCfg->modemPreset : (uint8_t)PRESET_LONG_FAST;
            home = gCfg->loraUsePreset ? kPresets[pi].channelName : CUSTOM_CHANNEL_NAME;
        }
        html += "<p class='hint'>"
                "With Broadcast on, this device briefly retunes to the target preset on "
                "the default channel and sends a short beacon naming your primary channel, "
                "preset and region. Your own radio settings are never changed. "
                "With Include key on: <b>Anyone on ";
        appendAttr(html, kPresets[target].channelName);
        html += " can join ";
        appendAttr(html, home);
        html += ".</b></p>";
        if (lic::active()) {
            html += "<p class='hint'><b>Broadcast is off while licensed:</b> the beacon "
                    "is encrypted with the default key, so none is sent until licensed "
                    "mode is off.</p>";
        }
    }

    html += "<h3 class='cfg-sub'>Neighborhood Info</h3>";
    html += "<div class='row2'>";
    html += "<label>Enabled<select name='neighbor_info_en'>"
            "<option value='1'"; if ( gCfg->neighborInfoEnabled) html += " selected"; html += ">Enabled</option>"
            "<option value='0'"; if (!gCfg->neighborInfoEnabled) html += " selected"; html += ">Disabled</option>"
            "</select></label>";
    uint32_t neighborInfoIntervalMins = (gCfg->neighborInfoIntervalS >= 60UL)
        ? (uint32_t)(gCfg->neighborInfoIntervalS / 60UL)
        : 240UL;
    if (neighborInfoIntervalMins < 240UL) neighborInfoIntervalMins = 240UL;
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)neighborInfoIntervalMins);
    html += "<label>Broadcast Interval (min)<input name='neighbor_info_mins' type='number' min='240' value='";
    html += tmp; html += "'></label></div>";
    html += "<label>Transmit Over LoRa<select name='neighbor_info_lora'>"
            "<option value='1'"; if ( gCfg->neighborInfoOverLora) html += " selected"; html += ">Yes</option>"
            "<option value='0'"; if (!gCfg->neighborInfoOverLora) html += " selected"; html += ">No</option>"
            "</select></label>";

    // camillia chat server client
    html += "<h3 class='cfg-sub'>Chat Server</h3>";
    if (lic::active()) {
        html += "<p class='hint'><b>Off while licensed:</b> chat server traffic is "
                "encrypted, so the client sends nothing until licensed mode is off.</p>";
    }
    html += "<label>Catch-up<select name='cs_mode'>";
    {
        static const char *const kModes[3] = {"Off", "Automatic (boot + every 15 min)", "Manual only"};
        for (int m = 0; m < 3; m++) {
            html += "<option value='"; html += String(m); html += "'";
            if (gCfg->chatServerMode == m) html += " selected";
            html += ">"; html += kModes[m]; html += "</option>";
        }
    }
    html += "</select></label>";
    html += "<label>Chat Server Node<input name='cs_server' type='text' maxlength='16' placeholder='auto' value='";
    if (gCfg->chatServerNodeId != 0) {
        char idBuf[16];
        snprintf(idBuf, sizeof(idBuf), "!%08lx", (unsigned long)gCfg->chatServerNodeId);
        html += idBuf;
    }
    html += "'></label>"
            "<p class='hint'>"
            "A camillia chat server keeps channel messages and replays the ones this node "
            "missed. Leave blank to use the first one found nearby. Enter a short name or "
            "<code>!aabbccdd</code> to always use that node: a server set here is never "
            "replaced automatically. Replayed messages are marked in the chat.";
    if (gCfg->chatServerNodeId != 0) {
        html += (gCfg->chatServerFlags & CHAT_SERVER_FLAG_MANUAL) ? " Currently set manually." : " Currently found automatically.";
    }
    html += "</p>";

    sectionEnd(html, lite);
    sendChunk(html);

    // WiFi. Collapsed by default, matching MQTT / Power / Modules. A <details>
    // only hides its contents visually — the inputs stay in the form and submit
    // either way, which is what the other collapsed sections already rely on.
#if defined(PLATFORM_LINUX)
    // The CardputerZero joins whatever network the OS is on (issue #162), so
    // there is nothing to type in here: credentials saved from this page would
    // be stored and never used. Name the connection instead.
    section(html, lite, "WiFi", "wifi", "The system's network connection.");
    {
        const String sysSsid = WiFi.SSID();
        html += "<p>Uses the system's network connection";
        if (sysSsid.length()) {
            html += ": <b>";
            appendAttr(html, sysSsid.c_str());
            html += "</b>";
        }
        html += ".</p>"
                "<p class='hint'>Camillia does not manage Wi-Fi networks on the "
                "CardputerZero; the system's network settings do.</p>";
    }
#else
    section(html, lite, "WiFi", "wifi", "Networks this device joins.");
    // appendAttr, not a raw append: an SSID or password containing an
    // apostrophe would close the value= attribute and mangle the rest of the
    // form, so the field would come back truncated on the next save. Same
    // reason the channel-name fields use it.
    html += "<label>SSID<input name='wifi_ssid' type='text' maxlength='63' value='";
    appendAttr(html, gWifiSsid);
    html += "'></label>"
            "<label>Password<input name='wifi_pass' type='password' maxlength='63' value='";
    appendAttr(html, gWifiPass);
    html += "'></label>";
    if (!lite) {
        html += "<p class='hint'>This is the network the device joins.</p>";
        appendSavedNetworks(html);
    }
#endif
    {
        // How long this page may sit idle before the server shuts itself down.
        // The setting has existed in the config struct and in YAML export for
        // as long as the timeout has, but had no control anywhere — so the only
        // way to discover the ten-minute default was to be timed out by it
        // mid-session, and the only way to change it was to hand-edit a YAML
        // file and import it.
        //
        // A select rather than a number box: these are the stopping points that
        // mean something, and cfgCoerceWebCfgIdle() would silently move a typed
        // 7 to 60 anyway.
        static const struct { uint32_t secs; const char *label; } kIdleOpts[] = {
            { 300,  "5 minutes" },
            { 600,  "10 minutes (default)" },
            { 900,  "15 minutes" },
            { 1800, "30 minutes" },
            { 3600, "1 hour" },
            { 0,    "Never" },
        };
        const uint32_t cur = cfgCoerceWebCfgIdle((long)gCfg->webCfgIdleTimeoutS);
        html += "<label>Idle Timeout<select name='webcfg_idle_s'>";
        bool matched = false;
        for (size_t i = 0; i < sizeof(kIdleOpts) / sizeof(kIdleOpts[0]); i++) {
            const bool sel = (cur == kIdleOpts[i].secs);
            matched = matched || sel;
            snprintf(tmp, sizeof(tmp), "<option value='%lu'%s>%s</option>",
                     (unsigned long)kIdleOpts[i].secs, sel ? " selected" : "",
                     kIdleOpts[i].label);
            html += tmp;
        }
        // A value imported from YAML need not be one of the six. Showing it as
        // its own option beats silently re-selecting 5 minutes and then saving
        // that back the next time anything else on this page is changed.
        if (!matched) {
            snprintf(tmp, sizeof(tmp), "<option value='%lu' selected>%lu seconds</option>",
                     (unsigned long)cur, (unsigned long)cur);
            html += tmp;
        }
        html += "</select></label>";
        html += "<p class='hint'>How long web config stays up with no requests "
                "before it closes itself and releases WiFi. Loading or saving a "
                "page counts as activity; reading one does not. <b>Never</b> "
                "keeps WiFi and its power draw on until you stop it yourself.</p>";
    }
    sectionEnd(html, lite);
    sendChunk(html);

    // Firmware Updates. Compiled out on the Cardputer, where OTA is disabled
    // altogether — offering the toggle there would promise a check that never
    // leads anywhere. The update source itself is not configurable by design.
#if !defined(DEVICE_CARDPUTER_LORA_HAT) && !defined(DEVICE_CARDPUTER_ZERO)
    section(html, lite, "Firmware Updates", "firmware", "Update checks and over-the-air updates.");
    if (otaInstalledByLauncher()) {
        // Installed by bmorcelli's Launcher, which owns the partition table and
        // installs updates itself (#160).
        html += "<p class='hint'>"
                "Camillia was installed by Launcher, which keeps its own partition "
                "layout, so Camillia does not update itself here. Update it through "
                "Launcher: its OTA menu, an SD card, or its web interface. Flashing "
                "Camillia's factory image over USB removes Launcher and every app it "
                "installed.</p>";
    } else if (!otaLayoutSupportsUpdate()) {
        // Installed by a third-party flasher into its own partition layout: the
        // slot an update would land in belongs to another firmware. Say so
        // instead of offering a toggle that leads nowhere.
        html += "<p class='hint'>"
                "Updates are unavailable on this install: the device was flashed "
                "with a third-party partition layout, where the spare app slot may "
                "hold other firmware. Re-flash the factory image to enable "
                "over-the-air updates.</p>";
    } else {
        html += "<p class='hint'>"
                "On boot, once WiFi is up, the device asks whether a newer release "
                "exists and offers to install it. Declining is remembered until the "
                "next reboot.</p>";
        html += "<label>Check for Updates on Boot<select name='ota_autocheck'>"
                "<option value='1'"; if ( gCfg->otaAutoCheckEnabled) html += " selected"; html += ">Yes</option>"
                "<option value='0'"; if (!gCfg->otaAutoCheckEnabled) html += " selected"; html += ">No</option>"
                "</select></label>";

        // Selected from the RESOLVED channel, so a device flashed with an alpha
        // image shows Alpha rather than the stored AUTO reading as Stable.
        const uint8_t otaChannelNow = otaResolveChannel(gCfg->otaChannel);
        html += "<label>Release Channel<select name='ota_channel'>"
                "<option value='1'";
        if (otaChannelNow != OTA_CHANNEL_ALPHA) html += " selected";
        html += ">Stable</option>"
                "<option value='2'";
        if (otaChannelNow == OTA_CHANNEL_ALPHA) html += " selected";
        html += ">Alpha</option>"
                "</select></label>";
        html += "<p class='hint'>"
                "<b>Stable</b> installs published releases only. <b>Alpha</b> also "
                "takes prerelease builds cut from a development branch — earlier "
                "access, and rougher. Alpha images are signed and verified exactly "
                "like stable ones; what changes is how well tested they are. "
                "Switching back to Stable offers the newest stable release even "
                "though it is numerically older than the alpha you are running, "
                "so returning to the stable track never needs a USB reflash. "
                "Changing the channel here reboots the device and checks the new "
                "channel on the way back up, whatever the setting above says; "
                "any update it finds is offered on the device's own screen.</p>";

        const uint8_t autoUpdateNow = cfgCoerceOtaAutoUpdate(gCfg->otaAutoUpdatePeriod);
        html += "<label>Automatic Updates<select name='ota_autoupdate'>";
        static const struct { uint8_t v; const char *label; } kAutoUpdateOpts[] = {
            { OTA_AUTO_UPDATE_OFF, "Off" },
            { OTA_AUTO_UPDATE_1H,  "Every hour" },
            { OTA_AUTO_UPDATE_6H,  "Every 6 hours" },
            { OTA_AUTO_UPDATE_12H, "Every 12 hours" },
            { OTA_AUTO_UPDATE_24H, "Every 24 hours" },
        };
        for (const auto &opt : kAutoUpdateOpts) {
            html += "<option value='"; html += String((int)opt.v); html += "'";
            if (autoUpdateNow == opt.v) html += " selected";
            html += ">"; html += opt.label; html += "</option>";
        }
        html += "</select></label>";
        html += "<p class='hint'>"
                "When set, the device checks for a new release on this schedule and "
                "installs it <b>without asking</b>, then reboots. Intended for nodes "
                "left unattended; anything in progress on the device is lost at the "
                "reboot. Off by default. Automatic updates follow the Release Channel "
                "above, are skipped while the battery is low, and need WiFi connected. "
                "While this is set it supersedes <i>Check for Updates on Boot</i> — the "
                "device installs on its own shortly after booting rather than asking, "
                "so that setting has no effect until you turn this back off.</p>";
    }
    sectionEnd(html, lite);
    sendChunk(html);
#endif

    // Node Management. The archive checkbox is only meaningful where the node
    // table can actually spill somewhere: the board needs an SD slot AND a
    // mounted card, or (the CardputerZero) its own disk. Where it can't, the box
    // is rendered disabled with the reason shown, and handlePostSave leaves the
    // stored preference untouched so it survives until a card is available.
    {
        const bool slot    = nodeArchiveSlotExists();
        const bool canArch = slot && nodeArchiveAvailable();
        section(html, lite, "Node Management", "nodes", "Clean up the node list.");
        html += "<p class='hint'>"
                "The device keeps the ";
        html += String(MAX_NODES);
        html += " most recently heard nodes. When that fills up, the "
                "oldest non-favorite is dropped to make room — favorites are never "
                "dropped.</p>";

        html += "<label>"
                "<input type='checkbox' name='node_archive' value='1'";
        if (gCfg->nodeArchiveEnabled) html += " checked";
        if (!canArch) html += " disabled";
        html += ">"
#if NODE_ARCHIVE_ON_DISK
                "<span>Archive dropped nodes to storage</span></label>";
#else
                "<span>Archive dropped nodes to SD card</span></label>";
#endif

        html += "<p class='hint'>";
        if (!slot) {
            html += "Unavailable: this board has no SD card slot. Dropped nodes "
                    "are discarded.";
        } else if (!canArch) {
#if NODE_ARCHIVE_ON_DISK
            html += "Unavailable: the device's storage could not be opened. "
                    "Dropped nodes are discarded.";
#else
            html += "Unavailable: no SD card detected. Insert a card and reboot "
                    "to enable archiving.";
#endif
        } else {
            html += "Appends each dropped node to <code>";
#if defined(PLATFORM_LINUX)
            // Where the file really is on the Zero's disk, not the path inside
            // storageFs() that every other board shows.
            appendAttr(html, storageFs().hostPath(nodeArchiveFilePath()).c_str());
#else
            html += nodeArchiveFilePath();
#endif
            html += "</code> so its details are preserved after it leaves the "
                    "live list.";
        }
        html += "</p>";

        // Companion to the box above: whether the archive is also *shown*. Same
        // availability rule, and the same reason for it -- an unchecked checkbox
        // is not submitted, so saving from a card-less device must not be read
        // as the user turning this off.
        html += "<label>"
                "<input type='checkbox' name='node_archive_show' value='1'";
        if (gCfg->nodeArchiveShow) html += " checked";
        if (!canArch) html += " disabled";
        html += ">"
                "<span>Show archived nodes on the Nodes screen</span></label>";

        html += "<p class='hint'>";
        if (!slot) {
            html += "Unavailable: this board has no SD card slot.";
        } else if (!canArch) {
#if NODE_ARCHIVE_ON_DISK
            html += "Unavailable: the device's storage could not be opened.";
#else
            html += "Unavailable: no SD card detected.";
#endif
        } else {
            html += "Lists the most recently archived nodes below the live ones, "
                    "dimmed and marked <code>~</code>, and counts them in the "
                    "header total. They are history, "
                    "not reachable peers: messaging, favoriting and traceroute stay "
                    "disabled until a node is restored to the live list. Only the "
                    "newest ";
            html += String(NODE_ARCHIVE_INDEX_MAX);
            html += " are listed, so the screen opens at the same speed however "
                    "large the archive has grown.";
        }
        html += "</p>";

        // Auto-favorite. Radius is stored in meters but shown in whatever the
        // Units setting uses, so it reads naturally alongside the rest of the UI.
        const bool imperial = (gCfg->displayUnits != 0);
        const float shown = imperial ? (gCfg->autoFavoriteRangeM / 1609.344f)
                                     : (gCfg->autoFavoriteRangeM / 1000.0f);
        char rangeBuf[24];
        snprintf(rangeBuf, sizeof(rangeBuf), "%.2f", (double)shown);

        html += "<label>"
                "<input type='checkbox' name='autofav' value='1'";
        if (gCfg->autoFavoriteEnabled) html += " checked";
        html += ">"
                "<span>Auto-favorite nearby nodes</span></label>";

        html += "<label>Auto-favorite radius (";
        html += imperial ? "miles" : "km";
        html += ")<input name='autofav_range' type='number' min='0.05' max='500'"
                // 'any', not 0.05: the radius is stored in metres and shown
                // converted, and 1 km in miles (0.62) is not a 0.05 step --
                // the browser refused to submit the whole page over it.
                " step='any' value='";
        html += rangeBuf;
        html += "'></label>";

        html += "<p class='hint'>"
                "Any node reporting a position within this radius is favorited "
                "automatically. Favorites sort to the top of the node and DM lists "
                "and are never dropped when the node table fills up. This only ever "
                "<em>adds</em> favorites &mdash; a node moving out of range is never "
                "un-favorited, so it can't undo one you set by hand. Requires a known "
                "position for both your node and theirs.</p>";

        html += "<p><a href='/nodes.csv'"
                " style='display:inline-block;padding:.4em 1.2em;background:#3b82f6;"
                "color:#fff;border-radius:3px;text-decoration:none;font-size:.95em'>"
                "&#11015; Export Node List (CSV)</a></p>"
                "<p class='hint'>"
                "Downloads every node currently known to the device, plus any "
                "previously archived nodes if an archive exists. While wardriving "
                "is on, the <code>mapLat</code>/<code>mapLon</code> columns also "
                "fall back to where this device was when it heard a node best "
                "(<code>mapSource</code> says which); otherwise they hold only "
                "positions nodes report themselves.</p>";

        // ── Wardrive log ────────────────────────────────────────────────────
        {
            const bool wdOk = wardriveLogFilePath() && wardriveLogAvailable();
            html += "<label>"
                    "<input type='checkbox' name='wardrive_log' value='1'";
            if (gCfg->wardriveLogEnabled) html += " checked";
            if (!wdOk) html += " disabled";
            html += ">"
                    "<span>Wardriving (log sightings with this device's GPS position)</span></label>";
            html += "<p class='hint'>";
            if (!wardriveLogFilePath()) {
                html += "Unavailable: this board has no file storage.";
            } else if (!wdOk) {
                html += "Unavailable: no storage mounted. Insert a card and reboot.";
            } else {
                html += "Appends one line per node per 30&nbsp;s (or per 50&nbsp;m "
                        "moved) to <code>";
                html += wardriveLogFilePath();
                html += "</code>: time, RSSI/SNR, hops and your own GPS position. "
                        "Radio only, and only with a GPS fix. While on, the node "
                        "export (and the node archive, if enabled) also records "
                        "where you were when you heard each node &mdash; your own "
                        "location history, so mind who you share those files with. "
                        "The log stops at an eighth of the storage (64&nbsp;MB at "
                        "most); clear it to start again.";
                char st[384];
                snprintf(st, sizeof(st),
                         "<br><b>This session:</b> %lu line(s), %lu node(s), "
                         "%lu missed for want of a GPS fix, %lu dropped. GPS: %s%s",
                         (unsigned long)wardriveLogLines(),
                         (unsigned long)wardriveLogNodes(),
                         (unsigned long)wardriveLogSkippedNoFix(),
                         (unsigned long)wardriveLogDropped(),
                         gpsHasFix() ? "fix" : "<b class='danger-text'>NO FIX</b>",
                         wardriveLogIsFull()
                             ? "<br><b class='danger-text'>Log full: nothing more is "
                               "being written. Download it, then clear it.</b>"
                             : "");
                html += st;
            }
            html += "</p>";
            if (wdOk) {
                html += "<p><a href='/wardrive.csv'"
                        " style='display:inline-block;padding:.4em 1.2em;background:#3b82f6;"
                        "color:#fff;border-radius:3px;text-decoration:none;font-size:.95em'>"
                        "&#11015; Download Wardrive Log (CSV)</a></p>"
                        // This is inside the Save form, and forms cannot nest:
                        // a <form> here was ignored by the browser and its
                        // </form> closed the Save form instead, leaving Save All
                        // outside any form, where it did nothing. So the button
                        // names its own form, which is emitted after Save's.
                        "<p>"
                        "<button type='submit' form='wdclear' class='btn-danger'>"
                        "Clear Wardrive Log</button></p>";
            }
        }
        sectionEnd(html, lite);
    }

    // Web Config Access. Last section on the page by design: it governs who can
    // reach this page rather than how the device behaves, so it sits after
    // everything someone actually came here to change.
    section(html, lite, "Web Config Access", "access", "Who can open this page.");
    html += "<label>"
            "<input type='checkbox' name='web_auth' value='1' id='web-auth-cb'"
            " onchange=\"document.getElementById('web-auth-creds').style.display="
            "this.checked?'':'none'\"";
    if (gCfg->webCfgAuthEnabled) html += " checked";
    html += "> Require login (authentication)</label>";
    html += "<p class='hint'>When disabled, the web config is reachable without a "
            "username or password. Only leave this off on trusted networks.</p>";
    html += "<div id='web-auth-creds'";
    if (!gCfg->webCfgAuthEnabled) html += " style='display:none'";
    html += ">";
    html += "<label>Username<input type='text' value='admin' readonly></label>";
    html += "<label>Password (leave blank to keep current)"
        "<input name='web_pass' type='password' maxlength='63' autocomplete='new-password'"
        " placeholder='Enter new password'></label>";
    html += "</div>";
    sectionEnd(html, lite);
    sendChunk(html);

    // The full page saves from the save bar (kAppJs), which appears once a
    // field differs from what the page loaded with. Lite has no script, so it
    // keeps a plain button at the end of the form.
    if (lite) html += "<button type='submit'>Save All</button>";
    html += "</form>";
    // The Clear Wardrive Log button above submits this, by its form attribute.
    // Empty and invisible when there is no log to clear.
    html += "<form id='wdclear' method='POST' action='/clear-wardrive'"
            " onsubmit=\"return confirm('Delete the wardrive log file? Download it first if you need it.')\"></form>";
    // A field the browser rejects stops the submit, and if it sits in a closed
    // section the browser cannot show why: Save just does nothing. Open the
    // section first so the message has somewhere to appear.
    // Saving from the save bar while another tab is showing would leave the
    // browser nowhere to point at, so bring Config back first.
    html += "<script>document.addEventListener('invalid',function(e){"
            "if(window.switchTab&&document.body.getAttribute('data-tab')!=='config')switchTab('config');"
            "for(var d=e.target.parentNode;d;d=d.parentNode)"
            "if(d.tagName==='DETAILS')d.open=true;},true);</script>";
    sendChunk(html);

    if (lite) {
        // Restore, but not backup. Import streams the upload into a static
        // buffer and parses it on the stack, so it costs AP mode nothing;
        // export has to build the whole YAML in one String, which is the sort
        // of multi-KB contiguous allocation this mode cannot promise. Outside
        // the form above rather than inside it — a nested form would submit to
        // /save instead.
        html += "<hr style='margin:1.4em 0;border:0;border-top:1px solid var(--line)'>"
                "<h3 style='margin:.2em 0 .4em'>Restore Config</h3>"
                "<form method='POST' action='/import' enctype='multipart/form-data'"
                " onsubmit=\"return confirm('This will replace your settings with "
                "the uploaded file and reboot the device. Continue?')\">"
                "<label>Import a YAML config file"
                "<input type='file' name='f' accept='.yaml,.yml'></label>"
                "<button type='submit' style='width:100%;margin-top:.6em'>"
                "Upload &amp; Apply</button></form>"
                "<p class='hint'>Applies the file and reboots. "
                "Files over 8 KB are rejected rather than partly applied. Export is "
                "available once the device is on your WiFi.</p>";
#if HAS_WEB_FILES
        // The Cardputer's only page, so its Files section lives here, loaded
        // straight away rather than on a tab switch. Never in AP mode: that is
        // onboarding, and the routes are not registered for it.
        if (kLiteOnlyBoard && !gApMode && gCfg && cfgWebFilesEnabled(*gCfg)) {
            html += "<div id='tab-files' style='margin-top:1.4em'>";
            sendChunk(html);
            sendFlash(kFilesPane);
            html += "</div><script>filesOpen();</script>";
        }
#endif
        // No further tabs in AP mode — close the document.
        html += "</body></html>";
        sendChunk(html);
        if (gSendAborted) {
            // Never terminated the chunked stream — drop the socket so the
            // browser fails the load now instead of waiting on more chunks.
            server.client().stop();
            logWifiHeapDiag("lite page abandoned");
            return;
        }
        server.sendContent("");   // terminate chunked response
        logWifiHeapDiag("lite page sent");
        return;
    }

    // The forms the Saved Networks rows in the WiFi section submit through --
    // see appendSavedNetworks(). Here, after the config form has closed,
    // because a form cannot sit inside another one.
#if !defined(PLATFORM_LINUX)
    appendSavedNetworkForms(html);
#endif
    sendChunk(html);

    html += "</div></div><div class='tab-panel' id='tab-utils'><div class='tab-pane-center'>";

    // ── Diagnostics / Utilities ───────────────────────────────
    // Collapsible, matching the config tab. These pass `false` rather than
    // `lite` because the lite page returns above and never reaches this tab.
    section(html, false, "Diagnostics", "u-diag");
    html +=
        "<form method='POST' action='/announce'>"
        "<button type='submit' class='btn'>"
        "&#128225; Send NODEINFO Broadcast</button>"
        "</form>"
        "<p class='hint'>"
        "Forces immediate re-announcement to the mesh (NODEINFO + position).</p>";

    html +=
        "<form method='POST' action='/telemetry'>"
        "<button type='submit' class='btn'>"
        "&#128202; Send Telemetry Now</button>"
        "</form>"
        "<p class='hint'>"
        "Forces immediate telemetry TX (device + environment when available).</p>";

    html +=
        "<form method='POST' action='/cs-check'>"
        "<button type='submit' class='btn'>"
        "&#128260; Check for Messages Now</button>"
        "</form>"
        "<p class='hint'>"
        "Asks the chat server for messages missed on this node's channels, "
        "or searches for one if none has been found yet. "
        "At most once every 5 minutes.";
    if (webCfgChatServerResult()[0]) {
        html += "<br>Last attempt: ";
        appendAttr(html, webCfgChatServerResult());
    }
    html += "</p>";


#if HAS_SD_MALWARE_SCAN
    // Scanning changes nothing, so the scan button asks nothing. The delete
    // below it does, and only appears once there is something to delete.
    html +=
        "<form method='POST' action='/sd-scan'>"
        "<button type='submit' class='btn'>"
        "&#128269; Scan SD Card for Malware</button>"
        "</form>"
        "<p class='hint'>"
        "Looks through the card for Windows programs, scripts, shortcuts and "
        "autorun files &mdash; including a program wearing a harmless-looking "
        "name, which is caught by its header rather than its extension. None of "
        "it can run on this device; the risk is the next PC the card goes into. "
        "Camillia's own <code>/camillia</code> folder is skipped.";
    if (gSdScanResult[0]) {
        html += "<br>Last scan: ";
        html += gSdScanResult;
    }
    html += "</p>";

    if (gSdScanHits > 0) {
        html +=
            "<form method='POST' action='/sd-repair'"
            " onsubmit=\"return confirm('Delete the ";
        html += String((unsigned)gSdScanHits);
        html +=
            " file(s) the scan matched? Nothing else on the card is touched, and "
            "this cannot be undone.')\">"
            "<button type='submit' class='btn-danger'>"
            "&#128465; Delete Matched Files</button>"
            "</form>"
            "<p class='hint'>"
            "Re-walks the card and deletes only what it still recognises &mdash; it "
            "is handed no list, so nothing here can be pointed at map tiles, chat "
            "history or a backup. Reformatting the card is still the only thing "
            "that can promise it is clean.</p>";
    }
#endif

    sectionEnd(html, false);
    sendChunk(html);

#if HAS_ADMIN_TERMINAL
    // ── Remote Admin ──────────────────────────────────────────────
    // Peers are added and proved here rather than from a node's menu: a node
    // menu offering "claim admin over this stranger" invites unauthorized admin
    // packets across the mesh and the broker.
    section(html, false, "Remote Admin", "u-admin");
    html +=
        "<p class='hint'>"
        "Administer another Meshtastic node over LoRa or MQTT. The remote must "
        "carry this node's public key in its <code>security.admin_key</code> &mdash; "
        "copy it from the chip at the top of this page. Favourite nodes are "
        "probed once per boot, so anything that accepts us turns up here on its "
        "own; <em>Verify</em> asks again."
        "<br><span style='color:var(--good);font-weight:700'>(C)</span> confirmed &mdash; "
        "the remote answered and accepts us. "
        "<span style='color:var(--warn);font-weight:700'>(U)</span> unconfirmed &mdash; "
        "listed but not yet proved.</p>";

    // Above the table rather than in it. Verify asks one peer that is already
    // listed; this asks every favourite, which is the only way a node that was
    // off at boot -- and so was never listed at all -- gets in.
    html +=
        "<p style='margin:.1em 0 .9em'>"
        "<button type='button' onclick='adminRescan(this)'>Scan favourites</button> "
        "<span id='admin-rescan-msg' class='hint'></span>"
        "<br><span class='hint'>"
        "Asks every favourite node with a public key whether it accepts "
        "administration from this device, one every few seconds. Results appear "
        "in the table as they arrive &mdash; reload to see them. "
        "Available once every 30 seconds.</span></p>";

    // Only peers that are, or may still turn out to be, administerable. A peer
    // the remote has actually refused is dropped from the table rather than
    // shown greyed: this list answers "what can I administer", and a row that
    // can only ever say no is not part of that answer.
    {
        int shown = 0;
        String rows;
        for (int i = 0; i < AdminPeerList.count(); i++) {
            const AdminPeer *p2 = AdminPeerList.at(i);
            if (!p2 || p2->state == ADMIN_PEER_DENIED) continue;

            char idbuf[16];
            snprintf(idbuf, sizeof(idbuf), "!%08lx", (unsigned long)p2->nodeId);

            // The name, falling back the way the rest of the UI does: long,
            // then short, then the id -- which is all a node heard only as a
            // relayed packet ever gives us.
            const NodeEntry *n = Nodes.find(p2->nodeId);
            const char *name = nullptr;
            if (n) {
                if (n->longName[0])       name = n->longName;
                else if (n->shortName[0]) name = n->shortName;
            }

            const bool confirmed = (p2->state == ADMIN_PEER_CONFIRMED);
            rows += "<tr><td>";
            if (name) {
                // Set over the air by whoever owns that node, so it is escaped
                // rather than pasted into the markup.
                for (const char *c = name; *c; c++) {
                    switch (*c) {
                        case '<': rows += "&lt;";   break;
                        case '>': rows += "&gt;";   break;
                        case '&': rows += "&amp;";  break;
                        case '"': rows += "&quot;"; break;
                        case '\'': rows += "&#39;"; break;
                        default:  rows += *c;       break;
                    }
                }
            } else {
                rows += "<code>"; rows += idbuf; rows += "</code>";
            }
            rows += "</td><td><span style='font-weight:700;color:";
            rows += confirmed ? "#8ef2b8" : "#ffd181";
            rows += "' title='";
            rows += confirmed ? "Confirmed - the remote accepts administration from this device"
                              : "Unconfirmed - listed but not yet proved";
            rows += "'>(";
            rows += confirmed ? "C" : "U";
            rows += ")</span></td><td>";
            // When, not how. lastVerified is epoch seconds, written by confirm()
            // from the device clock -- which may not have been set at the time,
            // and is 0 for a peer that has never answered. Both read as "never"
            // rather than as 1970.
            if (p2->lastVerified >= 1700000000UL) {
                const time_t when = (time_t)p2->lastVerified;
                struct tm lt;
                char whenBuf[24] = "";
                if (localtime_r(&when, &lt)) {
                    strftime(whenBuf, sizeof(whenBuf), "%Y-%m-%d %H:%M", &lt);
                }
                rows += whenBuf[0] ? whenBuf : "never";
            } else {
                rows += "never";
            }
            rows += "</td><td align='right'>";
            // Offered on unproved peers too: an unproved peer is one nobody has
            // asked yet, and after a reboot that is every peer. The worst an
            // attempt costs is one refused packet to a node already on the list.
            rows += "<button type='button' onclick=\"adminOpen('";
            rows += idbuf;
            rows += "')\">Terminal</button> ";
            // A plain button, not a form: submitting would reload the page and
            // land back on the Config tab. The probe takes up to 30 s anyway, so
            // there is nothing for a reload to show even if it kept its place.
            rows += "<button type='button' onclick=\"adminVerify('";
            rows += idbuf;
            rows += "',this)\">Verify</button></td></tr>";
            shown++;
        }

        if (shown == 0) {
            html += "<p class='hint'>"
                    "Nothing to administer yet. Favourite a node that carries this "
                    "device's public key and it will be probed on the next boot.</p>";
        } else {
            html += "<table style='width:100%;font-size:.84em;border-collapse:collapse'>"
                    "<tr><th align='left'>Node</th><th align='left'></th>"
                    "<th align='left'>Last proved</th><th></th></tr>";
            html += rows;
            html += "</table>";
        }
    }
    sectionEnd(html, false);
    sendChunk(html);
#endif

#if HAS_STATE_MAPS
    // ── Maps Download ─────────────────────────────────────────────
    // Browser-seeded slippy tiles for offline Locate. The browser downloads one
    // center tile per positioned node at every zoom from 13 through the chosen
    // detail ceiling, deduplicates overlaps, and uploads the original PNGs.
    if (stateMapStorageReady()) {
        section(html, false, "Maps Download", "u-maps");
        html +=
            "<p class='hint'>"
            "Downloads offline map tiles centered on every positioned node in the database. "
            "Zoom 13 is always included. Higher detail adds every tile needed for a "
            "node-centered Locate viewport at each zoom through the selected maximum. "
            "Offline gaps between those node areas are expected; Locate "
            "fills and saves missing viewport tiles whenever internet access is available.</p>";
        html +=
            "<div id='mapdl-status' class='hint'>"
            "Checking maps stored on the device&hellip;</div>";
        html +=
            "<div style='display:flex;align-items:center;gap:.45em;margin:.1em 0 .15em'>"
            "<div id='mapdl-progress-wrap' role='progressbar' aria-label='Map download progress' "
            "aria-valuemin='0' aria-valuemax='100' aria-valuenow='0' "
            "style='flex:1 1 auto;height:10px;border:1px solid var(--line);border-radius:999px;background:var(--panel-3);overflow:hidden;margin:0'>"
            "<div id='mapdl-progress-bar' style='height:100%;width:0%;background:var(--accent);transition:width .2s ease'></div>"
            "</div>"
            "<button type='button' id='mapdl-stop' title='Stop current download' aria-label='Stop current download' disabled "
            "class='btn-danger' style='margin:0;min-width:30px;height:24px;padding:0 .5em;opacity:.6;cursor:default'>X</button>"
            "</div>"
            "<div id='mapdl-progress-text' style='font-size:.78em;color:var(--dim);margin:0 0 .5em'>0%</div>";
        html +=
            "<div style='display:flex;flex-direction:column;align-items:flex-start;gap:.45em'>"
            "<label style='font-size:.82em;color:var(--dim);display:flex;align-items:center;gap:.35em'>"
            "Detail"
            "<select id='mapdl-level' style='margin:0'>"
            "<option value='13'>Roads (zoom 13 only)</option>"
            "<option value='16' selected>Streets (zooms 13-16)</option>"
            "<option value='19'>Buildings (zooms 13-19)</option>"
            "</select></label>"
            "<button type='button' id='mapdl-btn' class='btn' disabled>"
            "&#128506; Download offline tiles for known nodes</button></div>"
            "<div id='mapdl-warn' style='font-size:.8em;color:var(--warn);margin:.45em 0 .15em;font-weight:600'>"
            "Detailed plans can take a very long time. Buildings may require up to 42 tile "
            "downloads per positioned node before overlap is removed. Keep this page open "
            "and the device connected for the entire run.</div>";
        html +=
            "<details id='mapdl-debug-wrap' style='margin:.55em 0 .8em'>"
            "<summary style='font-size:.82em;color:var(--dim);cursor:pointer'>"
            "Map download debug (<span id='mapdl-debug-count'>0</span>)</summary>"
            "<textarea id='mapdl-debug' readonly spellcheck='false' "
            "style='box-sizing:border-box;width:100%;height:170px;margin:.45em 0 .35em;"
            "font:11px/1.35 monospace;white-space:pre;resize:vertical'></textarea>"
            "<div style='display:flex;align-items:center;gap:.4em;flex-wrap:wrap'>"
            "<button type='button' id='mapdl-debug-copy' class='btn-2' style='margin:0'>Copy report</button>"
            "<button type='button' id='mapdl-debug-clear' class='btn-2' style='margin:0'>Clear</button>"
            "<span id='mapdl-debug-copy-status' class='hint'></span>"
            "</div></details>";
        html +=
            "<p class='hint'>"
            "Downloading writes to the card over the same SPI bus the radio uses, "
            "so expect the mesh to miss traffic while it runs. Tiles are based on "
            "OpenStreetMap data.</p>";
        sectionEnd(html, false);
        sendChunk(html);
    }
#endif

    section(html, false, "Debug Output", "u-debug");
    html +=
        "<form method='POST' action='/set-debug-monitor'>"
        "<label style='display:flex;align-items:center;gap:.5em'>"
        "<input type='checkbox' name='dbg_monitor' value='1'";
    if (monitorDebugEnabled()) html += " checked";
    html +=
        "> Enable serial debug output in pio device monitor</label>"
        "<button type='submit' class='btn'>Apply Debug Flag</button>"
        "</form>"
        "<p class='hint'>"
        "When disabled, debug logs are suppressed from serial monitor output.</p>";
    sectionEnd(html, false);
    sendChunk(html);

    section(html, false, "Software Update", "u-update");
    html +=
        "<p class='hint'>Current firmware: <b>";
    html += APP_VERSION;
    html +=
        "</b></p>";
    // Update-available check intentionally removed for now. The check plumbing
    // (queueReleaseCheckNow / runQueuedReleaseCheck / the /release-check route
    // and its client JS) remains dormant behind kWebDeviceReleaseCheckEnabled and
    // is no longer reachable from the UI.

    sectionEnd(html, false);
    sendChunk(html);

    section(html, false, "Backup &amp; Restore", "u-backup");
    html +=
        "<p><a href='/export'"
        " class='btn'>"
        "&#11015; Export Config</a></p>";
    {
        // Boards with a card slot: the same /camillia/config.yaml the
        // on-device Export writes, and Import on the device reads back.
        SdProbeStatus sd;
        sdGetProbeStatus(sd);
        if (sd.hasSlot) {
            html +=
                "<form method='POST' action='/export-sd'>"
                "<button type='submit' class='btn'>"
                "&#128190; Export Config to SD Card</button>"
                "</form>"
                "<p class='hint'>Writes <code>/camillia/config.yaml</code> to the "
                "card in the device, replacing any copy already there.</p>";
        }
    }
    html +=
        "<form method='POST' action='/import' enctype='multipart/form-data'"
        " onsubmit=\"return confirm('This will replace your settings with the "
        "uploaded file and reboot the device. Continue?')\""
        " style='margin-top:.6em'>"
        "<label>Import a YAML config file.</label>  "
        "<input type='file' name='f' accept='.yaml,.yml'"
        " style='margin-top:.3em'><br />"
        "<button type='submit'>&#11014; Upload &amp; Apply</button>"
        "</form>";
    sectionEnd(html, false);
    sendChunk(html);

    if (gOnScreenshotPng) {
        section(html, false, "Display Capture", "u-capture");
        html +=
            "<p><a href='/screenshot'"
            " class='btn'>"
            "&#128247; Capture &amp; Download PNG</a></p>"
            "<p class='hint'>"
            "Captures the current on-device screen and downloads it as a PNG file.</p>";
        sectionEnd(html, false);
        sendChunk(html);
    }

#if HAS_UI_COLOR_OPTIONS
    // Above the Danger Zone rather than inside it: it changes nothing but which
    // palette entry each node draws in, and nothing about it needs a confirm.
    section(html, false, "Chat Colors", "u-colors");
    html +=
        "<form method='POST' action='/reset-chat-colors'>"
        "<button type='submit'>&#127912; Reset Chat Colors</button>"
        "</form>"
        "<p class='hint'>"
        "Each node's chat color comes from its node ID, so two nodes can end up "
        "sharing one. This re-rolls every assignment at once &mdash; press again "
        "for another arrangement. Messages are not touched.</p>";
    sectionEnd(html, false);
    sendChunk(html);
#endif

    // Directly above the Danger Zone, which is where Clear Messages lives:
    // someone reading that button is exactly the person who wants this first.
    section(html, false, "Messages", "u-messages");
    html += "<p style='margin:.2em 0 1em'><a href='/messages.csv'"
            " class='btn'>"
            "&#11015; Export Messages (CSV)</a></p>"
            "<p class='hint'>"
            "Downloads every channel message and direct message the device still "
            "holds, one row each, with the channel or peer, timestamp, sender and "
            "delivery state. Long messages are joined back into one row. The file "
            "is sent to this browser only &mdash; nothing is written to the "
            "device's SD card. History is limited by what the device keeps in "
            "memory, so take a copy before clearing anything below.</p>";
    sectionEnd(html, false);
    sendChunk(html);

    // Written out rather than using section(): the helper has no way to carry
    // the red, and losing it would make the destructive actions read like any
    // other group. Collapsed like the rest, which also puts a deliberate click
    // in front of Factory Reset.
    html +=
        "<details><summary class='danger-text'>Danger Zone</summary>"
        "<form method='POST' action='/reboot'"
        " onsubmit=\"return confirm('This will restart the device. Continue?')\">"
        "<button type='submit' class='btn-danger'>"
        "Reboot Device</button>"
        "</form>"
        "<p class='hint'>"
        "Restarts the firmware. Nothing is erased and no settings change &mdash; "
        "messages, nodes and configuration are all kept. The device is "
        "unreachable for a few seconds while it comes back.</p>"
        "<form method='POST' action='/clear-messages'"
        " onsubmit=\"return confirm('This will clear all stored channel and DM messages. Continue?')\">"
        "<button type='submit' class='btn-danger'>"
        "Clear Messages</button>"
        "</form>"
        "<p class='hint'>"
        "Clears saved and in-memory chat/DM history without resetting device configuration.</p>"
        // One form, as on the device: the two boxes widen the clear, and
        // the confirm names what the ticked ones add.
        "<form method='POST' action='/clear-nodes'"
        " onsubmit=\"var f=this.favorites.checked,a=this.archive&&this.archive.checked;"
        "return confirm('This will clear all discovered nodes'+(f?' INCLUDING favorites':"
#if NODE_ARCHIVE_ON_DISK
        "' except favorites')+(a?' and every archived node':'')+"
#else
        "' except favorites')+(a?' and every archived node on the SD card':'')+"
#endif
        "', then reboot. Continue?')\">"
        "<label><input type='checkbox' name='favorites' value='1'> Include favorites</label>";
    if (nodeArchiveSlotExists()) {
        html += "<label><input type='checkbox' name='archive' value='1'>"
#if NODE_ARCHIVE_ON_DISK
                " Include archived nodes</label>";
#else
                " Include archived nodes (SD card)</label>";
#endif
    }
    html +=
        "<button type='submit' class='btn-danger'>"
        "Clear Nodes</button>"
        "</form>"
        "<p class='hint'>"
        "Drops every non-favorited node from the persisted database and reboots. "
        "Favorited nodes are kept, with their names, keys and positions, unless "
        "<b>Include favorites</b> is ticked. <b>Include archived nodes</b> also deletes "
#if NODE_ARCHIVE_ON_DISK
        "the archive of dropped nodes.</p>";
#else
        "the archive of dropped nodes on the SD card.</p>";
#endif

#if HAS_STATE_MAPS
    // Only where there is a cache to clear. Hidden on the same terms as the
    // Maps Download section above, so the two never disagree about whether this
    // device has maps at all.
    if (stateMapStorageReady()) {
        html +=
            "<form method='POST' action='/clear-maps'"
            " onsubmit=\"return confirm('This will delete every offline map tile and any legacy map files. "
            "Maps Download can rebuild node-centered coverage, and Locate will refill online gaps. Continue?')\">"
            "<button type='submit' class='btn-danger'>"
            "Clear Maps</button>"
            "</form>"
            "<p class='hint'>"
            "Deletes offline slippy tiles plus obsolete state/detail map files. Nothing else on "
            "the card is touched, and no node or message data is affected. Maps Download rebuilds "
            "the selected node-centered coverage; online Locate use fills additional gaps.</p>";
    }
#endif

    html +=
        "<form method='POST' action='/factory-reset'"
        " onsubmit=\"return confirm('This will erase ALL settings and reboot the device. Continue?')\">"
        "<button type='submit' class='btn-danger'>"
        "Factory Reset</button>"
        "</form>"
        "<p class='hint'>"
        "Erases all NVS configuration (node identity, channels, keys) and reboots."
        " The device will behave as if freshly flashed.</p>"
        "</details>";

    html += "</div></div><div class='tab-panel' id='tab-live'>";
    html += "<h3>Live RX/TX</h3>"
            "<p class='gps-hint'>Streams the Announcements feed from the device, similar to the on-device ANN view.</p>"
            "<div class='live-wrap'>"
            "<div class='live-toolbar'>"
            "<button type='button' class='map-mini-btn' onclick='clearLiveFeed()'>Clear View</button>"
            "<span id='live-status'>Paused</span>"
            "</div>"
            "<div id='live-feed' class='live-feed'></div>"
            "</div>"
            "<h3>Live Charts</h3>"
            "<p class='gps-hint'>Same data shown on-device under Live &rarr; Tools. Last 60 samples, newest on the right.</p>"
            "<div class='chart-wrap'>"
              "<div class='chart-box'>"
                "<div class='chart-head'><strong>Channel Utilization (%)</strong>"
                  "<span id='chart-chutil-stats' class='chart-stats'>--</span></div>"
                "<canvas id='chart-chutil' class='chart-canvas' width='600' height='180'></canvas>"
                "<div class='chart-legend'>"
                  "<span class='swatch sw-chutil'></span>ChUtil"
                  "&nbsp;&nbsp;<span class='swatch sw-airutil'></span>AirTx"
                "</div>"
              "</div>"
              "<div class='chart-box'>"
                "<div class='chart-head'><strong>SNR (dB) / RSSI (dBm)</strong>"
                  "<span id='chart-snr-stats' class='chart-stats'>--</span></div>"
                "<canvas id='chart-snr' class='chart-canvas' width='600' height='180'></canvas>"
                "<div class='chart-legend'>"
                  "<span class='swatch sw-snr'></span>SNR (left axis, dB)"
                  "&nbsp;&nbsp;<span class='swatch sw-rssi'></span>RSSI (right axis, dBm)"
                "</div>"
              "</div>"
            "</div>";

    // Chat tab is omitted on the no-PSRAM Cardputer: its chat/DM buffers are
    // freed while web config runs (see webCfgBegin), so the tab would show
    // nothing, and dropping it trims the page and the polling load.
#if !defined(DEVICE_CARDPUTER_LORA_HAT)
    html += "</div><div class='tab-panel' id='tab-chat'>";
    html += "<h3>Chat</h3>";
    html += "<label style='display:inline-flex;align-items:center;gap:.4em;margin:.2em 0'>"
            "<input type='checkbox' id='chat-live' checked onchange='chatLiveToggle()' style='width:auto'>"
            " Live updates</label>";
    html += "<p class='gps-hint'>Live channel and direct-message conversations. "
            "Send, reply, and react to messages over the mesh. Uncheck <b>Live updates</b> "
            "to stop background polling.</p>";
    html += "<div id='chat-body'>";
    html += "<label>Conversation<select id='chat-target' onchange='chatTargetChanged()'>"
            "<option value=''>-- select --</option></select></label>";
    html += "<div id='chat-feed' class='chat-feed'></div>";
    html += "<div id='chat-reply-bar' class='chat-reply' style='display:none'>"
            "<span id='chat-reply-text'></span>"
            "<button type='button' class='chat-mini' onclick='chatCancelReply()'>&times;</button></div>";
        html += "<div class='chat-compose'>"
            "<textarea id='chat-input' rows='3' maxlength='";
        html += String((unsigned)MESH_TEXT_MAX_LEN);
        html += "' placeholder='Message...' onkeydown='chatComposerKey(event)' oninput='chatUpdateCount()'></textarea>"
            "<div class='chat-compose-foot'><span id='chat-count' class='chat-char-count'>0/";
        html += String((unsigned)MESH_TEXT_MAX_LEN);
        html += "</span><button type='button' onclick='chatSend()'>Send</button></div></div>";
    html += "</div>";  // #chat-body
#endif
    html += "</div><div class='tab-panel' id='tab-map'>";
    html += "<h3>Nodes</h3>";
    html += "<p class='gps-hint'>Positioned nodes: ";
    snprintf(tmp, sizeof(tmp), "%d", mapPointCount);
    html += tmp;
    html += " / ";
    snprintf(tmp, sizeof(tmp), "%d", totalNodes);
    html += tmp;
    html += ". Heat intensity increases where multiple nodes overlap.</p>";
    if (totalNodes > 0) {
        // Two ways to pick a node, one result: the dropdown and a search match
        // both fill #node-info below and centre the map on the node.
        // Narrow the dropdown only; the search below always looks at every node.
        html += "<div class='node-filters'>"
                "<label><input type='checkbox' id='nf-known' onchange='nodeFilterChanged()'>Show only known</label>"
                "<label><input type='checkbox' id='nf-loc' onchange='nodeFilterChanged()'>Show only with location</label>"
                "<label><input type='checkbox' id='nf-fav' onchange='nodeFilterChanged()'>Show only favorites</label>"
                "</div>";
        html += "<p class='gps-hint' id='node-filter-count' style='margin:.1em 0 .2em'></p>";
        html += "<select id='node-select' onchange='selectNode()'>";
        html += "<option value=''>-- select a node --</option>";
        sendChunk(html);            // flush before streaming the option list
        streamNodeOptions();
        html += "</select>";
        // Narrows the dropdown as you type, lists the matches under it and
        // marks the positioned ones on the map.
        html += "<input type='search' id='node-search' autocomplete='off' spellcheck='false' "
                "placeholder='Search nodes by name or ID' style='margin-top:.5em' "
                "oninput='renderNodeOptions()' onkeydown='nodeSearchKey(event)'>";
        html += "<p class='gps-hint' id='node-search-count' style='margin:.25em 0 0'></p>";
        html += "<div id='node-search-results' class='node-results' style='display:none'></div>";
        html += "<div id='node-info' style='margin-top:.6em'></div>";
    } else {
        html += "<p class='gps-hint'>No nodes discovered yet.</p>";
    }
    html += "<div class='map-controls'>"
            "<button type='button' id='map-fit-btn' class='map-mini-btn' onclick='fitNodeMap()'>Fit Nodes</button>"
            "<button type='button' class='map-mini-btn' onclick='worldNodeMap()'>World View</button>"
            "<button type='button' id='map-heat-btn' class='map-mini-btn active' onclick='toggleNodeHeat()'>Heat: On</button>";
        html += "<button type='button' id='map-me-btn' class='map-mini-btn map-mini-right' onclick='centerOnMeMap()'";
        if (!mapHasMe) html += " disabled";
        html += ">ME</button></div>";
    html += "<div class='map-wrap'>"
            "<div id='node-heatmap' class='map-canvas'></div>"
            "<div class='map-legend'><span>Drag to pan, scroll/pinch to zoom</span><span id='map-status'></span></div>"
            "</div>";
    if (totalNodes > 0) {
        html += "<div id='node-detail-store' style='display:none'>";
        sendChunk(html);            // flush before streaming the detail panels
        streamNodeDetails();
        html += "</div>";
    }
    html += "<script>var NODE_HEAT_POINTS=";
    sendChunk(html);                // flush before streaming the points array
    streamMapPoints();
    html += ";var NODE_ME_POINT=";
    if (mapHasMe) {
        snprintf(tmp, sizeof(tmp), "{lat:%.7f,lon:%.7f}", mapMeLat, mapMeLon);
        html += tmp;
    } else {
        html += "null";
    }
    html += ";</script>";

#if HAS_STATE_MAPS
    // Maps Download status. Runs after NODE_HEAT_POINTS is defined above, and
    // no-ops when the section was not rendered (no storage on this device).
    //
    // The containing-state search deliberately mirrors nodesStateForCoords() in
    // the firmware, smallest-box tie-break included: if the two disagreed about
    // which state a node is in, this page would cache a map the device never
    // looks at.
#if 0
    // Legacy state/detail composite planner. Kept temporarily while old cache
    // cleanup ships, but excluded from both firmware and generated pages.
    html +=
        "<script>\n"
        "(function(){\n"
        "var el=document.getElementById('mapdl-status-legacy');\n"
        "if(!el)return;\n"
        "var btn=document.getElementById('mapdl-btn');\n"
        "var levelSel=document.getElementById('mapdl-level');\n"
        "var warn=document.getElementById('mapdl-warn');\n"
        "var progressWrap=document.getElementById('mapdl-progress-wrap');\n"
        "var progressBar=document.getElementById('mapdl-progress-bar');\n"
        "var progressText=document.getElementById('mapdl-progress-text');\n"
        "var stopBtn=document.getElementById('mapdl-stop');\n"
        "var TILE=256,ST=null,NEED_STATES=[],NEED_DETAILS=[];\n"
        "var mapRunActive=false,mapRunInterrupted=false,mapRunCancelRequested=false,mapRunStoppedByUser=false;\n"
        "var STATE_BY_CODE={},DETAIL_BY_KEY={},DETAIL_CACHE={};\n"
        "var DETAIL_LEVELS={\n"
        "  roads:{id:'roads',label:'Roads',lod:0,zoomBias:0,maxTiles:96,detailKb:170,tileSec:0.08},\n"
        "  streets:{id:'streets',label:'Streets',lod:1,zoomBias:1,maxTiles:196,detailKb:240,tileSec:0.11},\n"
        "  buildings:{id:'buildings',label:'Buildings',lod:2,zoomBias:2,maxTiles:324,detailKb:320,tileSec:0.16}\n"
        "};\n"
        "function levelCfg(){\n"
        "  var v=(levelSel&&levelSel.value)?levelSel.value:'streets';\n"
        "  return DETAIL_LEVELS[v]||DETAIL_LEVELS.streets;\n"
        "}\n"
        "function setProgress(frac,label){\n"
        "  if(!progressWrap||!progressBar||!progressText)return;\n"
        "  if(!isFinite(frac))frac=0;\n"
        "  if(frac<0)frac=0;\n"
        "  if(frac>1)frac=1;\n"
        "  var pct=Math.round(frac*100);\n"
        "  progressWrap.setAttribute('aria-valuenow',String(pct));\n"
        "  progressBar.style.width=String(pct)+'%';\n"
        "  progressText.textContent=label?(label+' ('+pct+'%)'):(String(pct)+'%');\n"
        "}\n"
        "function setStopEnabled(enabled){\n"
        "  if(!stopBtn)return;\n"
        "  stopBtn.disabled=!enabled;\n"
        "  stopBtn.style.opacity=enabled?'1':'0.6';\n"
        "  stopBtn.style.cursor=enabled?'pointer':'default';\n"
        "}\n"
        "if(stopBtn){\n"
        "  setStopEnabled(false);\n"
        "  stopBtn.onclick=function(){\n"
        "    if(!mapRunActive)return;\n"
        "    mapRunCancelRequested=true;\n"
        "    mapRunStoppedByUser=true;\n"
        "    mapRunInterrupted=true;\n"
        "    setStopEnabled(false);\n"
        "    say('Stopping map download...');\n"
        "  };\n"
        "}\n"
        "function netOffline(){\n"
        "  return (typeof navigator!=='undefined'&&navigator.onLine===false);\n"
        "}\n"
        "if(typeof window!=='undefined'&&window.addEventListener){\n"
        "  window.addEventListener('offline',function(){\n"
        "    if(mapRunActive)mapRunInterrupted=true;\n"
        "  });\n"
        "}\n"
        "function lon2px(lon,z){return (lon+180)/360*TILE*Math.pow(2,z);}\n"
        "function lat2px(lat,z){var s=Math.sin(lat*Math.PI/180);return (0.5-Math.log((1+s)/(1-s))/(4*Math.PI))*TILE*Math.pow(2,z);}\n"
        "function tileWindow(b,z){\n"
        "  var px0=lon2px(b.lonMin,z),px1=lon2px(b.lonMax,z);\n"
        "  var py0=lat2px(b.latMax,z),py1=lat2px(b.latMin,z);\n"
        "  var tx0=Math.floor(px0/TILE),tx1=Math.floor(px1/TILE);\n"
        "  var ty0=Math.floor(py0/TILE),ty1=Math.floor(py1/TILE);\n"
        "  return {z:z,px0:px0,px1:px1,py0:py0,py1:py1,tx0:tx0,tx1:tx1,ty0:ty0,ty1:ty1,tiles:(tx1-tx0+1)*(ty1-ty0+1)};\n"
        "}\n"
        "function pickPlan(b,W,H,zoomBias,maxTiles){\n"
        "  var base=16;\n"
        "  for(var z=2;z<=16;z++){\n"
        "    var w=lon2px(b.lonMax,z)-lon2px(b.lonMin,z);\n"
        "    var h=lat2px(b.latMin,z)-lat2px(b.latMax,z);\n"
        "    if(w>=W&&h>=H){base=z;break;}\n"
        "  }\n"
        "  var zSel=base+(zoomBias||0);\n"
        "  if(zSel<2)zSel=2;\n"
        "  if(zSel>19)zSel=19;\n"
        "  var cap=maxTiles||64;\n"
        "  var plan=tileWindow(b,zSel);\n"
        "  while(zSel>2&&plan.tiles>cap){zSel--;plan=tileWindow(b,zSel);}\n"
        "  return plan;\n"
        "}\n"
        "function loadTile(z,x,y){\n"
        "  return new Promise(function(res){\n"
        "    var img=new Image();img.crossOrigin='anonymous';\n"
        "    img.onload=function(){res(img);};img.onerror=function(){res(null);};\n"
        "    img.src='https://tile.openstreetmap.org/'+z+'/'+x+'/'+y+'.png';\n"
        "  });\n"
        "}\n"
        "async function buildPng(b,W,H,onTile,opts){\n"
        "  var plan=pickPlan(b,W,H,opts&&opts.zoomBias,opts&&opts.maxTiles);\n"
        "  var z=plan.z,px0=plan.px0,px1=plan.px1,py0=plan.py0,py1=plan.py1;\n"
        "  var tx0=plan.tx0,tx1=plan.tx1,ty0=plan.ty0,ty1=plan.ty1;\n"
        "  var mw=(tx1-tx0+1)*TILE,mh=(ty1-ty0+1)*TILE;\n"
        "  var mos=document.createElement('canvas');mos.width=mw;mos.height=mh;\n"
        "  var mc=mos.getContext('2d');mc.fillStyle='#e8eef7';mc.fillRect(0,0,mw,mh);\n"
        "  var total=(tx1-tx0+1)*(ty1-ty0+1),done=0,miss=0;\n"
        "  for(var ty=ty0;ty<=ty1;ty++){\n"
        "    for(var tx=tx0;tx<=tx1;tx++){\n"
        "      if(mapRunCancelRequested)throw new Error('user-cancel');\n"
        "      var img=await loadTile(z,tx,ty);\n"
        "      if(mapRunCancelRequested)throw new Error('user-cancel');\n"
        "      if(img){\n"
        "        mc.drawImage(img,(tx-tx0)*TILE,(ty-ty0)*TILE);\n"
        "      }else{\n"
        "        miss++;\n"
        "        if(netOffline())throw new Error('offline');\n"
        "      }\n"
        "      done++;if(onTile)onTile(done,total);\n"
        "    }\n"
        "  }\n"
        "  if(miss===total)throw new Error('tile-fetch-failed');\n"
        "  var out=document.createElement('canvas');out.width=W;out.height=H;\n"
        "  var oc=out.getContext('2d');oc.imageSmoothingEnabled=true;\n"
        "  var sx=px0-tx0*TILE,sw=px1-px0;\n"
        "  for(var dy=0;dy<H;dy++){\n"
        "    var latT=b.latMax-(b.latMax-b.latMin)*dy/H;\n"
        "    var latB=b.latMax-(b.latMax-b.latMin)*(dy+1)/H;\n"
        "    var syT=lat2px(latT,z)-ty0*TILE,syB=lat2px(latB,z)-ty0*TILE;\n"
        "    var sh=Math.max(1,syB-syT);\n"
        "    oc.drawImage(mos,sx,syT,sw,sh,0,dy,W,1);\n"
        "  }\n"
        "  return new Promise(function(res){out.toBlob(res,'image/png');});\n"
        "}\n"
        "function uploadState(s,blob){\n"
        "  var fd=new FormData();fd.append('file',blob,s.code+'.png');\n"
        "  var q='?latMin='+s.latMin+'&latMax='+s.latMax+'&lonMin='+s.lonMin+'&lonMax='+s.lonMax;\n"
        "  return fetch('/state-map-upload'+q,{method:'POST',body:fd}).then(function(r){return r.json();});\n"
        "}\n"
        "function uploadDetail(c,blob,lod){\n"
        "  var fd=new FormData();fd.append('file',blob,c.key+'.png');\n"
        "  return fetch('/detail-map-upload?lod='+encodeURIComponent(lod),{method:'POST',body:fd}).then(function(r){return r.json();});\n"
        "}\n"
        "function say(t){el.textContent=t;}\n"
        "function freshDim(item,w,h){return !!(item&&item.w==w&&item.h==h);}\n"
        "function detailCacheFresh(key,cfg,w,h){\n"
        "  var c=DETAIL_CACHE[key];\n"
        "  if(!c||!freshDim(c,w,h))return false;\n"
        "  var lod=(typeof c.lod==='number')?c.lod:1;\n"
        "  return lod===cfg.lod;\n"
        "}\n"
        "function cellForPoint(p,cellDeg){\n"
        "  if(!p||!isFinite(p.lat)||!isFinite(p.lon))return null;\n"
        "  var lat=p.lat,lon=p.lon;\n"
        "  if(lat<-90||lat>90)return null;\n"
        "  if(lat>=90)lat=89.9999;\n"
        "  lon=((lon+180)%360+360)%360-180;\n"
        "  if(lon>=180)lon=179.9999;\n"
        "  var lat10=Math.floor(lat*10),lon10=Math.floor(lon*10);\n"
        "  if(lat10<-900||lat10>899||lon10<-1800||lon10>1799)return null;\n"
        "  var key=(lat10<0?'S':'N')+String(Math.abs(lat10)).padStart(4,'0')+\n"
        "          (lon10<0?'W':'E')+String(Math.abs(lon10)).padStart(4,'0');\n"
        "  var latMin=lat10/10,lonMin=lon10/10;\n"
        "  return {key:key,latMin:latMin,latMax:latMin+cellDeg,lonMin:lonMin,lonMax:lonMin+cellDeg};\n"
        "}\n"
        "function stateForPoint(p,states){\n"
        "  var best=null,ba=1e9;\n"
        "  states.forEach(function(s){\n"
        "    if(p.lat<s.latMin||p.lat>s.latMax||p.lon<s.lonMin||p.lon>s.lonMax)return;\n"
        "    var a=(s.latMax-s.latMin)*(s.lonMax-s.lonMin);\n"
        "    if(!best||a<ba){best=s;ba=a;}\n"
        "  });\n"
        "  return best;\n"
        "}\n"
        "function formatDuration(sec){\n"
        "  if(sec<50)return '<1 min';\n"
        "  var m=Math.round(sec/60);\n"
        "  if(m<120)return String(m)+' min';\n"
        "  var h=(m/60).toFixed(1);\n"
        "  return h+' h';\n"
        "}\n"
        "function formatMb(kb){\n"
        "  var mb=kb/1024;\n"
        "  return (mb<1?mb.toFixed(1):mb.toFixed(0))+' MB';\n"
        "}\n"
        "function estimatePlan(cfg){\n"
        "  if(!ST)return {sec:0,kb:0,tiles:0};\n"
        "  var stateTiles=0,detailTiles=0;\n"
        "  for(var i=0;i<NEED_STATES.length;i++){\n"
        "    stateTiles+=pickPlan(NEED_STATES[i],ST.imageW,ST.imageH,0,64).tiles;\n"
        "  }\n"
        "  var detailW=(ST.detail&&ST.detail.imageW)||ST.imageW;\n"
        "  var detailH=(ST.detail&&ST.detail.imageH)||ST.imageH;\n"
        "  for(var j=0;j<NEED_DETAILS.length;j++){\n"
        "    detailTiles+=pickPlan(NEED_DETAILS[j],detailW,detailH,cfg.zoomBias,cfg.maxTiles).tiles;\n"
        "  }\n"
        "  var kb=NEED_STATES.length*220+NEED_DETAILS.length*cfg.detailKb;\n"
        "  var sec=stateTiles*0.09+detailTiles*cfg.tileSec+(NEED_STATES.length+NEED_DETAILS.length)*0.4;\n"
        "  return {sec:sec,kb:kb,tiles:stateTiles+detailTiles};\n"
        "}\n"
        "function refreshPlanStatus(prefix){\n"
        "  if(!ST)return;\n"
        "  var cfg=levelCfg();\n"
        "  var stateCodes=Object.keys(STATE_BY_CODE).sort();\n"
        "  NEED_STATES=stateCodes.filter(function(c){\n"
        "    var s=STATE_BY_CODE[c];\n"
        "    return !(s.cached&&freshDim(s,ST.imageW,ST.imageH));\n"
        "  }).map(function(c){return STATE_BY_CODE[c];});\n"
        "  var staleStates=stateCodes.filter(function(c){\n"
        "    var s=STATE_BY_CODE[c];\n"
        "    return s.cached&&!freshDim(s,ST.imageW,ST.imageH);\n"
        "  }).length;\n"

        "  var detailKeys=Object.keys(DETAIL_BY_KEY).sort();\n"
        "  var detailW=(ST.detail&&ST.detail.imageW)||ST.imageW;\n"
        "  var detailH=(ST.detail&&ST.detail.imageH)||ST.imageH;\n"
        "  NEED_DETAILS=detailKeys.filter(function(k){\n"
        "    return !detailCacheFresh(k,cfg,detailW,detailH);\n"
        "  }).map(function(k){return DETAIL_BY_KEY[k];});\n"
        "  var staleDetails=detailKeys.filter(function(k){\n"
        "    var c=DETAIL_CACHE[k];\n"
        "    if(!c)return false;\n"
        "    if(!freshDim(c,detailW,detailH))return true;\n"
        "    var lod=(typeof c.lod==='number')?c.lod:1;\n"
        "    return lod!==cfg.lod;\n"
        "  }).length;\n"

        "  var parts=[];\n"
        "  if(stateCodes.length){\n"
        "    parts.push(stateCodes.length+' state'+(stateCodes.length==1?'':'s')+' from node positions'+\n"
        "      (NEED_STATES.length?(' ('+NEED_STATES.length+' pending'+(staleStates?' incl '+staleStates+' refresh':'')+')'):' (cached)'));\n"
        "  }else{\n"
        "    parts.push('No US state maps needed from current node positions');\n"
        "  }\n"
        "  if(detailKeys.length){\n"
        "    parts.push(detailKeys.length+' detail cell'+(detailKeys.length==1?'':'s')+' from nodes + your position at '+cfg.label.toLowerCase()+' detail'+\n"
        "      (NEED_DETAILS.length?(' ('+NEED_DETAILS.length+' pending'+(staleDetails?' incl '+staleDetails+' refresh':'')+')'):' (cached)'));\n"
        "  }else{\n"
        "    parts.push('No positioned nodes or local position for detail cells yet');\n"
        "  }\n"
        "  var pending=NEED_STATES.length+NEED_DETAILS.length;\n"
        "  parts.push(pending?pending+' total map item'+(pending==1?'':'s')+' to download.':'All required maps already cached.');\n"
        "  var status=parts.join('. ');\n"
        "  say(prefix?prefix+' '+status:status);\n"
        "  if(!mapRunActive){\n"
        "    setProgress(pending?0:1,pending?'Ready':'Cached');\n"
        "    setStopEnabled(false);\n"
        "  }\n"

        "  if(warn){\n"
        "    var est=estimatePlan(cfg);\n"
        "    if(pending){\n"
        "      var txt='Estimate at '+cfg.label+' detail: about '+formatDuration(est.sec)+', '+formatMb(est.kb)+' on card, and roughly '+est.tiles+' tile fetches.';\n"
        "      if(cfg.id==='buildings')txt+=' Building-level downloads can take a long time on larger node sets.';\n"
        "      warn.textContent=txt;\n"
        "    }else{\n"
        "      warn.textContent='Estimate at '+cfg.label+' detail: no downloads pending.';\n"
        "    }\n"
        "  }\n"

        "  if(btn){\n"
        "    btn.disabled=!pending;\n"
        "    btn.title=pending?'Downloads '+NEED_STATES.length+' state and '+NEED_DETAILS.length+' detail map item'+(pending==1?'':'s')+' at '+cfg.label+' detail':'Everything required is already cached at the selected detail';\n"
        "    btn.onclick=run;\n"
        "  }\n"
        "}\n"
        "async function run(){\n"
        "  if(!btn||!ST||mapRunActive)return;\n"
        "  mapRunActive=true;\n"
        "  mapRunInterrupted=false;\n"
        "  mapRunCancelRequested=false;\n"
        "  mapRunStoppedByUser=false;\n"
        "  var cfg=levelCfg();\n"
        "  refreshPlanStatus();\n"
        "  var stateJobs=NEED_STATES.slice(0);\n"
        "  var detailJobs=NEED_DETAILS.slice(0);\n"
        "  var total=stateJobs.length+detailJobs.length;\n"
        "  if(!total){mapRunActive=false;setStopEnabled(false);return;}\n"
        "  if(netOffline())mapRunInterrupted=true;\n"
        "  btn.disabled=true;\n"
        "  if(levelSel)levelSel.disabled=true;\n"
        "  setStopEnabled(true);\n"
        "  var okState=0,failState=0,okDetail=0,failDetail=0;\n"
        "  var detailW=(ST.detail&&ST.detail.imageW)||ST.imageW;\n"
        "  var detailH=(ST.detail&&ST.detail.imageH)||ST.imageH;\n"
        "  var totalTiles=0;\n"
        "  for(var tsi=0;tsi<stateJobs.length;tsi++)totalTiles+=pickPlan(stateJobs[tsi],ST.imageW,ST.imageH,0,64).tiles;\n"
        "  for(var tdi=0;tdi<detailJobs.length;tdi++)totalTiles+=pickPlan(detailJobs[tdi],detailW,detailH,cfg.zoomBias,cfg.maxTiles).tiles;\n"
        "  var totalSteps=totalTiles+total;\n"
        "  if(totalSteps<1)totalSteps=1;\n"
        "  var doneSteps=0;\n"
        "  setProgress(0,'Starting');\n"
        "  for(var i=0;i<stateJobs.length;i++){\n"
        "    if(mapRunInterrupted||mapRunCancelRequested||netOffline()){mapRunInterrupted=true;break;}\n"
        "    var s=stateJobs[i];\n"
        "    var idx=i+1;\n"
        "    var st=pickPlan(s,ST.imageW,ST.imageH,0,64).tiles;\n"
        "    if(st<1)st=1;\n"
        "    try{\n"
        "      var blob=await buildPng(s,ST.imageW,ST.imageH,function(d,t){var td=d;if(td<0)td=0;if(td>st)td=st;setProgress((doneSteps+td)/totalSteps,'Downloading '+s.name);say('['+idx+'/'+total+'] '+s.name+': tile '+d+' of '+t);},{zoomBias:0,maxTiles:64});\n"
        "      if(mapRunInterrupted||mapRunCancelRequested||netOffline()){mapRunInterrupted=true;break;}\n"
        "      doneSteps+=st;\n"
        "      setProgress(doneSteps/totalSteps,'Uploading '+s.name);\n"
        "      say('['+idx+'/'+total+'] '+s.name+': uploading '+Math.round(blob.size/1024)+' KB');\n"
        "      var r=await uploadState(s,blob);\n"
        "      doneSteps++;\n"
        "      if(r&&r.ok){okState++;s.cached=true;s.w=ST.imageW;s.h=ST.imageH;setProgress(doneSteps/totalSteps,'Saved '+s.name);}else{failState++;setProgress(doneSteps/totalSteps,'Interrupted on '+s.name);mapRunInterrupted=true;break;}\n"
        "    }catch(e){\n"
        "      if(mapRunCancelRequested){mapRunInterrupted=true;mapRunStoppedByUser=true;break;}\n"
        "      failState++;\n"
        "      setProgress(doneSteps/totalSteps,'Interrupted on '+s.name);\n"
        "      mapRunInterrupted=true;\n"
        "      break;\n"
        "    }\n"
        "  }\n"
        "  for(var j=0;j<detailJobs.length;j++){\n"
        "    if(mapRunInterrupted||mapRunCancelRequested||netOffline()){mapRunInterrupted=true;break;}\n"
        "    var c=detailJobs[j];\n"
        "    var jdx=stateJobs.length+j+1;\n"
        "    var dt=pickPlan(c,detailW,detailH,cfg.zoomBias,cfg.maxTiles).tiles;\n"
        "    if(dt<1)dt=1;\n"
        "    try{\n"
        "      var dblob=await buildPng(c,detailW,detailH,function(d,t){var td=d;if(td<0)td=0;if(td>dt)td=dt;setProgress((doneSteps+td)/totalSteps,'Downloading '+c.key);say('['+jdx+'/'+total+'] '+c.key+': tile '+d+' of '+t);},{zoomBias:cfg.zoomBias,maxTiles:cfg.maxTiles});\n"
        "      if(mapRunInterrupted||mapRunCancelRequested||netOffline()){mapRunInterrupted=true;break;}\n"
        "      doneSteps+=dt;\n"
        "      setProgress(doneSteps/totalSteps,'Uploading '+c.key);\n"
        "      say('['+jdx+'/'+total+'] '+c.key+': uploading '+Math.round(dblob.size/1024)+' KB');\n"
        "      var dr=await uploadDetail(c,dblob,cfg.lod);\n"
        "      doneSteps++;\n"
        "      if(dr&&dr.ok){okDetail++;DETAIL_CACHE[c.key]={key:c.key,w:detailW,h:detailH,lod:cfg.lod};setProgress(doneSteps/totalSteps,'Saved '+c.key);}else{failDetail++;setProgress(doneSteps/totalSteps,'Interrupted on '+c.key);mapRunInterrupted=true;break;}\n"
        "    }catch(e){\n"
        "      if(mapRunCancelRequested){mapRunInterrupted=true;mapRunStoppedByUser=true;break;}\n"
        "      failDetail++;\n"
        "      setProgress(doneSteps/totalSteps,'Interrupted on '+c.key);\n"
        "      mapRunInterrupted=true;\n"
        "      break;\n"
        "    }\n"
        "  }\n"
        "  var okN=okState+okDetail,failN=failState+failDetail;\n"
        "  mapRunActive=false;\n"
        "  btn.disabled=false;\n"
        "  if(levelSel)levelSel.disabled=false;\n"
        "  setStopEnabled(false);\n"
        "  if(mapRunStoppedByUser){\n"
        "    setProgress(doneSteps/totalSteps,'Stopped');\n"
        "    refreshPlanStatus('Stopped by user. '+okN+' saved'+(failN?', '+failN+' failed':'')+'. Press Download maps to continue.');\n"
        "  }else if(mapRunInterrupted){\n"
        "    setProgress(doneSteps/totalSteps,'Interrupted');\n"
        "    refreshPlanStatus('Interrupted. '+okN+' saved'+(failN?', '+failN+' failed':'')+'. Reconnect, then press Download maps to continue.');\n"
        "  }else{\n"
        "    setProgress(1,'Complete');\n"
        "    refreshPlanStatus('Done. '+okN+' saved'+(failN?', '+failN+' failed':'')+'.');\n"
        "  }\n"
        "}\n"
        "fetch('/state-map-status').then(function(r){return r.json();}).then(function(d){\n"
        "  ST=d;\n"
        "  if(!d||!d.storage){say('No storage detected on the device.');if(warn)warn.textContent='';return;}\n"
        "  var pts=(window.NODE_HEAT_POINTS||[]);\n"
        "  var me=(window.NODE_ME_POINT||null);\n"
        "  STATE_BY_CODE={};DETAIL_BY_KEY={};DETAIL_CACHE={};\n"
        "  pts.forEach(function(p){var s=stateForPoint(p,d.states||[]);if(s)STATE_BY_CODE[s.code]=s;});\n"
        "  var detail=(d.detail||{});\n"
        "  var cellDeg=(detail.cellDeg||0.1);\n"
        "  pts.forEach(function(p){var c=cellForPoint(p,cellDeg);if(c)DETAIL_BY_KEY[c.key]=c;});\n"
        "  if(me){var mc=cellForPoint(me,cellDeg);if(mc)DETAIL_BY_KEY[mc.key]=mc;}\n"
        "  (detail.cached||[]).forEach(function(c){if(c&&c.key)DETAIL_CACHE[c.key]=c;});\n"
        "  if(levelSel)levelSel.onchange=function(){refreshPlanStatus();};\n"
        "  refreshPlanStatus();\n"
        "}).catch(function(){say('Could not read map cache status.');setProgress(0,'Unavailable');if(warn)warn.textContent='';});\n"
        "})();\n"
        "</script>";
#endif
    sendChunk(html);

    html +=
        "<script>\n"
        "(function(){\n"
        "var statusEl=document.getElementById('mapdl-status');\n"
        "if(!statusEl)return;\n"
        "var button=document.getElementById('mapdl-btn');\n"
        "var level=document.getElementById('mapdl-level');\n"
        "var warning=document.getElementById('mapdl-warn');\n"
        "var progressWrap=document.getElementById('mapdl-progress-wrap');\n"
        "var progressBar=document.getElementById('mapdl-progress-bar');\n"
        "var progressText=document.getElementById('mapdl-progress-text');\n"
        "var stopButton=document.getElementById('mapdl-stop');\n"
        "var debugBox=document.getElementById('mapdl-debug');\n"
        "var debugCount=document.getElementById('mapdl-debug-count');\n"
        "var debugCopy=document.getElementById('mapdl-debug-copy');\n"
        "var debugClear=document.getElementById('mapdl-debug-clear');\n"
        "var debugCopyStatus=document.getElementById('mapdl-debug-copy-status');\n"
        "var active=false,cancelRequested=false,plan=[],pending=[],refreshToken=0;\n"
        "var MAX_TILE_ATTEMPTS=3,UNCONFIRMED_STREAK_LIMIT=5;\n"
        "var MAP_DEBUG_MAX=120,MAP_DEBUG_FW='" APP_VERSION "';\n"
        "var mapDebugStarted=Date.now(),mapDebugEvents=[];\n"
        "var mapDebugMeta={event:'map-debug',firmware:MAP_DEBUG_FW,started:new Date().toISOString(),browser:navigator.userAgent,online:navigator.onLine};\n"
        "var ROAD_Z=13,STATUS_BATCH=8,TILE=256,VIEW_W=282,VIEW_H=188;\n"
        "function say(text){statusEl.textContent=text;}\n"
        "function waitMs(ms){return new Promise(function(resolve){setTimeout(resolve,ms);});}\n"
        "function mapDebugText(){return JSON.stringify(mapDebugMeta)+'\\n'+mapDebugEvents.join('\\n');}\n"
        "function renderMapDebug(){\n"
        "  if(debugBox){debugBox.value=mapDebugText();debugBox.scrollTop=debugBox.scrollHeight;}\n"
        "  if(debugCount)debugCount.textContent=String(mapDebugEvents.length);\n"
        "}\n"
        "function appendMapDebug(event,fields){\n"
        "  var entry={event:event,at:new Date().toISOString(),elapsedMs:Date.now()-mapDebugStarted};\n"
        "  if(fields)for(var key in fields)if(Object.prototype.hasOwnProperty.call(fields,key))entry[key]=fields[key];\n"
        "  mapDebugEvents.push(JSON.stringify(entry));\n"
        "  if(mapDebugEvents.length>MAP_DEBUG_MAX)mapDebugEvents.splice(0,mapDebugEvents.length-MAP_DEBUG_MAX);\n"
        "  renderMapDebug();\n"
        "}\n"
        "function resetMapDebug(reason){\n"
        "  mapDebugStarted=Date.now();mapDebugEvents=[];\n"
        "  mapDebugMeta={event:'map-debug',firmware:MAP_DEBUG_FW,started:new Date().toISOString(),browser:navigator.userAgent,online:navigator.onLine,reason:reason};\n"
        "  if(debugCopyStatus)debugCopyStatus.textContent='';renderMapDebug();\n"
        "}\n"
        "function showCopyResult(text){if(debugCopyStatus){debugCopyStatus.textContent=text;setTimeout(function(){debugCopyStatus.textContent='';},2500);}}\n"
        "if(debugCopy)debugCopy.onclick=function(){\n"
        "  var text=mapDebugText();\n"
        "  if(navigator.clipboard&&window.isSecureContext){\n"
        "    navigator.clipboard.writeText(text).then(function(){showCopyResult('Copied');},function(){showCopyResult('Copy failed; select the report above');});\n"
        "  }else if(debugBox){\n"
        "    debugBox.focus();debugBox.select();\n"
        "    try{showCopyResult(document.execCommand('copy')?'Copied':'Copy failed; report selected');}catch(ignore){showCopyResult('Copy failed; report selected');}\n"
        "  }\n"
        "};\n"
        "if(debugClear)debugClear.onclick=function(){resetMapDebug('manual-clear');};\n"
        "function setProgress(done,total,label){\n"
        "  var fraction=total>0?done/total:0;if(fraction<0)fraction=0;if(fraction>1)fraction=1;\n"
        "  var percent=Math.round(fraction*100);\n"
        "  progressWrap.setAttribute('aria-valuenow',String(percent));\n"
        "  progressBar.style.width=String(percent)+'%';\n"
        "  progressText.textContent=(label?label+' ':'')+'('+percent+'%)';\n"
        "}\n"
        "function setStop(enabled){\n"
        "  stopButton.disabled=!enabled;stopButton.style.opacity=enabled?'1':'0.6';\n"
        "  stopButton.style.cursor=enabled?'pointer':'default';\n"
        "}\n"
        "function selectedMaxZoom(){var value=parseInt(level.value||'16',10);return value===13||value===16||value===19?value:16;}\n"
        "function tilesForPoint(point,zoom){\n"
        "  if(!point||!isFinite(point.lat)||!isFinite(point.lon))return [];\n"
        "  var lat=Number(point.lat),lon=Number(point.lon);\n"
        "  if((lat===0&&lon===0)||lat<-90||lat>90)return [];\n"
        "  if(lat>85.05112878)lat=85.05112878;if(lat<-85.05112878)lat=-85.05112878;\n"
        "  lon=((lon+180)%360+360)%360-180;\n"
        "  var count=Math.pow(2,zoom),world=TILE*count,worldX=(lon+180)/360*world;\n"
        "  var sinLat=Math.sin(lat*Math.PI/180);\n"
        "  var worldY=(0.5-Math.log((1+sinLat)/(1-sinLat))/(4*Math.PI))*world;\n"
        "  var x0=Math.floor((worldX-VIEW_W/2)/TILE),x1=Math.floor((worldX+VIEW_W/2-0.001)/TILE);\n"
        "  var y0=Math.floor((worldY-VIEW_H/2)/TILE),y1=Math.floor((worldY+VIEW_H/2-0.001)/TILE);\n"
        "  var tiles=[];\n"
        "  for(var rawY=y0;rawY<=y1;rawY++){if(rawY<0||rawY>=count)continue;\n"
        "    for(var rawX=x0;rawX<=x1;rawX++){var x=((rawX%count)+count)%count;\n"
        "      tiles.push({z:zoom,x:x,y:rawY,key:zoom+'_'+x+'_'+rawY+'.png',cached:false});\n"
        "    }\n"
        "  }\n"
        "  return tiles;\n"
        "}\n"
        "function buildPlan(){\n"
        "  var points=window.NODE_HEAT_POINTS||[],maxZoom=selectedMaxZoom(),byKey={};\n"
        "  for(var i=0;i<points.length;i++){\n"
        "    for(var zoom=ROAD_Z;zoom<=maxZoom;zoom++){\n"
        "      var tiles=tilesForPoint(points[i],zoom);\n"
        "      for(var t=0;t<tiles.length;t++)byKey[tiles[t].key]=tiles[t];\n"
        "    }\n"
        "  }\n"
        "  return Object.keys(byKey).map(function(key){return byKey[key];}).sort(function(a,b){\n"
        "    return a.z-b.z||a.x-b.x||a.y-b.y;\n"
        "  });\n"
        "}\n"
        "async function markCached(tiles){\n"
        "  var storage=true;\n"
        "  for(var offset=0;offset<tiles.length;offset+=STATUS_BATCH){\n"
        "    var batch=tiles.slice(offset,offset+STATUS_BATCH);\n"
        "    var keys=batch.map(function(tile){return tile.key;}).join(',');\n"
        "    var response=await fetch('/map-tile-status?keys='+encodeURIComponent(keys),{cache:'no-store'});\n"
        "    var data=await response.json();\n"
        "    if(!response.ok||!data)throw new Error((data&&data.error)||'status failed');\n"
        "    storage=storage&&data.storage!==false;\n"
        "    var bits=String(data.bits||'');\n"
        "    for(var i=0;i<batch.length;i++)batch[i].cached=bits.charAt(i)==='1';\n"
        "  }\n"
        "  return storage;\n"
        "}\n"
        "async function verifySaved(tile){\n"
        "  tile.cached=false;\n"
        "  var storage=await markCached([tile]);\n"
        "  if(!storage)throw new Error('device storage unavailable');\n"
        "  return tile.cached;\n"
        "}\n"
        "function durationText(tileCount){\n"
        "  var minutes=Math.max(1,Math.ceil(tileCount*1.5/60));\n"
        "  if(minutes<60)return 'at least about '+minutes+' minute'+(minutes===1?'':'s');\n"
        "  return 'at least about '+(minutes/60).toFixed(1)+' hours';\n"
        "}\n";
    sendChunk(html);

    html +=
        "function renderPlan(prefix){\n"
        "    pending=plan.filter(function(tile){return !tile.cached;});\n"
        "    var cached=plan.length-pending.length,maxZoom=selectedMaxZoom();\n"
        "    mapDebugMeta.maxZoom=maxZoom;mapDebugMeta.planTiles=plan.length;mapDebugMeta.cachedTiles=cached;mapDebugMeta.pendingTiles=pending.length;renderMapDebug();\n"
        "    var summary=plan.length+' planned tile'+(plan.length===1?'':'s')+' through zoom '+maxZoom+\n"
        "      ': '+cached+' stored on the device, '+pending.length+' remaining.';\n"
        "    say(prefix?prefix+' '+summary:summary);\n"
        "    setProgress(cached,plan.length,pending.length?'Ready':'All stored');\n"
        "    if(!pending.length){\n"
        "      warning.textContent='All planned tiles through zoom '+maxZoom+' are stored on the device.';warning.style.color='#2f7652';\n"
        "    }else if(maxZoom>=19){\n"
        "      warning.textContent='WARNING: Buildings detail can take a very, very long time. '+pending.length+\n"
        "        ' remaining tile'+(pending.length===1?'':'s')+' may take '+durationText(pending.length)+\n"
        "        ', and the page must remain open for the entire run.';warning.style.color='#a33a18';\n"
        "    }else if(maxZoom>=16){\n"
        "      warning.textContent='Streets detail can take a long time on a large node database. '+pending.length+\n"
        "        ' tile'+(pending.length===1?'':'s')+' remain; allow '+durationText(pending.length)+'.';warning.style.color='#9a4f00';\n"
        "    }else{\n"
        "      warning.textContent='Roads downloads the zoom-13 tiles covering each node-centered Locate viewport after overlap is removed.';warning.style.color='#68788a';\n"
        "    }\n"
        "    button.disabled=!pending.length;\n"
        "}\n"
        "async function refreshPlan(prefix){\n"
        "  var token=++refreshToken;button.disabled=true;say('Checking maps stored on the device...');\n"
        "  plan=buildPlan();pending=[];\n"
        "  if(!plan.length){say('No positioned nodes are available for map downloads.');setProgress(0,1,'No tiles');return;}\n"
        "  try{\n"
        "    var storage=await markCached(plan);if(token!==refreshToken)return;\n"
        "    if(!storage){say('No writable storage is available on the device.');setProgress(0,1,'Unavailable');return;}\n"
        "    renderPlan(prefix);\n"
        "  }catch(error){\n"
        "    appendMapDebug('status-failed',{message:(error&&error.message)?error.message:String(error)});\n"
        "    if(token!==refreshToken)return;say('Could not check maps stored on the device. Try refreshing this page.');\n"
        "    setProgress(0,1,'Unavailable');warning.textContent='';\n"
        "  }\n"
        "}\n"
        "async function fetchTile(tile){\n"
        "  var query='?z='+tile.z+'&x='+tile.x+'&y='+tile.y;\n"
        "  var started=Date.now(),response;\n"
        "  try{response=await fetch('/map-tile-fetch'+query,{method:'POST'});}\n"
        "  catch(error){var transport=new Error((error&&error.message)?error.message:'browser fetch failed');transport.mapDebug={stage:'browser-fetch',clientMs:Date.now()-started};throw transport;}\n"
        "  var body=await response.text(),data=null;try{data=JSON.parse(body);}catch(ignore){}\n"
        "  var details={clientStatus:response.status,clientMs:Date.now()-started};\n"
        "  if(data)details.device=data;else details.body=body.slice(0,160);\n"
        "  if(!response.ok||!data||!data.ok){var failure=new Error((data&&data.error)||('HTTP '+response.status));failure.mapDebug=details;throw failure;}\n"
        "  return {data:data,clientMs:details.clientMs};\n"
        "}\n"
        "async function run(){\n"
        "  if(active||!pending.length)return;\n"
        "  var jobs=pending.slice(),maxZoom=selectedMaxZoom();\n"
        "  if((maxZoom>=19||jobs.length>200)&&!confirm('This plan will download '+jobs.length+\n"
        "      ' tiles through zoom '+maxZoom+'. Detailed maps can take a very, very long time. Continue?'))return;\n"
        "  active=true;cancelRequested=false;button.disabled=true;level.disabled=true;setStop(true);\n"
        "  var saved=0,unconfirmed=0,unconfirmedStreak=0;\n"
        "  var pausedForConnection=false,lastError='',runStarted=Date.now();\n"
        "  appendMapDebug('run-start',{maxZoom:maxZoom,jobs:jobs.length,planTiles:plan.length,pendingTiles:pending.length});\n"
        "  setProgress(0,jobs.length,'Starting');\n"
        "  for(var i=0;i<jobs.length;i++){\n"
        "    if(cancelRequested)break;var tile=jobs[i];\n"
        "    say('['+(i+1)+'/'+jobs.length+'] Downloading zoom '+tile.z+' tile '+tile.x+'/'+tile.y);\n"
        "    var tileOk=false;\n"
        "    for(var attempt=1;attempt<=MAX_TILE_ATTEMPTS;attempt++){\n"
        "      if(cancelRequested)break;\n"
        "      try{\n"
        "        var result=await fetchTile(tile);\n"
        "        tile.cached=true;saved++;unconfirmedStreak=0;tileOk=true;\n"
        "        appendMapDebug('tile-saved',{tile:tile.key,z:tile.z,x:tile.x,y:tile.y,attempt:attempt,clientMs:result.clientMs,cached:!!result.data.cached,bytes:result.data.bytes||0,upstream:result.data.upstream||0,expected:typeof result.data.expected==='number'?result.data.expected:null,headersMs:result.data.headersMs||0,bodyMs:result.data.bodyMs||0,deviceTotalMs:result.data.totalMs||0,heap:result.data.heap||0});\n"
        "        setProgress(i+1,jobs.length,'Saved zoom '+tile.z);\n"
        "        break;\n"
        "      }catch(error){\n"
        "        lastError=(error&&error.message)?error.message:String(error||'error');\n"
        "        appendMapDebug('attempt-failed',{tile:tile.key,z:tile.z,x:tile.x,y:tile.y,attempt:attempt,maxAttempts:MAX_TILE_ATTEMPTS,message:lastError,details:error&&error.mapDebug?error.mapDebug:null});\n"
        "        var deviceAnswered=!!(error&&error.mapDebug&&error.mapDebug.device);\n"
        "        if(!deviceAnswered&&!cancelRequested){\n"
        "          say('['+(i+1)+'/'+jobs.length+'] Checking whether '+tile.key+' was saved...');\n"
        "          setProgress(i,jobs.length,'Confirming saved tile');\n"
        "          try{\n"
        "            if(await verifySaved(tile)){\n"
        "              saved++;unconfirmedStreak=0;tileOk=true;\n"
        "              appendMapDebug('tile-confirmed',{tile:tile.key,z:tile.z,x:tile.x,y:tile.y,attempt:attempt,message:'Saved on device; download response was not received'});\n"
        "              setProgress(i+1,jobs.length,'Saved (confirmed)');\n"
        "              break;\n"
        "            }\n"
        "          }catch(verifyError){\n"
        "            appendMapDebug('verification-unavailable',{tile:tile.key,attempt:attempt,message:(verifyError&&verifyError.message)?verifyError.message:String(verifyError)});\n"
        "          }\n"
        "        }\n"
        "        if(attempt<MAX_TILE_ATTEMPTS&&!cancelRequested){\n"
        "          say('['+(i+1)+'/'+jobs.length+'] Retrying '+tile.key+' ('+(attempt+1)+' of '+MAX_TILE_ATTEMPTS+')');\n"
        "          setProgress(i,jobs.length,'Retrying');\n"
        "          await waitMs(250*attempt);\n"
        "        }\n"
        "      }\n"
        "    }\n"
        "    if(tileOk)continue;\n"
        "    if(cancelRequested)break;\n"
        "    unconfirmed++;\n"
        "    unconfirmedStreak++;\n"
        "    appendMapDebug('tile-still-pending',{tile:tile.key,consecutiveUnconfirmed:unconfirmedStreak,message:lastError});\n"
        "    setProgress(i+1,jobs.length,'Still pending');\n"
        "    say('Could not confirm '+tile.key+' after '+MAX_TILE_ATTEMPTS+' attempts. It remains available to retry.');\n"
        "    if(unconfirmedStreak>=UNCONFIRMED_STREAK_LIMIT){\n"
        "      pausedForConnection=true;\n"
        "      say('Pausing after '+unconfirmedStreak+' consecutive tiles could not be confirmed. Saved tiles are kept.');\n"
        "      break;\n"
        "    }\n"
        "  }\n"
        "  active=false;level.disabled=false;setStop(false);\n"
        "  pending=plan.filter(function(tile){return !tile.cached;});\n"
        "  appendMapDebug('run-finished',{reason:cancelRequested?'user-stop':(pausedForConnection?'connection-pause':'complete'),saved:saved,unconfirmed:unconfirmed,remaining:pending.length,elapsedMs:Date.now()-runStarted,lastError:lastError||null});\n"
        "  var prefix=cancelRequested?'Stopped by you.':\n"
        "    (pausedForConnection?'Paused after repeated connection problems.':\n"
        "    (pending.length?'Finished this pass; unconfirmed tiles can be retried.':'Download complete.'));\n"
        "  renderPlan(prefix);\n"
        "}\n"
        "stopButton.onclick=function(){if(active){cancelRequested=true;setStop(false);say('Stopping after the current tile...');}};\n"
        "button.onclick=run;\n"
        "level.onchange=function(){if(!active)refreshPlan();};\n"
        "resetMapDebug('page-load');setStop(false);refreshPlan();\n"
        "})();\n"
        "</script>";
    sendChunk(html);
#endif


    html += "<script src='https://unpkg.com/leaflet@1.9.4/dist/leaflet.js'></script>"
            "<script src='https://unpkg.com/leaflet.heat@0.2.0/dist/leaflet-heat.js'></script>";

        html += "</div>";
#if HAS_VNC_HOST
        const String vncIp = WiFi.localIP().toString();
        html += "<div class='tab-panel' id='tab-remote'><div class='tab-pane-center'>"
            "<h3>Remote</h3>"
            "<label class='remote-toggle'><input type='checkbox' id='remote-enabled' onchange='remoteToggle()'";
        if (vncHostEnabled()) html += " checked";
        html += "><span>Enable VNC host</span></label>"
            "<p id='remote-status' class='remote-status'>Checking remote host...</p>"
            "<form id='remote-lock' class='remote-lock' style='display:none' onsubmit='remoteUnlock();return false'>"
            "<label for='remote-pin'>PIN shown on the device</label>"
            "<input id='remote-pin' name='pin' type='text' inputmode='numeric' pattern='[0-9]{6}'"
            " maxlength='6' autocomplete='off' placeholder='000000'>"
            "<button type='submit' id='remote-unlock'>Unlock</button>"
            "<p id='remote-lock-msg' class='remote-status' style='flex-basis:100%;margin:0'></p></form>"
            "<iframe id='remote-frame' class='remote-frame' title='Remote control' data-src='http://";
        appendAttr(html, vncIp.c_str());
        html += ":";
        html += String(vncHostPort());
        html += "/' style='display:none'></iframe></div></div>";
#endif
#if HAS_WEB_FILES
        if (gCfg && cfgWebFilesEnabled(*gCfg)) {
            html += "<div class='tab-panel' id='tab-files'>";
            sendChunk(html);
            sendFlash(kFilesPane);
            html += "</div>";
        }
#endif
        // Close the frame, then the phone tab bar, the two sheets and the save
        // bar, all fixed to the viewport, then the shell script they use.
        html += "</main></div>"
                "<nav class='tabbar' aria-label='Pages'>"
                "<a data-tab='config' href='#config' class='active'><svg viewBox='0 0 24 24'>"
                "<circle cx='12' cy='12' r='3'/><path d='M12 2v3M12 19v3M2 12h3M19 12h3M4.9 4.9l2.1 2.1"
                "M17 17l2.1 2.1M4.9 19.1L7 17M17 7l2.1-2.1'/></svg>Config</a>"
                "<a data-tab='live' href='#live'><svg viewBox='0 0 24 24'><path d='M3 12h4l3-7 4 14 3-7h4'/>"
                "</svg>Live</a>"
#if !defined(DEVICE_CARDPUTER_LORA_HAT)
                "<a data-tab='chat' href='#chat'><svg viewBox='0 0 24 24'><path d='M4 5h16v11H9l-5 4z'/>"
                "</svg>Chat</a>"
#endif
                "<a data-tab='map' href='#map'><svg viewBox='0 0 24 24'><circle cx='12' cy='12' r='8'/>"
                "<circle cx='12' cy='12' r='2'/></svg>Nodes</a>"
                "<button type='button' onclick=\"appOpenSheet('sheet-more')\"><svg viewBox='0 0 24 24'>"
                "<circle cx='5' cy='12' r='1.5'/><circle cx='12' cy='12' r='1.5'/><circle cx='19' cy='12' r='1.5'/>"
                "</svg>More</button></nav>"
                "<div class='scrim' id='scrim'></div>"
                "<div class='sheet' id='sheet-secs'></div>"
                "<div class='sheet' id='sheet-more'><a data-tab='utils' href='#utils'>Utilities</a>";
#if HAS_VNC_HOST
        html += "<a data-tab='remote' href='#remote'>Remote</a>";
#endif
#if HAS_WEB_FILES
        if (gCfg && cfgWebFilesEnabled(*gCfg)) html += "<a data-tab='files' href='#files'>Files</a>";
#endif
        // The header's key chip does not fit a phone's header, and Remote Admin
        // setup sends people to it, so the key is one tap away here instead.
        if (havePubKey)
            html += "<a href='#' data-copykey onclick=\"copyPubKey(document.getElementById('pubkey-chip'));"
                    "this.textContent='Public key copied';return false\">Copy public key</a>";
        if (gCfg && gCfg->webCfgAuthEnabled) html += "<a href='/logout'>Logout</a>";
        html += "</div>"
                "<div class='savebar' id='savebar'><span id='save-count'></span>"
                "<button type='button' class='btn-2' id='save-discard'>Discard</button>"
                "<button type='submit' form='cfg-form' class='btn'>Save and restart</button></div>"
                "<script>";
        sendChunk(html);
        sendFlash(kAppJs);
        html += "</script>";
#if HAS_ADMIN_TERMINAL
        // Body level, outside every panel -- see kAdminModal.
        sendChunk(html);
        sendFlash(kAdminModal);
#endif
        html += "<script>"
                        "var nodeMap=null;"
                        "var nodeMarkerLayer=null;"
                        "var nodeHeatLayer=null;"
                        "var nodeHeatOn=true;"
                        "var livePollTimer=null;"
                        "var liveCursor=-1;"
                        "function setMapStatus(msg){"
                            "var el=document.getElementById('map-status');"
                            "if(el)el.textContent=msg||'';"
                        "}"
                        "function setLiveStatus(msg){"
                            "var el=document.getElementById('live-status');"
                            "if(el)el.textContent=msg||'';"
                        "}"
                        "function clearLiveFeed(){"
                            "var box=document.getElementById('live-feed');"
                            "if(box)box.innerHTML='';"
                        "}"
                        "function setReleaseCheckResult(msg,col){"
                            "var el=document.getElementById('release-check-result');"
                            "if(!el)return null;"
                            "el.style.color=col||'#888';"
                            "el.textContent=msg||'';"
                            "return el;"
                        "}"
                        "function parseVersionNums(v){"
                            "var m=String(v||'').match(/\\d+/g);"
                            "if(!m)return [];"
                            "for(var i=0;i<m.length;i++)m[i]=parseInt(m[i],10)||0;"
                            "return m;"
                        "}"
                        "function compareVersionNums(a,b){"
                            "var va=parseVersionNums(a),vb=parseVersionNums(b);"
                            "var n=va.length>vb.length?va.length:vb.length;"
                            "if(n<1)n=1;"
                            "for(var i=0;i<n;i++){"
                                "var xa=i<va.length?va[i]:0;"
                                "var xb=i<vb.length?vb[i]:0;"
                                "if(xa<xb)return -1;"
                                "if(xa>xb)return 1;"
                            "}"
                            "return 0;"
                        "}"
                        "function pollDeviceReleaseCheck(deadline,done){"
                            "var poll=function(start){"
                                "var path='/release-check'+(start?'?start=1':'');"
                                "var req=fetch(path,{cache:'no-store'})"
                                    ".then(function(r){if(!r.ok)throw new Error('http '+r.status);return r.json();});"
                                "var timeout=new Promise(function(_,reject){setTimeout(function(){reject(new Error('timeout'));},15000);});"
                                "Promise.race([req,timeout])"
                                    ".then(function(d){"
                                        "var cur=(d&&d.current)?d.current:'unknown';"
                                        "var st=(d&&d.status)?d.status:'';"
                                        "if(st==='pending'||st==='running'||st==='idle'){"
                                            "setReleaseCheckResult('Checking latest release...','#b0b8c8');"
                                            "if(Date.now()>=deadline){setReleaseCheckResult('Update check failed: request timed out','#ff9f9f');done();return;}"
                                            "setTimeout(function(){poll(false);},900);"
                                            "return;"
                                        "}"
                                        "if(d&&d.error){setReleaseCheckResult('Update check failed: '+d.error,'#ff9f9f');done();return;}"
                                        "var latest=(d&&d.latest)?d.latest:'';"
                                        "if(d&&d.updateAvailable){"
                                            "var el=setReleaseCheckResult('New release available: '+latest+' (current '+cur+').','#8ef2b8');"
                                            "if(el&&d.url){"
                                                "var a=document.createElement('a');"
                                                "a.href=d.url;"
                                                "a.target='_blank';"
                                                "a.rel='noopener';"
                                                "a.style.marginLeft='0.45em';"
                                                "a.textContent='View release';"
                                                "el.appendChild(a);"
                                            "}"
                                        "}else if(latest){"
                                            "setReleaseCheckResult('You are up to date ('+cur+'). Latest: '+latest+'.','#8ef2b8');"
                                        "}else{"
                                            "setReleaseCheckResult('No release information returned.','#ffd181');"
                                        "}"
                                        "done();"
                                    "})"
                                    ".catch(function(err){"
                                        "var msg=(err&&err.message)?err.message:'network error';"
                                        "if(msg==='timeout')msg='request timed out';"
                                        "setReleaseCheckResult('Update check failed: '+msg,'#ff9f9f');"
                                        "done();"
                                    "});"
                            "};"
                            "poll(true);"
                        "}"
                        "function checkLatestRelease(){"
                            "var btn=document.getElementById('check-release-btn');"
                            "if(btn)btn.disabled=true;"
                            "var done=function(){if(btn)btn.disabled=false;};"
                            "setReleaseCheckResult('Checking latest release...','#b0b8c8');"
                            "var cur='" APP_VERSION "';"
                            "var releaseUrl='https://github.com/oumike/camillia-mt/releases/latest';"
                            "var apiReq=fetch('https://api.github.com/repos/oumike/camillia-mt/releases/latest',{cache:'no-store',headers:{'Accept':'application/vnd.github+json'}})"
                                ".then(function(r){if(!r.ok)throw new Error('http '+r.status);return r.json();})"
                                ".then(function(d){"
                                    "var latest=(d&&d.tag_name)?String(d.tag_name).trim():'';"
                                    "var url=(d&&d.html_url)?d.html_url:releaseUrl;"
                                    "if(!latest)throw new Error('missing tag');"
                                    "return {latest:latest,url:url};"
                                "})"
                                ".catch(function(){"
                                    "return fetch('https://raw.githubusercontent.com/oumike/camillia-mt/main/VERSION',{cache:'no-store'})"
                                        ".then(function(r){if(!r.ok)throw new Error('http '+r.status);return r.text();})"
                                        ".then(function(t){"
                                            "var latest=String(t||'').trim();"
                                            "if(!latest)throw new Error('missing tag');"
                                            "return {latest:latest,url:releaseUrl};"
                                        "});"
                                "});"
                            "var timeout=new Promise(function(_,reject){setTimeout(function(){reject(new Error('timeout'));},15000);});"
                            "Promise.race([apiReq,timeout])"
                                ".then(function(info){"
                                    "var latest=(info&&info.latest)?info.latest:'';"
                                    "if(!latest){setReleaseCheckResult('No release information returned.','#ffd181');done();return;}"
                                    "if(compareVersionNums(cur,latest)<0){"
                                        "var el=setReleaseCheckResult('New release available: '+latest+' (current '+cur+').','#8ef2b8');"
                                        "if(el&&info.url){"
                                            "var a=document.createElement('a');"
                                            "a.href=info.url;"
                                            "a.target='_blank';"
                                            "a.rel='noopener';"
                                            "a.style.marginLeft='0.45em';"
                                            "a.textContent='View release';"
                                            "el.appendChild(a);"
                                        "}"
                                        "done();"
                                        "return;"
                                    "}"
                                    "setReleaseCheckResult('You are up to date ('+cur+'). Latest: '+latest+'.','#8ef2b8');"
                                    "done();"
                                "})"
                                ".catch(function(err){"
                                    "var msg=(err&&err.message)?err.message:'network error';"
                                    "if(msg==='timeout')msg='request timed out';"
                                    "setReleaseCheckResult('Release check failed in browser: '+msg,'#ff9f9f');"
                                    "done();"
                                "});"
                        "}"
                        "function liveSplitTimestamp(t){"
                            "var s=String(t||'').trim();"
                            // Both clock formats, since the device sends whichever
                            // its Clock Format setting was on when the line was built.
                            "var m=s.match(/^(\\d{1,2}:\\d\\d(?: [AP]M)?|--:--)\\s+(.*)$/);"
                            "if(m)return {ts:m[1],body:String(m[2]||'').trim()};"
                            "return {ts:'',body:s};"
                        "}"
                        "function livePrefixTs(ts,msg){return ts?(ts+' '+msg):msg;}"
                        "function livePortLabel(tag){"
                            "if(!tag)return 'data';"
                            "if(tag==='T')return 'text';"
                            "if(tag==='N')return 'nodeinfo';"
                            "if(tag==='P')return 'position';"
                            "if(tag==='E')return 'telemetry';"
                            "if(tag==='G')return 'neighborinfo';"
                            "if(tag==='A')return 'routing';"
                            "return 'data';"
                        "}"
                        "function liveDestLabel(dst){"
                            "if(!dst)return 'node';"
                            "if(dst==='B'||dst==='BCAST')return 'broadcast';"
                            "if(dst==='M')return 'me';"
                            "if(dst==='U')return 'node';"
                            "return 'node';"
                        "}"
                        "function liveRoutingErrorName(err){"
                            "if(err===0)return 'NONE';"
                            "if(err===1)return 'NO_ROUTE';"
                            "if(err===2)return 'GOT_NAK';"
                            "if(err===3)return 'TIMEOUT';"
                            "if(err===4)return 'NO_INTERFACE';"
                            "if(err===5)return 'MAX_RETRANSMIT';"
                            "if(err===6)return 'NO_CHANNEL';"
                            "if(err===7)return 'TOO_LARGE';"
                            "if(err===8)return 'NO_RESPONSE';"
                            "if(err===9)return 'DUTY_CYCLE_LIMIT';"
                            "if(err===35)return 'PKI_UNKNOWN_PUBKEY';"
                            "return '';"
                        "}"
                        "function liveHexReq(req){"
                            "var n=parseInt(req,16);"
                            "if(isNaN(n))return String(req||'').toUpperCase();"
                            "var h=(n>>>0).toString(16).toUpperCase();"
                            "while(h.length<8)h='0'+h;"
                            "if(h.length>8)h=h.slice(-8);"
                            "return h;"
                        "}"
                        "function liveFormatFirmwareLine(t){"
                            "var p=liveSplitTimestamp(t);"
                            "var body=p.body;"
                            "var ts=p.ts;"
                            "if(!body)return '';"
                            "var m=body.match(/^R\\s+([^>\\s]+)>([^\\s]+)\\s+([^\\s]+)\\s+c(\\d+)\\s+h([^\\s]{1,3})$/);"
                            "if(m){"
                                "return livePrefixTs(ts,'RX '+livePortLabel(m[3])+' from '+m[1]+' to '+liveDestLabel(m[2])+' on ch'+m[4]+' hash '+m[5]);"
                            "}"
                            "m=body.match(/^R\\s+([^>\\s]+)>([^\\s]+)\\s+([^\\s]+)\\s+c(\\d+)$/);"
                            "if(m){"
                                "return livePrefixTs(ts,'RX '+livePortLabel(m[3])+' from '+m[1]+' to '+liveDestLabel(m[2])+' on ch'+m[4]);"
                            "}"
                            "m=body.match(/^R\\s+ACK\\s+([^\\s]+)\\s+([0-9A-Fa-f]{1,8})\\s+h([^\\s]{1,3})$/);"
                            "if(m){"
                                "return livePrefixTs(ts,'RX routing ACK from '+m[1]+' req:'+liveHexReq(m[2])+' hash:'+m[3]);"
                            "}"
                            "m=body.match(/^R\\s+NAK\\s+([^\\s]+)\\s+([0-9A-Fa-f]{1,8})\\s+err(\\d+)\\s+h([^\\s]{1,3})$/);"
                            "if(m){"
                                "var err=parseInt(m[3],10);"
                                "var errName=liveRoutingErrorName(isNaN(err)?-1:err);"
                                "if(errName)return livePrefixTs(ts,'RX routing NAK from '+m[1]+' req:'+liveHexReq(m[2])+' '+errName+'('+m[3]+') hash:'+m[4]);"
                                "return livePrefixTs(ts,'RX routing NAK from '+m[1]+' req:'+liveHexReq(m[2])+' err:'+m[3]+' hash:'+m[4]);"
                            "}"
                            "m=body.match(/^R\\s+([^\\s]+)\\s+ENC\\s+([^\\s]+)$/);"
                            "if(m){"
                                "return livePrefixTs(ts,'RX encrypted packet from '+m[1]+' (hash '+m[2]+')');"
                            "}"
                            "m=body.match(/^T\\s+ACK\\s+([^\\s]+)\\s+([^\\s]+)$/);"
                            "if(m){"
                                "return livePrefixTs(ts,'TX routing ACK to '+m[1]+' ('+m[2]+')');"
                            "}"
                            "m=body.match(/^T\\s+TXT\\s+([^\\s]+)\\s+c(\\d+)\\s+([^\\s]+)$/);"
                            "if(m){"
                                "return livePrefixTs(ts,'TX text to '+liveDestLabel(m[1])+' on ch'+m[2]+' id:'+m[3]);"
                            "}"
                            "m=body.match(/^T\\s+TXT\\s+([^\\s]+)\\s+ER$/);"
                            "if(m){"
                                "return livePrefixTs(ts,'TX text to '+liveDestLabel(m[1])+' FAILED');"
                            "}"
                            "m=body.match(/^T\\s+POS\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)$/);"
                            "if(m){"
                                "return livePrefixTs(ts,'TX position to '+liveDestLabel(m[1])+' id:'+m[2]+' ('+m[3]+')');"
                            "}"
                            "m=body.match(/^T\\s+NOD\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)$/);"
                            "if(m){"
                                "var mode=(m[1]==='U')?'unicast':'broadcast';"
                                "return livePrefixTs(ts,'TX nodeinfo '+mode+' to '+m[2]+' ('+m[3]+')');"
                            "}"
                            "m=body.match(/^T\\s+DM\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)$/);"
                            "if(m){"
                                "return livePrefixTs(ts,'TX DM to '+m[2]+' via '+m[1]+' id:'+m[3]);"
                            "}"
                            "m=body.match(/^T\\s+DM\\s+ER\\s+([^\\s]+)$/);"
                            "if(m){"
                                "return livePrefixTs(ts,'TX DM FAILED ('+m[1]+')');"
                            "}"
                            "return String(t||'');"
                        "}"
                        "function liveTrafficClass(t){"
                            "var s=liveSplitTimestamp(t).body;"
                            "if(!s)return 'live-default';"
                            "if(s.indexOf(' ER')>=0||s.indexOf('R NAK')===0)return 'live-err';"
                            "if(s.indexOf('T ACK')===0)return 'live-tx-ack';"
                            "if(s.indexOf('R ACK')===0)return 'live-rx-ack';"
                            "if(s.indexOf('T DM')===0)return 'live-tx-dm';"
                            "if(s.indexOf('R ')===0&&s.indexOf(' ENC ')>=0)return 'live-rx-enc';"
                            "var m=s.match(/^R\\s+([^>\\s]+)>([^\\s]+)\\s+([A-Z])\\s+c(\\d+)(?:\\s+([^\\s]+))?$/);"
                            "if(m){"
                                "if(m[3]==='T')return 'live-rx-text';"
                                "if(m[3]==='N')return 'live-rx-node';"
                                "if(m[3]==='P')return 'live-rx-pos';"
                                "if(m[3]==='E')return 'live-rx-tlm';"
                                "return 'live-rx-other';"
                            "}"
                            "if(s.indexOf('T TXT')===0)return 'live-tx-text';"
                            "if(s.indexOf('T NOD')===0)return 'live-tx-node';"
                            "if(s.indexOf('T POS')===0)return 'live-tx-pos';"
                            "if(s.indexOf('T TLM')===0)return 'live-tx-tlm';"
                            "if(s.indexOf('R ')===0)return 'live-rx-other';"
                            "if(s.indexOf('T ')===0)return 'live-tx-other';"
                            "return 'live-default';"
                        "}"
                        "function appendLiveLines(lines){"
                            "var box=document.getElementById('live-feed');"
                            "if(!box||!lines||!lines.length)return;"
                            "var autoScroll=(box.scrollTop+box.clientHeight+18)>=box.scrollHeight;"
                            "for(var i=0;i<lines.length;i++){"
                                "var raw=(lines[i]&&lines[i].t)?lines[i].t:'';"
                                "var txt=liveFormatFirmwareLine(raw);"
                                "var cls=liveTrafficClass(raw);"
                                "var row=document.createElement('div');"
                                "row.className='live-line '+cls;"
                                "row.textContent=txt;"
                                "if(raw&&raw!==txt)row.title=raw;"
                                "box.appendChild(row);"
                            "}"
                            "while(box.childElementCount>280){box.removeChild(box.firstChild);}"
                            "if(autoScroll)box.scrollTop=box.scrollHeight;"
                        "}"
                        "function pollLiveFeed(){"
                            "var url='/live-data';"
                            "if(liveCursor>=0)url+='?after='+liveCursor;"
                            "fetch(url,{cache:'no-store'})"
                                ".then(function(r){if(!r.ok)throw new Error('http '+r.status);return r.json();})"
                                ".then(function(d){"
                                    "var cnt=(d&&d.lines&&d.lines.length)?d.lines.length:0;"
                                    "if(cnt)appendLiveLines(d.lines);"
                                    "if(d&&typeof d.total==='number')liveCursor=d.total-1;"
                                    "setLiveStatus(cnt?('+'+cnt+' update'+(cnt===1?'':'s')):'Listening...');"
                                "})"
                                ".catch(function(){setLiveStatus('Live feed unavailable');});"
                        "}"
                        "function startLivePolling(){"
                            "if(livePollTimer)return;"
                            "setLiveStatus('Connecting...');"
                            "pollLiveFeed();"
                            "livePollTimer=setInterval(pollLiveFeed,1500);"
                            "startChartPolling();"
                        "}"
                        "function stopLivePolling(){"
                            "if(!livePollTimer)return;"
                            "clearInterval(livePollTimer);"
                            "livePollTimer=null;"
                            "setLiveStatus('Paused');"
                            "stopChartPolling();"
                        "}"
                        "var chartPollTimer=null;"
                        "function drawChartFrame(ctx,w,h,pad){"
                            "ctx.clearRect(0,0,w,h);"
                            "ctx.fillStyle='#0a1a36';ctx.fillRect(0,0,w,h);"
                            "ctx.strokeStyle='#1f3a6a';ctx.lineWidth=1;"
                            "ctx.beginPath();"
                            "for(var i=0;i<=4;i++){"
                                "var y=pad.t+(h-pad.t-pad.b)*i/4;"
                                "ctx.moveTo(pad.l,y);ctx.lineTo(w-pad.r,y);"
                            "}"
                            "for(var j=0;j<=6;j++){"
                                "var x=pad.l+(w-pad.l-pad.r)*j/6;"
                                "ctx.moveTo(x,pad.t);ctx.lineTo(x,h-pad.b);"
                            "}"
                            "ctx.stroke();"
                        "}"
                        "function drawSeries(ctx,vals,cap,minV,maxV,pad,w,h,color){"
                            "if(!vals||vals.length<1)return;"
                            "if(maxV<=minV)maxV=minV+1;"
                            "var iw=w-pad.l-pad.r,ih=h-pad.t-pad.b;"
                            "var step=iw/Math.max(1,cap-1);"
                            "var offset=cap-vals.length;"
                            "ctx.strokeStyle=color;ctx.lineWidth=1.5;ctx.beginPath();"
                            "var started=false;"
                            "for(var i=0;i<vals.length;i++){"
                                "var idx=offset+i;"
                                "var x=pad.l+idx*step;"
                                "var nv=(vals[i]-minV)/(maxV-minV);"
                                "if(nv<0)nv=0;if(nv>1)nv=1;"
                                "var y=pad.t+(1-nv)*ih;"
                                "if(!started){ctx.moveTo(x,y);started=true;}"
                                "else ctx.lineTo(x,y);"
                            "}"
                            "ctx.stroke();"
                        "}"
                        "function drawYLabels(ctx,pad,w,h,minV,maxV,suffix,align){"
                            "ctx.fillStyle='#7ea1d4';ctx.font='10px ui-monospace,Menlo,Consolas,monospace';"
                            "ctx.textBaseline='middle';"
                            "for(var i=0;i<=4;i++){"
                                "var v=maxV-(maxV-minV)*i/4;"
                                "var y=pad.t+(h-pad.t-pad.b)*i/4;"
                                "var label=Math.round(v)+suffix;"
                                "if(align==='right'){ctx.textAlign='left';ctx.fillText(label,w-pad.r+2,y);}"
                                "else{ctx.textAlign='right';ctx.fillText(label,pad.l-2,y);}"
                            "}"
                        "}"
                        "function statsLine(name,unit,s){"
                            "if(!s||!s.values||s.values.length<1)return name+' --';"
                            "var sum=0,mn=Infinity,mx=-Infinity;"
                            "for(var i=0;i<s.values.length;i++){var v=s.values[i];sum+=v;if(v<mn)mn=v;if(v>mx)mx=v;}"
                            "var avg=sum/s.values.length;"
                            "var cur=(s.hasLast?s.last:s.values[s.values.length-1]);"
                            "return name+' cur '+cur.toFixed(1)+unit+'  avg '+avg.toFixed(1)+unit+'  min '+mn.toFixed(1)+unit+'  max '+mx.toFixed(1)+unit+'  n='+s.values.length;"
                        "}"
                        "function renderChUtilChart(d){"
                            "var c=document.getElementById('chart-chutil');if(!c)return;"
                            "var ctx=c.getContext('2d');"
                            "var w=c.width=c.clientWidth||600;var h=c.height=180;"
                            "var pad={l:36,r:10,t:8,b:14};"
                            "drawChartFrame(ctx,w,h,pad);"
                            "drawYLabels(ctx,pad,w,h,0,100,'%','left');"
                            "var cap=d.capacity||60;"
                            "drawSeries(ctx,(d.chUtil&&d.chUtil.values)||[],cap,0,100,pad,w,h,'#67d8ff');"
                            "drawSeries(ctx,(d.airUtil&&d.airUtil.values)||[],cap,0,100,pad,w,h,'#ffd56b');"
                            "var stats=document.getElementById('chart-chutil-stats');"
                            "if(stats)stats.textContent=statsLine('ChUtil','%',d.chUtil)+'   '+statsLine('AirTx','%',d.airUtil);"
                        "}"
                        "function seriesBounds(vals,fallbackLo,fallbackHi){"
                            "if(!vals||!vals.length)return[fallbackLo,fallbackHi];"
                            "var mn=Infinity,mx=-Infinity;"
                            "for(var i=0;i<vals.length;i++){var v=vals[i];if(v<mn)mn=v;if(v>mx)mx=v;}"
                            "var pad=(mx-mn)*0.15;if(pad<1)pad=1;"
                            "return[Math.floor(mn-pad),Math.ceil(mx+pad)];"
                        "}"
                        "function renderSnrChart(d){"
                            "var c=document.getElementById('chart-snr');if(!c)return;"
                            "var ctx=c.getContext('2d');"
                            "var w=c.width=c.clientWidth||600;var h=c.height=180;"
                            "var pad={l:40,r:48,t:8,b:14};"
                            "drawChartFrame(ctx,w,h,pad);"
                            "var snrVals=(d.snr&&d.snr.values)||[];"
                            "var rssiVals=(d.rssi&&d.rssi.values)||[];"
                            "var sb=seriesBounds(snrVals,-20,15);"
                            "var rb=seriesBounds(rssiVals,-130,-30);"
                            "drawYLabels(ctx,pad,w,h,sb[0],sb[1],'dB','left');"
                            "drawYLabels(ctx,pad,w,h,rb[0],rb[1],'dBm','right');"
                            "var cap=d.capacity||60;"
                            "drawSeries(ctx,snrVals,cap,sb[0],sb[1],pad,w,h,'#9ceadf');"
                            "drawSeries(ctx,rssiVals,cap,rb[0],rb[1],pad,w,h,'#f7b46d');"
                            "var stats=document.getElementById('chart-snr-stats');"
                            "if(stats)stats.textContent=statsLine('SNR','dB',d.snr)+'   '+statsLine('RSSI','dBm',d.rssi);"
                        "}"
                        "function pollChartData(){"
                            "fetch('/chart-data',{cache:'no-store',credentials:'same-origin'})"
                                ".then(function(r){if(!r.ok)throw r;return r.json();})"
                                ".then(function(d){renderChUtilChart(d);renderSnrChart(d);})"
                                ".catch(function(){});"
                        "}"
                        "function startChartPolling(){"
                            "if(chartPollTimer)return;"
                            "pollChartData();"
                            "chartPollTimer=setInterval(pollChartData,2000);"
                        "}"
                        "function stopChartPolling(){"
                            "if(!chartPollTimer)return;"
                            "clearInterval(chartPollTimer);"
                            "chartPollTimer=null;"
                        "}"
                        "function ensureNodeMap(){"
                            "var mapEl=document.getElementById('node-heatmap');"
                            "if(!mapEl)return;"
                            "if(!window.L){"
                                "mapEl.innerHTML='<div style=\"padding:1em;color:#ffd0d0\">Map library unavailable. Check internet access for tile/CDN loading.</div>';"
                                "setMapStatus('Basemap unavailable');"
                                "return;"
                            "}"
                            "if(nodeMap){nodeMap.invalidateSize();return;}"
                            "nodeMap=L.map('node-heatmap',{zoomControl:true,worldCopyJump:true,minZoom:2,maxZoom:19});"
                            "var base=L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',{" 
                                "maxZoom:19,attribution:'&copy; OpenStreetMap contributors'"
                            "}).addTo(nodeMap);"
                            "base.on('tileerror',function(){setMapStatus('Basemap tile load issue');});"
                            "nodeMarkerLayer=L.featureGroup().addTo(nodeMap);"
                            "var pts=(window.NODE_HEAT_POINTS||[]);"
                            "var heatPts=[];"
                            "for(var i=0;i<pts.length;i++){"
                                "var p=pts[i];"
                                "if(!isFinite(p.lat)||!isFinite(p.lon))continue;"
                                "var ll=[p.lat,p.lon];"
                                "heatPts.push([p.lat,p.lon,0.9]);"
                                "L.circleMarker(ll,{radius:5,weight:1,color:'#fff0b2',fillColor:'#ff7a45',fillOpacity:0.8}).addTo(nodeMarkerLayer);"
                            "}"
                            "if(window.L.heatLayer&&heatPts.length){"
                                "nodeHeatLayer=L.heatLayer(heatPts,{radius:26,blur:22,maxZoom:13,"
                                    "gradient:{0.2:'#2b83ba',0.45:'#abdda4',0.65:'#fdae61',0.9:'#d7191c'}}).addTo(nodeMap);"
                                "nodeHeatOn=true;"
                            "} else {"
                                "nodeHeatOn=false;"
                                "var hb=document.getElementById('map-heat-btn');"
                                "if(hb){hb.textContent='Heat: N/A';hb.disabled=true;hb.classList.remove('active');}"
                            "}"
                            "if(nodeMarkerLayer.getLayers().length){"
                                "nodeMap.fitBounds(nodeMarkerLayer.getBounds(),{padding:[18,18],maxZoom:13});"
                                "setMapStatus(nodeMarkerLayer.getLayers().length+' positioned node'+(nodeMarkerLayer.getLayers().length===1?'':'s'));"
                            "} else {"
                                "nodeMap.setView([20,0],2);"
                                "setMapStatus('No node positions yet');"
                            "}"
                        "}"
                        "function fitNodeMap(){"
                            "ensureNodeMap();"
                            "if(nodeMap&&nodeMarkerLayer&&nodeMarkerLayer.getLayers().length){"
                                "nodeMap.fitBounds(nodeMarkerLayer.getBounds(),{padding:[18,18],maxZoom:13});"
                            "}"
                        "}"
                        "function worldNodeMap(){"
                            "ensureNodeMap();"
                            "if(nodeMap)nodeMap.setView([20,0],2);"
                        "}"
                        "function centerOnMeMap(){"
                            "ensureNodeMap();"
                            "var me=window.NODE_ME_POINT;"
                            "if(!nodeMap||!me||!isFinite(me.lat)||!isFinite(me.lon)){setMapStatus('Device position unavailable');return;}"
                            "var z=nodeMap.getZoom();"
                            "if(!isFinite(z)||z<13)z=13;"
                            "nodeMap.setView([me.lat,me.lon],z);"
                            "setMapStatus('Centered on this device');"
                        "}"
                        "function toggleNodeHeat(){"
                            "ensureNodeMap();"
                            "var hb=document.getElementById('map-heat-btn');"
                            "if(!nodeMap||!nodeHeatLayer){if(hb)hb.classList.remove('active');return;}"
                            "if(nodeHeatOn){nodeMap.removeLayer(nodeHeatLayer);nodeHeatOn=false;if(hb){hb.textContent='Heat: Off';hb.classList.remove('active');}}"
                            "else{nodeHeatLayer.addTo(nodeMap);nodeHeatOn=true;if(hb){hb.textContent='Heat: On';hb.classList.add('active');}}"
                        "}"
                        // The picked node: its card in #node-info, and the map
                        // centred on it, marked the way search matches are (see
                        // nodeMatchMarker()). The dropdown, a search match and a
                        // marker on the map all end up here.
                        "var nodePinMarker=null;"
                        "function selectNode(){"
                            "var sel=document.getElementById('node-select');"
                            "var info=document.getElementById('node-info');"
                            "if(!sel||!info)return;"
                            "var v=sel.value;"
                            "var btns=document.querySelectorAll('#node-search-results button'),i;"
                            "for(i=0;i<btns.length;i++)btns[i].classList.toggle('active',btns[i].getAttribute('data-v')===v);"
                            "if(nodePinMarker&&nodeMap){nodeMap.removeLayer(nodePinMarker);nodePinMarker=null;}"
                            "if(v===''){info.innerHTML='';return;}"
                            "var d=document.getElementById('nd-'+v);"
                            "if(!d){info.innerHTML='';return;}"
                            "info.innerHTML=d.innerHTML;"
                            "ensureNodeMap();"
                            "var ll=nodeLoc(v);"
                            "if(!nodeMap)return;"
                            "if(!ll){setMapStatus('No position for '+(sel.options[sel.selectedIndex]||{}).textContent);return;}"
                            "nodePinMarker=nodeMatchMarker(ll,(sel.options[sel.selectedIndex]||{}).textContent||'',v).addTo(nodeMap);"
                            "nodePinMarker.bringToFront();"
                            "nodeMap.setView(ll,Math.max(nodeMap.getZoom()||0,13));"
                            "setMapStatus((sel.options[sel.selectedIndex]||{}).textContent||'');"
                        "}"
                        // ── Favorite toggle ───────────────────────────────────
                        // The detail panels are cloned out of the hidden store
                        // into #node-info, so every node has two copies of its
                        // card in the DOM. Updates go by node id across the whole
                        // document rather than to the clicked button, or the copy
                        // that was not clicked goes stale and reappears wrong the
                        // next time the node is selected.
                        "function applyNodeFavorite(id,fav){"
                            "var i,els;"
                            "els=document.querySelectorAll(\"[data-fav-btn='\"+id+\"']\");"
                            "for(i=0;i<els.length;i++){"
                                "els[i].setAttribute('data-fav',fav?'1':'0');"
                                "els[i].textContent=fav?'Unfavorite':'Favorite';"
                            "}"
                            "els=document.querySelectorAll(\"[data-fav-tag='\"+id+\"']\");"
                            "for(i=0;i<els.length;i++)els[i].style.display=fav?'':'none';"
                            "var opts=document.querySelectorAll(\"#node-select option[data-nid='\"+id+\"']\");"
                            "for(i=0;i<opts.length;i++)opts[i].setAttribute('data-fav',fav?'1':'0');"
                            "resortNodeOptions();"
                        "}"
                        // The streamed options, read once into NODE_OPTS: the
                        // dropdown is refilled from it in its own order (see
                        // renderNodeDropdown()), and the search reads it without
                        // touching the dropdown. No option is rebuilt, and the
                        // values that map to nd-<rank> are never touched.
                        "var NODE_OPTS=null,NODE_DIV=null;"
                        "function nodeOpts(){"
                            "if(NODE_OPTS)return NODE_OPTS;"
                            "var sel=document.getElementById('node-select'),a=[],i,o;"
                            "if(!sel)return a;"
                            "for(i=0;i<sel.options.length;i++){"
                                "o=sel.options[i];"
                                "if(o.getAttribute('data-fav-divider')==='1'){NODE_DIV=o;continue;}"
                                "if(o.hasAttribute('data-nid'))a.push(o);"   // not the placeholder
                            "}"
                            "NODE_OPTS=a;"
                            "return a;"
                        "}"
                        // Short and long name as shown, plus the id: "!1a2b3c4d",
                        // so "1a2b", "!1a2b" and "3c4d" all find it.
                        "function nodeOptMatches(o,q){"
                            "if(!q)return true;"
                            "var t=(o.textContent+' '+(o.getAttribute('data-nid')||'')).toLowerCase();"
                            "return t.indexOf(q)>=0||t.indexOf(q.replace(/^!/,''))>=0;"
                        "}"
                        // Alphabetical by what the option shows ("SHORT -- Long
                        // name"), ignoring case and reading digits as numbers.
                        // Nodes that have sent no NodeInfo show "----" and sort
                        // last rather than ahead of every letter.
                        "function nodeOptKey(o){"
                            "var t=o.textContent;"
                            "return t.indexOf('----')===0?'\\uffff'+t:t;"
                        "}"
                        "function nodeOptCmp(a,b){"
                            "return nodeOptKey(a).localeCompare(nodeOptKey(b),undefined,{sensitivity:'base',numeric:true});"
                        "}"
                        // The dropdown's checkboxes, as option attributes that
                        // must read '1'.
                        "function nodeFilterAttrs(){"
                            "var f=[],ids=[['nf-known','data-known'],['nf-loc','data-loc'],['nf-fav','data-fav']],i,c;"
                            "for(i=0;i<ids.length;i++){c=document.getElementById(ids[i][0]);if(c&&c.checked)f.push(ids[i][1]);}"
                            "return f;"
                        "}"
                        // Favorites first, as the device ranks them, then the
                        // rest, each group alphabetical; q filters, '' keeps all.
                        // attrs: the dropdown's checkboxes; the search passes none.
                        "function nodeGroups(q,attrs){"
                            "var all=nodeOpts(),favs=[],rest=[],i,j,o,skip;"
                            "for(i=0;i<all.length;i++){"
                                "o=all[i];"
                                "if(!nodeOptMatches(o,q))continue;"
                                "skip=false;"
                                "for(j=0;attrs&&j<attrs.length;j++)if(o.getAttribute(attrs[j])!=='1')skip=true;"
                                "if(skip)continue;"
                                "if(o.getAttribute('data-fav')==='1')favs.push(o);else rest.push(o);"
                            "}"
                            "favs.sort(nodeOptCmp);"
                            "rest.sort(nodeOptCmp);"
                            "return{favs:favs,rest:rest,total:all.length};"
                        "}"
                        // The dropdown always lists every node; the search only
                        // drives the results list and the map.
                        "function renderNodeDropdown(){"
                            "var sel=document.getElementById('node-select');"
                            "if(!sel)return;"
                            "var attrs=nodeFilterAttrs(),g=nodeGroups('',attrs),keep=sel.value,i;"
                            "while(sel.options.length>1)sel.remove(1);"   // keeps the placeholder
                            "for(i=0;i<g.favs.length;i++)sel.appendChild(g.favs[i]);"
                            "if(g.favs.length&&g.rest.length){"
                                "if(!NODE_DIV){"
                                    "NODE_DIV=document.createElement('option');"
                                    "NODE_DIV.disabled=true;"
                                    "NODE_DIV.setAttribute('data-fav-divider','1');"
                                    "NODE_DIV.textContent='\\u2014\\u2014\\u2014\\u2014\\u2014\\u2014\\u2014\\u2014\\u2014\\u2014\\u2014\\u2014';"
                                "}"
                                "sel.appendChild(NODE_DIV);"
                            "}"
                            "for(i=0;i<g.rest.length;i++)sel.appendChild(g.rest[i]);"
                            "sel.value=keep;"
                            "if(sel.value!==keep)sel.value='';"   // the picked node was filtered out
                            "var shown=g.favs.length+g.rest.length,cnt=document.getElementById('node-filter-count');"
                            "if(cnt)cnt.textContent=!attrs.length?'':(shown?('Showing '+shown+' of '+g.total+' nodes'):'No nodes match these filters');"
                        "}"
                        // Remembered per browser, like a tab; a page without
                        // storage just starts with every box clear.
                        "function nodeFilterChanged(){"
                            "try{localStorage.setItem('nodeFilters',JSON.stringify(nodeFilterAttrs()));}catch(e){}"
                            "renderNodeDropdown();"
                        "}"
                        "function nodeFilterRestore(){"
                            "var f=[],i,c,ids={'data-known':'nf-known','data-loc':'nf-loc','data-fav':'nf-fav'};"
                            "try{f=JSON.parse(localStorage.getItem('nodeFilters')||'[]')||[];}catch(e){}"
                            "for(i=0;i<f.length;i++){c=document.getElementById(ids[f[i]]);if(c)c.checked=true;}"
                            "renderNodeDropdown();"
                        "}"
                        // keepView: a re-sort after a favorite toggle, which must
                        // not move the map or re-pick a node under the user.
                        "function renderNodeOptions(keepView){"
                            "var sel=document.getElementById('node-select');"
                            "if(!sel)return;"
                            "var box=document.getElementById('node-search');"
                            "var q=box?box.value.trim().toLowerCase():'';"
                            "var g=nodeGroups(q),list=g.favs.concat(g.rest);"
                            "var cnt=document.getElementById('node-search-count');"
                            "if(cnt)cnt.textContent=!q?'':(list.length?(list.length+' of '+g.total+' nodes match'):'No nodes match');"
                            "renderNodeResults(q?list:[]);"
                            "renderNodeSearchMap(q?list:null,!keepView);"
                            // One match: show it, so a typed id or name is all it takes.
                            "if(q&&list.length===1&&!keepView&&sel.value!==list[0].value)nodePick(list[0].value);"
                        "}"
                        "function resortNodeOptions(){renderNodeDropdown();renderNodeOptions(true);}"
                        "if(document.readyState!=='loading')nodeFilterRestore();"
                        "else document.addEventListener('DOMContentLoaded',nodeFilterRestore);"
                        // A node's position, from its detail panel; null when it
                        // has none.
                        "function nodeLoc(v){"
                            "var d=document.getElementById('nd-'+v);"
                            "if(!d||d.getAttribute('data-loc')!=='1')return null;"
                            "var la=parseFloat(d.getAttribute('data-lat')),lo=parseFloat(d.getAttribute('data-lon'));"
                            "return(isFinite(la)&&isFinite(lo))?[la,lo]:null;"
                        "}"
                        // The matches, one button each, under the search box.
                        "function renderNodeResults(list){"
                            "var box=document.getElementById('node-search-results');"
                            "if(!box)return;"
                            "box.innerHTML='';"
                            "if(!list.length){box.style.display='none';return;}"
                            "var max=Math.min(list.length,100),sel=document.getElementById('node-select'),i,o,b,nm,id;"
                            "for(i=0;i<max;i++){"
                                "o=list[i];"
                                "b=document.createElement('button');"
                                "b.type='button';"
                                "b.setAttribute('data-v',o.value);"
                                "if(sel&&sel.value===o.value)b.classList.add('active');"
                                "nm=document.createElement('span');"
                                "nm.textContent=o.textContent;"
                                "id=document.createElement('span');"
                                "id.className='nr-id';"
                                "id.textContent=(o.getAttribute('data-nid')||'')+(nodeLoc(o.value)?'':' \\u00b7 no position');"
                                "b.appendChild(nm);"
                                "b.appendChild(id);"
                                "b.onclick=function(){nodePick(this.getAttribute('data-v'));};"
                                "box.appendChild(b);"
                            "}"
                            "if(list.length>max){"
                                "var more=document.createElement('div');"
                                "more.className='nr-more';"
                                "more.textContent=(list.length-max)+' more \\u2014 keep typing to narrow the search';"
                                "box.appendChild(more);"
                            "}"
                            "box.style.display='';"
                        "}"
                        // Marks the positioned matches on the map, over the heat
                        // layer; null clears them. fit: frame them.
                        "var nodeSearchLayer=null;"
                        // The blue marker for a node found or picked: a click on
                        // it picks it.
                        "function nodeMatchMarker(ll,label,v){"
                            "var mk=L.circleMarker(ll,{radius:8,weight:2,color:'#ffffff',fillColor:'#3fa9f5',fillOpacity:0.95});"
                            "mk.bindTooltip(label);"
                            "mk.on('click',function(){nodePick(v);});"
                            "return mk;"
                        "}"
                        "function renderNodeSearchMap(list,fit){"
                            "if(!list){"
                                "if(nodeSearchLayer&&nodeMap){nodeMap.removeLayer(nodeSearchLayer);nodeSearchLayer=null;}"
                                "if(nodeMap&&nodeMarkerLayer){var n0=nodeMarkerLayer.getLayers().length;"
                                    "setMapStatus(n0?(n0+' positioned node'+(n0===1?'':'s')):'No node positions yet');}"
                                "return;"
                            "}"
                            "ensureNodeMap();"
                            "if(!nodeMap)return;"
                            "if(nodeSearchLayer)nodeSearchLayer.clearLayers();"
                            "else nodeSearchLayer=L.featureGroup().addTo(nodeMap);"
                            "var i,o,ll,n=0;"
                            "for(i=0;i<list.length;i++){"
                                "o=list[i];"
                                "ll=nodeLoc(o.value);"
                                "if(!ll)continue;"
                                "nodeMatchMarker(ll,o.textContent,o.value).addTo(nodeSearchLayer);"
                                "n++;"
                            "}"
                            "setMapStatus(n?(n+' match'+(n===1?'':'es')+' on the map'):'No matching node has a position');"
                            "if(fit&&n)nodeMap.fitBounds(nodeSearchLayer.getBounds(),{padding:[24,24],maxZoom:13});"
                        "}"
                        // A search match or a map marker picked: the same as
                        // choosing it in the dropdown.
                        "function nodePick(v){"
                            "var sel=document.getElementById('node-select');"
                            "if(!sel)return;"
                            "sel.value=v;"
                            "selectNode();"
                        "}"
                        "function nodeSearchKey(e){"
                            "if(e.key==='Escape'){e.target.value='';renderNodeOptions();return;}"
                            "if(e.key!=='Enter')return;"
                            "e.preventDefault();"
                            "var sel=document.getElementById('node-select'),i,o;"
                            "if(!sel)return;"
                            "for(i=1;i<sel.options.length;i++){"
                                "o=sel.options[i];"
                                "if(o.disabled)continue;"
                                "nodePick(o.value);"
                                "return;"
                            "}"
                        "}"
                        "function toggleNodeFavorite(btn){"
                            "var id=btn.getAttribute('data-fav-btn');"
                            "var next=btn.getAttribute('data-fav')==='1'?'0':'1';"
                            "if(!id)return;"
                            // Every copy of this node's button, not just the one
                            // clicked: they are the same action in flight.
                            "var btns=document.querySelectorAll(\"[data-fav-btn='\"+id+\"']\"),i;"
                            "for(i=0;i<btns.length;i++)btns[i].disabled=true;"
                            "var done=function(){for(var j=0;j<btns.length;j++)btns[j].disabled=false;};"
                            "fetch('/node-favorite',{method:'POST',"
                                "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
                                "body:'id='+encodeURIComponent(id)+'&fav='+next})"
                            ".then(function(r){return r.json();})"
                            ".then(function(j){"
                                "if(!j||!j.ok)throw new Error(j&&j.error?j.error:'failed');"
                                "applyNodeFavorite(id,j.fav===1);"
                                "done();"
                            "})"
                            ".catch(function(e){"
                                "done();"
                                "alert('Could not change favorite: '+(e&&e.message?e.message:e));"
                            "});"
                        "}"
                        // ── Chat tab (omitted on Cardputer — no PSRAM) ────────
#if !defined(DEVICE_CARDPUTER_LORA_HAT)
                        // Mirrored by kTapbackTray in main_lvgl.cpp, which is what
                        // the device's Message Actions menu offers. Same set, same
                        // order — keep the two in step so a reaction means the same
                        // thing whichever side sent it.
                        "var CHAT_TAPS=['\\uD83D\\uDC4D','\\uD83D\\uDC4E','\\u203C\\uFE0F','\\u2753','\\uD83D\\uDE02','\\uD83D\\uDE22'];"
                        "var chatIsDm=false;var chatId='';var chatCursor=-1;var chatLines=[];"
                        "var chatReplyPid=0;var chatPreview={};var chatTimer=null;var chatTargetsTimer=null;"
                        "var chatPollBusy=false;var chatRenderPending=false;var chatTargetsHtml='';"
                        "function chatMaxLen(){var inp=document.getElementById('chat-input');if(!inp)return 0;var m=parseInt(inp.getAttribute('maxlength')||'0',10);return m>0?m:0;}"
                        "function chatUpdateCount(){var inp=document.getElementById('chat-input');var c=document.getElementById('chat-count');if(!inp||!c)return;var m=chatMaxLen();c.textContent=String(inp.value.length)+'/'+(m>0?String(m):'?');}"
                        "function chatComposerKey(ev){if(!ev)return;if((ev.ctrlKey||ev.metaKey)&&ev.key==='Enter'){ev.preventDefault();chatSend();}}"
                        "function chatEsc(s){return String(s==null?'':s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');}"
                        "function chatTargetChanged(){"
                            "var sel=document.getElementById('chat-target');var v=sel?sel.value:'';"
                            // Drop the in-flight guard, not just the data: the poll below has to
                            // start now rather than wait out a request for the target we just
                            // left. That request's reply is discarded by the target check in
                            // pollChat(), so both being in flight is safe.
                            "chatLines=[];chatCursor=-1;chatReplyPid=0;chatPreview={};chatPollBusy=false;"
                            "chatRenderPending=false;"
                            "chatCancelReply();"
                            "var feed=document.getElementById('chat-feed');if(feed)feed.innerHTML='';"
                            "if(v===''){chatId='';return;}"
                            "chatIsDm=(v.charAt(0)==='d');chatId=v.substring(2);"
                            "pollChat();"
                        "}"
                        "function loadChatTargets(){"
                            "fetch('/chat-targets',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){"
                                "var sel=document.getElementById('chat-target');if(!sel)return;"
                                "if(document.activeElement===sel)return;"   // don't rebuild while the user is choosing
                                "var cur=sel.value;var h='<option value=\"\">-- select --</option>';"
                                "if(d.channels&&d.channels.length){h+='<optgroup label=\"Channels\">';"
                                    "d.channels.forEach(function(c){h+='<option value=\"c:'+c.id+'\">'+chatEsc(c.name)+'</option>';});h+='</optgroup>';}"
                                "if(d.dms&&d.dms.length){h+='<optgroup label=\"Direct Messages\">';"
                                    "d.dms.forEach(function(c){h+='<option value=\"d:'+c.id+'\">'+chatEsc(c.name)+'</option>';});h+='</optgroup>';}"
                                // Every 5 s this runs; the list changes maybe once an hour.
                                // Reassigning innerHTML rebuilds the option nodes, and on a
                                // phone that is enough to shut an open dropdown and lose the
                                // scroll position inside it.
                                "if(h===chatTargetsHtml)return;chatTargetsHtml=h;"
                                "sel.innerHTML=h;if(cur)sel.value=cur;"
                            "}).catch(function(){});"
                        "}"
                        // Two guards against the same message arriving twice, because the
                        // cursor only advances when a reply lands. The device can take
                        // longer than the 2 s poll interval to answer — it is building up
                        // to 6 KB of JSON between mesh work — and sending adds a poll of
                        // its own 600 ms later. Two requests in flight at once therefore
                        // both carry the pre-send cursor, both come back with the new
                        // message, and both used to append it. The feed then showed it
                        // repeated: chatRender() groups consecutive lines by packet id, so
                        // the duplicates were glued into one bubble carrying the same text
                        // two or three times over.
                        //
                        // The authoritative fix is the per-line index: every line carries
                        // its absolute "i" (see handleGetChatData), so anything at or
                        // below the cursor is something we already hold, whatever order
                        // the replies arrive in. The busy flag is the cheaper half — it
                        // stops a slow device accumulating a queue of redundant requests.
                        "function pollChat(){"
                            "if(chatId===''||chatPollBusy)return;"
                            "var q=chatIsDm?('dm='+encodeURIComponent(chatId)):('ch='+encodeURIComponent(chatId));"
                            "var url='/chat-data?'+q+(chatCursor>=0?('&after='+chatCursor):'');"
                            // Which conversation this reply belongs to. A target switch
                            // mid-flight would otherwise splice the old one's lines into
                            // the new one's feed.
                            "var wantId=chatId;var wantDm=chatIsDm;"
                            "chatPollBusy=true;"
                            "fetch(url,{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){"
                                "chatPollBusy=false;"
                                "if(!d||!d.lines)return;"
                                "if(chatId!==wantId||chatIsDm!==wantDm)return;"
                                "var added=0;"
                                "for(var i=0;i<d.lines.length;i++){var ln=d.lines[i];"
                                    "if(typeof ln.i==='number'){if(ln.i<=chatCursor)continue;chatCursor=ln.i;}"
                                    "chatLines.push(ln);added++;}"
                                "if(typeof d.total==='number'&&d.total-1>chatCursor)chatCursor=d.total-1;"
                                // The poll runs every 2 s and almost always brings nothing.
                                // Rendering anyway rebuilt the whole feed from a string each
                                // time — hundreds of nodes on a busy channel, since every
                                // message carries a Reply button and six tapbacks — which is
                                // most of why this tab felt heavy. chatRenderPending covers
                                // the case where a render was skipped because the composer
                                // had focus: without it, a feed that changed during typing
                                // would wait for the *next* new message to be drawn.
                                "if(added>0||chatRenderPending)chatRender();"
                            "}).catch(function(){chatPollBusy=false;});"
                        "}"
                        // Message text without its "HH:MM [Name] " prefix, and the name.
                        "function chatStrip(t){var m=/^.{0,64}?[\\]>] /.exec(t);return m?t.substring(m[0].length):t;}"
                        "function chatWho(t){var m=/\\[([^\\]]+)\\] /.exec(t.substring(0,72));return m?m[1]:'You';}"
                        // A reply opens with a quote of what it answers; clicking it
                        // scrolls there. Reactions are chips under the original.
                        "function chatBubble(pid,mine,txt,rp,chips){"
                            "if(pid!==0)chatPreview[pid]=txt;"
                            "var s='<div'+(pid!==0?' id=\"cm-'+pid+'\"':'')+' class=\"chat-msg'+(mine?' mine':'')+'\">';"
                            "if(rp){var o=chatPreview[rp];"
                                "s+='<div class=\"chat-quote\" onclick=\"chatJump('+rp+')\">\\u21AA '+(o?chatEsc(chatWho(o))+': '+chatEsc(chatStrip(o).substring(0,60)):'earlier message')+'</div>';}"
                            "s+='<div class=\"chat-body\">'+chatEsc(txt)+'</div>';"
                            "if(chips){s+='<div class=\"chat-chips\">';for(var e in chips)s+='<span class=\"chat-chip\">'+chatEsc(e)+(chips[e]>1?' '+chips[e]:'')+'</span>';s+='</div>';}"
                            // No reply/tapback on our own messages.
                            "if(pid!==0&&!mine){s+='<div class=\"chat-actions\">';"
                                "s+='<button type=\"button\" class=\"chat-mini\" onclick=\"chatReply('+pid+')\">Reply</button>';"
                                "for(var k=0;k<CHAT_TAPS.length;k++)s+='<button type=\"button\" class=\"chat-tap\" onclick=\"chatTapback('+pid+','+k+')\">'+CHAT_TAPS[k]+'</button>';"
                                "s+='</div>';}"
                            "return s+'</div>';"
                        "}"
                        "function chatRender(){"
                            "var feed=document.getElementById('chat-feed');if(!feed)return;"
                            // Don't rebuild the feed while the user is composing — reassigning
                            // innerHTML blurs the focused input / drops the mobile keyboard.
                            "var ae=document.activeElement;if(ae&&ae.id==='chat-input'){chatRenderPending=true;return;}"
                            "chatRenderPending=false;"
                            "var atBottom=feed.scrollHeight-feed.scrollTop-feed.clientHeight<40;"
                            "var h='';var i=0;var L=chatLines;"
                            // Tally reactions first: they may arrive long after the original.
                            "var taps={};for(var r=0;r<L.length;r++){var x=L[r];"
                                "if(!x.rx||!x.rp||/^\\s\\s/.test(x.t))continue;"
                                "var em=chatStrip(x.t).trim();if(!em)continue;"
                                "var tb=taps[x.rp]||(taps[x.rp]={});tb[em]=(tb[em]||0)+1;}"
                            // A wrapped message is stored one line per wrap, and
                            // ChannelMgr::_wordWrap() pushes those lines LAST SEGMENT
                            // FIRST — the device draws its feed newest-at-top, so
                            // reversed in storage is correct order on the glass. Read
                            // the group back to front to put it together again.
                            // L[j-1] is the opening line: it carries the "HH:MM [id] "
                            // prefix and is the only line given an ack state, so it is
                            // also where `mine` has to come from.
                            "while(i<L.length){var pid=L[i].pid;var j=i+1;"
                                "if(pid!==0){while(j<L.length&&L[j].pid===pid)j++;}"
                                "var head=L[j-1];var txt=head.t;"
                                "for(var k=j-2;k>=i;k--){var seg=L[k].t.replace(/^\\s{1,2}/,'');if(seg)txt+=' '+seg;}"
                                "if(head.rx){i=j;continue;}"
                                "h+=chatBubble(pid,!!head.mine,txt,head.rp||0,pid!==0?taps[pid]:null);i=j;}"
                            "feed.innerHTML=h;if(atBottom)feed.scrollTop=feed.scrollHeight;"
                        "}"
                        "function chatReply(pid){"
                            "chatReplyPid=pid;var bar=document.getElementById('chat-reply-bar');"
                            "var t=document.getElementById('chat-reply-text');"
                            "if(t)t.textContent='Replying: '+String(chatPreview[pid]||'').substring(0,60);"
                            "if(bar)bar.style.display='flex';"
                            "var inp=document.getElementById('chat-input');if(inp){inp.scrollIntoView({block:'nearest'});inp.focus();}"
                        "}"
                        "function chatJump(pid){var el=document.getElementById('cm-'+pid);if(!el)return;"
                            "el.scrollIntoView({block:'center',behavior:'smooth'});el.classList.add('flash');"
                            "setTimeout(function(){el.classList.remove('flash');},1200);}"
                        "function chatCancelReply(){chatReplyPid=0;var bar=document.getElementById('chat-reply-bar');if(bar)bar.style.display='none';}"
                        "function chatPost(text,reply,emoji){"
                            "if(chatId==='')return;"
                            "var body='dm='+(chatIsDm?'1':'0')+'&id='+encodeURIComponent(chatId)+'&text='+encodeURIComponent(text)+'&reply='+(reply||0)+'&emoji='+(emoji||0);"
                            "fetch('/chat-send',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body})"
                                ".then(function(){setTimeout(pollChat,600);}).catch(function(){});"
                        "}"
                        "function chatTapback(pid,idx){chatPost(CHAT_TAPS[idx],pid,1);}"
                        "function chatSend(){"
                            "var inp=document.getElementById('chat-input');if(!inp)return;var t=String(inp.value||'').trim();if(!t)return;"
                            "chatPost(t,chatReplyPid,0);inp.value='';chatUpdateCount();chatCancelReply();"
                        "}"
                        "function chatLiveOn(){var c=document.getElementById('chat-live');return c?c.checked:true;}"
                        "function chatStartTimers(){if(!chatTimer)chatTimer=setInterval(pollChat,2000);if(!chatTargetsTimer)chatTargetsTimer=setInterval(loadChatTargets,5000);}"
                        "function chatStopTimers(){if(chatTimer){clearInterval(chatTimer);chatTimer=null;}if(chatTargetsTimer){clearInterval(chatTargetsTimer);chatTargetsTimer=null;}}"
                        "function chatBodySync(){var b=document.getElementById('chat-body');if(b)b.style.display=chatLiveOn()?'':'none';chatUpdateCount();}"
                        "function chatLiveToggle(){chatBodySync();if(chatLiveOn()){loadChatTargets();if(chatId!=='')pollChat();chatStartTimers();}else{chatStopTimers();}}"
                        "function startChatPolling(){chatBodySync();loadChatTargets();if(chatId!=='')pollChat();if(chatLiveOn())chatStartTimers();}"
                        "function stopChatPolling(){chatStopTimers();}"
#endif
#if HAS_VNC_HOST
                        "var remotePollTimer=null,remoteSession=null,remoteTok=null,remoteSrc='';"
                        // The VNC token lives for this browser session only
                        // (sessionStorage), tagged with the device's VNC
                        // session so a token from before the host was last
                        // re-enabled is dropped rather than tried.
                        "function remoteTokGet(){"
                            "if(!remoteTok){try{remoteTok=JSON.parse(sessionStorage.getItem('vncTok')||'null');}catch(e){remoteTok=null;}}"
                            "if(remoteTok&&(remoteSession===null||remoteTok.s!==remoteSession||!/^[0-9a-f]{32,64}$/.test(remoteTok.t||'')))remoteTokDrop();"
                            "return remoteTok?remoteTok.t:null;"
                        "}"
                        "function remoteTokDrop(){remoteTok=null;try{sessionStorage.removeItem('vncTok');}catch(e){}}"
                        "function remoteFrameSync(on){"
                            "var frame=document.getElementById('remote-frame');"
                            "var lock=document.getElementById('remote-lock');"
                            "var panel=document.getElementById('tab-remote');"
                            "if(!frame)return;"
                            "var active=!!(panel&&panel.classList.contains('active'));"
                            "var tok=on?remoteTokGet():null;"
                            "if(lock)lock.style.display=(on&&active&&!tok)?'':'none';"
                            "frame.style.display=(on&&active&&tok)?'block':'none';"
                            "var want=(on&&active&&tok)?frame.getAttribute('data-src')+'#t='+tok:'';"
                            "if(want!==remoteSrc){remoteSrc=want;frame.setAttribute('src',want||'about:blank');}"
                        "}"
                        "function remoteUnlock(){"
                            "var input=document.getElementById('remote-pin'),msg=document.getElementById('remote-lock-msg'),btn=document.getElementById('remote-unlock');"
                            "if(!input)return;"
                            "var pin=(input.value||'').replace(/\\D/g,'');"
                            "if(pin.length!==6){if(msg)msg.textContent='Enter the 6-digit PIN shown on the device.';return;}"
                            "if(btn)btn.disabled=true;"
                            "fetch('/vnc-unlock',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'pin='+pin})"
                                ".then(function(r){return r.json().catch(function(){return {ok:false,error:'Unlock failed ('+r.status+').'};});})"
                                ".then(function(d){"
                                    "if(btn)btn.disabled=false;"
                                    "if(d&&d.ok&&d.token){"
                                        "remoteSession=d.session;remoteTok={t:d.token,s:d.session};"
                                        "try{sessionStorage.setItem('vncTok',JSON.stringify(remoteTok));}catch(e){}"
                                        "input.value='';if(msg)msg.textContent='';pollRemote();"
                                    "}else if(msg){msg.textContent=(d&&d.error)||'Unlock failed.';}"
                                "})"
                                ".catch(function(){if(btn)btn.disabled=false;if(msg)msg.textContent='Unlock failed.';});"
                        "}"
                        // The viewer reports a refused socket (token no longer
                        // valid) so the PIN prompt comes back on its own.
                        "window.addEventListener('message',function(e){"
                            "var frame=document.getElementById('remote-frame');"
                            "if(!frame||e.source!==frame.contentWindow||!e.data||e.data.vnc!=='locked')return;"
                            "remoteTokDrop();pollRemote();"
                        "});"
                        "function remoteRender(d){"
                            "var box=document.getElementById('remote-enabled');"
                            "var status=document.getElementById('remote-status');"
                            "if(!box)return;"
                            "if(typeof d.session==='number')remoteSession=d.session;"
                            "box.disabled=!!d.pending;box.checked=!!d.enabled;"
                            "if(status){"
                                "if(d.pending)status.textContent='Applying...';"
                                "else if(!d.enabled)status.textContent='Remote host is off.';"
                                "else if(d.client)status.textContent='Browser connected.';"
                                "else if(d.running)status.textContent=remoteTokGet()?'Ready.':'Locked. Enter the PIN shown on the device.';"
                                "else status.textContent='Starting remote host...';"
                            "}"
                            "remoteFrameSync(!!d.enabled);"
                        "}"
                        "function pollRemote(){"
                            "fetch('/vnc-status',{cache:'no-store'}).then(function(r){if(!r.ok)throw new Error();return r.json();})"
                                ".then(remoteRender).catch(function(){var s=document.getElementById('remote-status');if(s)s.textContent='Remote status unavailable.';});"
                        "}"
                        "function remoteToggle(){"
                            "var box=document.getElementById('remote-enabled');if(!box)return;"
                            "box.disabled=true;var desired=box.checked;"
                            "var status=document.getElementById('remote-status');if(status)status.textContent=desired?'Starting...':'Stopping...';"
                            "fetch('/vnc-toggle',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'enabled='+(desired?'1':'0')})"
                                ".then(function(r){if(!r.ok)throw new Error();setTimeout(pollRemote,150);})"
                                ".catch(function(){box.disabled=false;pollRemote();});"
                        "}"
                        "function startRemotePolling(){pollRemote();if(!remotePollTimer)remotePollTimer=setInterval(pollRemote,1000);}"
                        "function stopRemotePolling(){if(remotePollTimer){clearInterval(remotePollTimer);remotePollTimer=null;}remoteFrameSync(false);}"
#endif
                        // Copying the public key. navigator.clipboard is a
                        // secure-context API and this page is served over plain
                        // http, so it is absent in most of the cases that
                        // matter -- the execCommand path below is the one that
                        // actually runs, and the prompt is there for the
                        // browsers that refuse both.
#if HAS_ADMIN_TERMINAL
                        // The terminal polls only while it is open, and only
                        // repaints when the session's revision moves -- the same
                        // shape /chat-data uses, for the same reason: this runs
                        // against a radio, not a server.
                        "var adminNode='',adminTimer=null,adminRev=-1;"
                        "function adminOpen(id){"
                            "adminNode=id;adminRev=-1;"
                            // The id until the first reply carries the name.
                            // Rewritten rather than left blank so a reopen never
                            // shows the previous node's title.
                            "document.getElementById('admin-title').textContent='Remote Admin '+id;"
                            "document.getElementById('admin-log').textContent='';"
                            "document.getElementById('adminModal').classList.add('open');"
                            "document.getElementById('admin-line').focus();"
                            "adminPost('');"
                            "if(!adminTimer)adminTimer=setInterval(adminPoll,1200);"
                        "}"
                        "function adminClose(){"
                            "adminHelpHide();"
                            "document.getElementById('adminModal').classList.remove('open');"
                            "if(adminTimer){clearInterval(adminTimer);adminTimer=null;}"
                        "}"
                        "function adminRender(d){"
                            // Before the revision gate: the name can arrive on a
                            // poll that has no new lines, and the title is not
                            // part of the transcript the revision tracks.
                            "document.getElementById('admin-title').textContent="
                                "'Remote Admin '+(d.name||d.node);"
                            "if(d.rev===adminRev)return;adminRev=d.rev;"
                            "var box=document.getElementById('admin-log');"
                            "var cls=['#8fb8ff','#a7c7ff','#8ef2b8','#ff9f9f','#e8e8e8'];"
                            "box.innerHTML='';"
                            "d.lines.forEach(function(l){"
                                "var s=document.createElement('div');"
                                "s.style.color=cls[l.k]||cls[4];s.textContent=l.t;box.appendChild(s);"
                            "});"
                            "box.scrollTop=box.scrollHeight;"
                            "var t=document.getElementById('admin-line');"
                            "t.placeholder=d.confirm?'type confirm':(d.busy?'waiting...':'help');"
                        "}"
                        "function adminPoll(){"
                            "fetch('/admin-data').then(function(r){return r.json();})"
                                ".then(adminRender).catch(function(){});"
                        "}"
                        "function adminPost(line){"
                            "fetch('/admin-send',{method:'POST',"
                                "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
                                "body:'node='+encodeURIComponent(adminNode)+'&line='+encodeURIComponent(line)})"
                                ".then(function(r){return r.ok?r.json():null;})"
                                ".then(function(d){if(d)adminRender(d);}).catch(function(){});"
                        "}"
                        // Verify reports on the button itself. The probe is a
                        // radio round trip of up to 30 s, so the honest answer
                        // now is "asked", and the badge changes on the next
                        // reload rather than under the cursor.
                        "function adminVerify(id,btn){"
                            "var was=btn.textContent;btn.disabled=true;btn.textContent='Asking...';"
                            "fetch('/admin-peer-verify',{method:'POST',"
                                "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
                                "body:'node='+encodeURIComponent(id)})"
                                ".then(function(r){return r.text().then(function(t){return {ok:r.ok,t:t};});})"
                                ".then(function(r){"
                                    "btn.textContent=r.ok?'Asked':r.t;"
                                    "setTimeout(function(){btn.disabled=false;btn.textContent=was;},"
                                        "r.ok?30000:4000);"
                                "})"
                                ".catch(function(){btn.disabled=false;btn.textContent=was;});"
                        "}"
                        // Rescan runs the whole favourites sweep, so its
                        // button counts the cooldown down in place. The server
                        // is the one that enforces it -- this only saves the
                        // user a click that was going to be refused -- and a
                        // 429 hands back the remainder, which is what a page
                        // reloaded mid-cooldown uses to pick the count back up.
                        "function adminRescanCool(btn,secs){"
                            "btn.disabled=true;"
                            "(function tick(){"
                                "if(secs<=0){btn.disabled=false;btn.textContent='Scan favourites';return;}"
                                "btn.textContent='Scan favourites ('+secs+'s)';"
                                "secs--;setTimeout(tick,1000);"
                            "})();"
                        "}"
                        "function adminRescan(btn){"
                            "var out=document.getElementById('admin-rescan-msg');"
                            "btn.disabled=true;btn.textContent='Starting...';"
                            "fetch('/admin-rescan',{method:'POST'})"
                                ".then(function(r){return r.text().then(function(t){"
                                    "return {ok:r.ok,code:r.status,t:t};});})"
                                ".then(function(r){"
                                    "out.textContent=r.t;"
                                    "if(r.ok){adminRescanCool(btn,30);return;}"
                                    // The remainder is in the message; 30 is the
                                    // safe fallback when it is not parseable.
                                    "var m=r.code===429?/(\\d+)s/.exec(r.t):null;"
                                    "adminRescanCool(btn,m?parseInt(m[1],10):0);"
                                "})"
                                ".catch(function(){"
                                    "out.textContent='Could not reach the device';"
                                    "btn.disabled=false;btn.textContent='Scan favourites';"
                                "});"
                        "}"
                        "function adminHelpHide(){"
                            "document.getElementById('admin-help-card').style.display='none';"
                            "document.getElementById('admin-row').style.justifyContent='center';"
                        "}"
                        // The row only moves once the text is in hand. Shifting
                        // left and then failing the fetch would leave the
                        // terminal off-centre next to nothing.
                        "function adminHelpShow(){"
                            "var card=document.getElementById('admin-help-card');"
                            "fetch('/admin-help').then(function(r){return r.text();})"
                                ".then(function(t){"
                                    "document.getElementById('admin-help').textContent=t;"
                                    "card.style.display='';"
                                    "document.getElementById('admin-row').style"
                                        ".justifyContent='flex-start';"
                                "}).catch(function(){});"
                        "}"
                        // A bare `help` opens the panel beside the terminal and
                        // never reaches the radio -- the table is the firmware's
                        // own, so there is nothing to ask the remote. `help <cmd>`
                        // still goes through, since that answer is one line and
                        // belongs in the transcript with the rest of the session.
                        "function adminSend(){"
                            "var t=document.getElementById('admin-line');"
                            "var v=t.value;t.value='';"
                            "if(v.trim()==='help'){adminHelpShow();return;}"
                            "adminPost(v);"
                        "}"
#endif
                        "function copyPubKey(el){"
                            "var k=el.getAttribute('data-key');"
                            "var done=function(){"
                                "var t=el.textContent;el.classList.add('copied');el.textContent='KEY COPIED';"
                                "setTimeout(function(){el.textContent=t;el.classList.remove('copied');},1200);"
                            "};"
                            "if(navigator.clipboard&&window.isSecureContext){"
                                "navigator.clipboard.writeText(k).then(done).catch(function(){fallbackCopy(k,done);});"
                            "}else{fallbackCopy(k,done);}"
                        "}"
                        "function fallbackCopy(text,done){"
                            "var ta=document.createElement('textarea');"
                            "ta.value=text;ta.setAttribute('readonly','');"
                            "ta.style.position='fixed';ta.style.top='-1000px';"
                            "document.body.appendChild(ta);ta.select();"
                            "var okc=false;try{okc=document.execCommand('copy');}catch(e){okc=false;}"
                            "document.body.removeChild(ta);"
                            "if(okc){done();}else{window.prompt('Public key (copy with Ctrl/Cmd+C):',text);}"
                        "}"
                        // appTab() (kAppJs) marks the pane and every nav that
                        // points at it; this adds what each tab starts or stops.
                        "function switchTab(tab){"
                            "if(!document.getElementById('tab-'+tab))tab='config';"
                            "appTab(tab);"
                            "var isLive=(tab==='live');"
                            "var isMap=(tab==='map');"
                            "if(isLive)startLivePolling();else stopLivePolling();"
#if HAS_VNC_HOST
                            "if(tab==='remote')startRemotePolling();else stopRemotePolling();"
#endif
#if HAS_WEB_FILES
                            "if(tab==='files')filesOpen();"
#endif
#if !defined(DEVICE_CARDPUTER_LORA_HAT)
                            "if(tab==='chat')startChatPolling();else stopChatPolling();"
#endif
                            "if(isMap){"
                                "ensureNodeMap();"
                                "var _s=document.getElementById('node-select');"
                                "if(_s&&_s.value!=='')selectNode();"
                                "setTimeout(function(){if(nodeMap)nodeMap.invalidateSize();},0);"
                            "}"
                        "}"
                        "window.addEventListener('resize',function(){"
                            "var mapTab=document.getElementById('tab-map');"
                            "if(mapTab&&mapTab.classList.contains('active')&&nodeMap)nodeMap.invalidateSize();"
                        "});"
                        "</script>";

    html += "</body></html>";
    sendChunk(html);
    if (gSendAborted) {
        server.client().stop();
        logWifiHeapDiag("config page abandoned");
        return;
    }
    server.sendContent("");   // empty chunk signals end of response
    logWifiHeapDiag("config page sent");
}

// ── Onboarding (WiFi setup) ───────────────────────────────────

