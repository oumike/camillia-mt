// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: Live traffic view and chart sampling.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

// Split a live-feed line into the clock liveBuildPrefix() put on it and the
// body after it. Anchored: the feed's own lines start with the clock, so
// liveFindClock() is given a one-byte window rather than allowed to find a time
// somewhere inside the body.
static bool liveTimestampAndBody(const char *s, char *tsOut, size_t tsOutLen, const char **bodyOut) {
    if (!s) s = "";
    if (tsOut && tsOutLen > 0) tsOut[0] = '\0';
    if (!bodyOut) return false;

    while (*s == ' ') s++;
    *bodyOut = s;

    size_t len = strlen(s);
    if (len < 6) return false;

    const char *tsStart = nullptr;
    const char *tsEnd = liveFindClock(s, 1, &tsStart);
    // "--:--" is not a time, so liveFindClock() does not match it; the feed
    // prints it like any other prefix and the body still has to be split off.
    bool unset = (s[0] == '-' && s[1] == '-' && s[2] == ':' &&
                  s[3] == '-' && s[4] == '-' && s[5] == ' ');
    if (unset) { tsStart = s; tsEnd = s + 5; }
    // A clock has to be followed by the separating space, or what was found is
    // the start of the body rather than a prefix.
    if (!tsStart || !tsEnd || *tsEnd != ' ') return false;

    const size_t tsLen = (size_t)(tsEnd - tsStart);
    if (tsOut && tsOutLen > tsLen) {
        memcpy(tsOut, tsStart, tsLen);
        tsOut[tsLen] = '\0';
    }
    s = tsEnd;
    while (*s == ' ') s++;
    *bodyOut = s;
    return true;
}

static const char *livePortLabel(const char *tag) {
    if (!tag || !tag[0]) return "data";
    if (strcmp(tag, "T") == 0) return "text";
    if (strcmp(tag, "N") == 0) return "nodeinfo";
    if (strcmp(tag, "P") == 0) return "position";
    if (strcmp(tag, "E") == 0) return "telemetry";
    if (strcmp(tag, "R") == 0) return "traceroute";
    if (strcmp(tag, "A") == 0) return "routing";
    return "data";
}

static const char *liveDestLabel(const char *tag) {
    if (!tag || !tag[0]) return "node";
    if (strcmp(tag, "B") == 0 || strcmp(tag, "BCAST") == 0) return "broadcast";
    if (strcmp(tag, "M") == 0) return "me";
    if (strcmp(tag, "U") == 0) return "node";
    return "node";
}

static const char *liveTxResultLabel(const char *tag) {
    if (!tag || !tag[0]) return "unknown";
    if (strcmp(tag, "OK") == 0) return "sent";
    if (strcmp(tag, "ER") == 0) return "failed";
    if (strcmp(tag, "NR") == 0) return "radio not ready";
    return tag;
}

static const char *liveTelemetryKindLabel(const char *tag) {
    if (!tag || !tag[0]) return "telemetry";
    if (strcmp(tag, "D") == 0) return "device telemetry";
    if (strcmp(tag, "E") == 0) return "environment telemetry";
    return "telemetry";
}

static const char *routingErrorName(uint32_t errorReason) {
    switch (errorReason) {
        case 0:  return "NONE";
        case 1:  return "NO_ROUTE";
        case 2:  return "GOT_NAK";
        case 3:  return "TIMEOUT";
        case 4:  return "NO_INTERFACE";
        case 5:  return "MAX_RETRANSMIT";
        case 6:  return "NO_CHANNEL";
        case 7:  return "TOO_LARGE";
        case 8:  return "NO_RESPONSE";
        case 9:  return "DUTY_CYCLE_LIMIT";
        case 35: return "PKI_UNKNOWN_PUBKEY";
        default: return nullptr;
    }
}

static void formatLiveLineText(const DisplayLine &dl, char *out, size_t outLen) {
    if (!out || outLen == 0) return;
    out[0] = '\0';

    char ts[LIVE_CLOCK_BUF];
    const char *body = "";
    liveTimestampAndBody(dl.text, ts, sizeof(ts), &body);

    char who[20] = {0};
    char dst[8] = {0};
    char tag[12] = {0};
    char stat[12] = {0};
    char id[16] = {0};
    int ch = -1;

    char hashHex[4] = {0};
    if (sscanf(body, "R %19[^>]>%7s %11s c%d h%3s", who, dst, tag, &ch, hashHex) == 5) {
        if (ts[0]) snprintf(out, outLen, "%s RX %s from %s to %s on ch%d hash %s",
                            ts, livePortLabel(tag), who, liveDestLabel(dst), ch, hashHex);
        else snprintf(out, outLen, "RX %s from %s to %s on ch%d hash %s",
                      livePortLabel(tag), who, liveDestLabel(dst), ch, hashHex);
        return;
    }

    if (sscanf(body, "R %19[^>]>%7s %11s c%d", who, dst, tag, &ch) == 4) {
        if (ts[0]) snprintf(out, outLen, "%s RX %s from %s to %s on ch%d",
                            ts, livePortLabel(tag), who, liveDestLabel(dst), ch);
        else snprintf(out, outLen, "RX %s from %s to %s on ch%d",
                      livePortLabel(tag), who, liveDestLabel(dst), ch);
        return;
    }

    uint32_t reqId = 0;
    uint32_t err = 0;
    if (sscanf(body, "R ACK %19s %8lx h%3s", who, &reqId, hashHex) == 3) {
        if (ts[0]) snprintf(out, outLen, "%s RX routing ACK from %s req:%08lX hash:%s",
                            ts, who, (unsigned long)reqId, hashHex);
        else snprintf(out, outLen, "RX routing ACK from %s req:%08lX hash:%s",
                      who, (unsigned long)reqId, hashHex);
        return;
    }

    if (sscanf(body, "R NAK %19s %8lx err%lu h%3s", who, &reqId, &err, hashHex) == 4) {
        const char *errName = routingErrorName(err);
        if (ts[0]) {
            if (errName) snprintf(out, outLen, "%s RX routing NAK from %s req:%08lX %s(%lu) hash:%s",
                                  ts, who, (unsigned long)reqId, errName, (unsigned long)err, hashHex);
            else snprintf(out, outLen, "%s RX routing NAK from %s req:%08lX err:%lu hash:%s",
                          ts, who, (unsigned long)reqId, (unsigned long)err, hashHex);
        } else {
            if (errName) snprintf(out, outLen, "RX routing NAK from %s req:%08lX %s(%lu) hash:%s",
                                  who, (unsigned long)reqId, errName, (unsigned long)err, hashHex);
            else snprintf(out, outLen, "RX routing NAK from %s req:%08lX err:%lu hash:%s",
                          who, (unsigned long)reqId, (unsigned long)err, hashHex);
        }
        return;
    }

    if (sscanf(body, "R %19s ENC %11s", who, stat) == 2) {
        if (ts[0]) snprintf(out, outLen, "%s RX encrypted packet from %s (hash %s)",
                            ts, who, stat);
        else snprintf(out, outLen, "RX encrypted packet from %s (hash %s)",
                      who, stat);
        return;
    }

    if (sscanf(body, "T ACK %19s %11s", who, stat) == 2) {
        if (ts[0]) snprintf(out, outLen, "%s TX routing ACK to %s (%s)",
                            ts, who, stat);
        else snprintf(out, outLen, "TX routing ACK to %s (%s)", who, stat);
        return;
    }

    if (sscanf(body, "T TXT %7s c%d %15s", dst, &ch, id) == 3) {
        if (ts[0]) snprintf(out, outLen, "%s TX text to %s on ch%d id:%s",
                            ts, liveDestLabel(dst), ch, id);
        else snprintf(out, outLen, "TX text to %s on ch%d id:%s",
                      liveDestLabel(dst), ch, id);
        return;
    }

    if (sscanf(body, "T TXT %7s NR", dst) == 1) {
        if (ts[0]) snprintf(out, outLen, "%s TX text to %s not sent (radio not ready)",
                            ts, liveDestLabel(dst));
        else snprintf(out, outLen, "TX text to %s not sent (radio not ready)",
                      liveDestLabel(dst));
        return;
    }

    if (sscanf(body, "T TXT %7s ER", dst) == 1) {
        if (ts[0]) snprintf(out, outLen, "%s TX text to %s FAILED",
                            ts, liveDestLabel(dst));
        else snprintf(out, outLen, "TX text to %s FAILED", liveDestLabel(dst));
        return;
    }

    if (sscanf(body, "T POS %7s %15s %11s", dst, id, stat) == 3) {
        if (ts[0]) snprintf(out, outLen, "%s TX position to %s id:%s (%s)",
                            ts, liveDestLabel(dst), id, stat);
        else snprintf(out, outLen, "TX position to %s id:%s (%s)",
                      liveDestLabel(dst), id, stat);
        return;
    }

    if (sscanf(body, "T NOD %7s %19s %11s", dst, who, stat) == 3) {
        if (ts[0]) snprintf(out, outLen, "%s TX nodeinfo %s to %s (%s)",
                            ts,
                            (strcmp(dst, "U") == 0) ? "unicast" : "broadcast",
                            who,
                            stat);
        else snprintf(out, outLen, "TX nodeinfo %s to %s (%s)",
                      (strcmp(dst, "U") == 0) ? "unicast" : "broadcast",
                      who,
                      stat);
        return;
    }

    if (sscanf(body, "T TEL %7s %15s %11s", dst, id, stat) == 3) {
        if (ts[0]) snprintf(out, outLen, "%s TX %s to broadcast id:%s (%s)",
                            ts,
                            liveTelemetryKindLabel(dst),
                            id,
                            liveTxResultLabel(stat));
        else snprintf(out, outLen, "TX %s to broadcast id:%s (%s)",
                      liveTelemetryKindLabel(dst),
                      id,
                      liveTxResultLabel(stat));
        return;
    }

    if (sscanf(body, "T NBR %7s %15s %11s", dst, id, stat) == 3) {
        if (ts[0]) snprintf(out, outLen, "%s TX neighborhood info to %s id:%s (%s)",
                            ts,
                            liveDestLabel(dst),
                            id,
                            liveTxResultLabel(stat));
        else snprintf(out, outLen, "TX neighborhood info to %s id:%s (%s)",
                      liveDestLabel(dst),
                      id,
                      liveTxResultLabel(stat));
        return;
    }

    if (sscanf(body, "T DM %11s %19s %15s", tag, who, id) == 3) {
        if (ts[0]) snprintf(out, outLen, "%s TX DM to %s via %s id:%s",
                            ts, who, tag, id);
        else snprintf(out, outLen, "TX DM to %s via %s id:%s", who, tag, id);
        return;
    }

    if (sscanf(body, "T DM ER %11s", stat) == 1) {
        if (ts[0]) snprintf(out, outLen, "%s TX DM FAILED (%s)", ts, stat);
        else snprintf(out, outLen, "TX DM FAILED (%s)", stat);
        return;
    }

    snprintf(out, outLen, "%s", dl.text);
}

enum LiveTrafficClass {
    LIVE_TRAFFIC_DEFAULT = 0,
    LIVE_TRAFFIC_ERROR,
    LIVE_TRAFFIC_TX_ACK,
    LIVE_TRAFFIC_TX_TEXT,
    LIVE_TRAFFIC_TX_NODE,
    LIVE_TRAFFIC_TX_POS,
    LIVE_TRAFFIC_TX_TLM,
    LIVE_TRAFFIC_TX_DM,
    LIVE_TRAFFIC_RX_ACK,
    LIVE_TRAFFIC_RX_TEXT,
    LIVE_TRAFFIC_RX_NODE,
    LIVE_TRAFFIC_RX_POS,
    LIVE_TRAFFIC_RX_TLM,
    LIVE_TRAFFIC_RX_ENC,
    LIVE_TRAFFIC_RX_OTHER,
    LIVE_TRAFFIC_TX_OTHER,
};

static LiveTrafficClass classifyLiveTraffic(const DisplayLine &dl) {
    const char *body = "";
    liveTimestampAndBody(dl.text, nullptr, 0, &body);
    if (!body[0]) return LIVE_TRAFFIC_DEFAULT;

    if (strstr(body, " ER") || strncmp(body, "R NAK", 5) == 0) return LIVE_TRAFFIC_ERROR;
    if (strncmp(body, "T ACK", 5) == 0) return LIVE_TRAFFIC_TX_ACK;
    if (strncmp(body, "R ACK", 5) == 0) return LIVE_TRAFFIC_RX_ACK;
    if (strncmp(body, "T DM", 4) == 0) return LIVE_TRAFFIC_TX_DM;
    if (strncmp(body, "R ", 2) == 0 && strstr(body, " ENC ")) return LIVE_TRAFFIC_RX_ENC;

    char who[20] = {0};
    char dst[8] = {0};
    char tag[12] = {0};
    int ch = -1;
    if (sscanf(body, "R %19[^>]>%7s %11s c%d", who, dst, tag, &ch) == 4) {
        if (strcmp(tag, "T") == 0) return LIVE_TRAFFIC_RX_TEXT;
        if (strcmp(tag, "N") == 0) return LIVE_TRAFFIC_RX_NODE;
        if (strcmp(tag, "P") == 0) return LIVE_TRAFFIC_RX_POS;
        if (strcmp(tag, "E") == 0) return LIVE_TRAFFIC_RX_TLM;
        return LIVE_TRAFFIC_RX_OTHER;
    }

    if (strncmp(body, "T TXT", 5) == 0) return LIVE_TRAFFIC_TX_TEXT;
    if (strncmp(body, "T NOD", 5) == 0) return LIVE_TRAFFIC_TX_NODE;
    if (strncmp(body, "T POS", 5) == 0) return LIVE_TRAFFIC_TX_POS;
    if (strncmp(body, "T TLM", 5) == 0 || strncmp(body, "T TEL", 5) == 0) return LIVE_TRAFFIC_TX_TLM;
    if (strncmp(body, "R ", 2) == 0) return LIVE_TRAFFIC_RX_OTHER;
    if (strncmp(body, "T ", 2) == 0) return LIVE_TRAFFIC_TX_OTHER;

    return LIVE_TRAFFIC_DEFAULT;
}

// ── Live traffic filter ──────────────────────────────────────────────────────
// The enum and its state live further up, beside the picker's own statics, which
// need LIVE_FILTER_COUNT to size their arrays. These two are here instead
// because they are the half that has to know about LiveTrafficClass, defined
// just above.
static const char *liveFilterName(uint8_t f) {
    switch (f) {
        case LIVE_FILTER_TEXT:  return TR("Text");
        // "DMs" rather than "Direct messages": this name has to fit a grid cell
        // on a 240px panel and the header chip beside it.
        case LIVE_FILTER_DM:    return TR("DMs");
        case LIVE_FILTER_POS:   return TR("Position");
        case LIVE_FILTER_TLM:   return TR("Telemetry");
        case LIVE_FILTER_NODE:  return TR("Node info");
        case LIVE_FILTER_ACK:   return TR("ACKs");
        case LIVE_FILTER_ENC:   return TR("Encrypted");
        case LIVE_FILTER_ERROR: return TR("Errors");
        case LIVE_FILTER_OTHER: return TR("Other");
        default:                return TR("All");
    }
}

static bool liveFilterMatches(uint8_t f, LiveTrafficClass cls) {
    if (f == LIVE_FILTER_ALL) return true;
    switch (f) {
        case LIVE_FILTER_TEXT:  return cls == LIVE_TRAFFIC_RX_TEXT || cls == LIVE_TRAFFIC_TX_TEXT;
        case LIVE_FILTER_DM:    return cls == LIVE_TRAFFIC_TX_DM;
        case LIVE_FILTER_POS:   return cls == LIVE_TRAFFIC_RX_POS  || cls == LIVE_TRAFFIC_TX_POS;
        case LIVE_FILTER_TLM:   return cls == LIVE_TRAFFIC_RX_TLM  || cls == LIVE_TRAFFIC_TX_TLM;
        case LIVE_FILTER_NODE:  return cls == LIVE_TRAFFIC_RX_NODE || cls == LIVE_TRAFFIC_TX_NODE;
        case LIVE_FILTER_ACK:   return cls == LIVE_TRAFFIC_RX_ACK  || cls == LIVE_TRAFFIC_TX_ACK;
        case LIVE_FILTER_ENC:   return cls == LIVE_TRAFFIC_RX_ENC;
        case LIVE_FILTER_ERROR: return cls == LIVE_TRAFFIC_ERROR;
        // Everything the named rows do not claim, so no line is unreachable.
        case LIVE_FILTER_OTHER: return cls == LIVE_TRAFFIC_RX_OTHER || cls == LIVE_TRAFFIC_TX_OTHER
                                    || cls == LIVE_TRAFFIC_DEFAULT;
        default:                return true;
    }
}

// Chat colors in a dark theme must not themselves be dark: a navy or brown
// name/message on a near-black background is unreadable. Below the luminance
// floor the color is blended toward white just far enough to reach it, keeping
// its hue. 150 also sits above bubbleTextColor()'s 145 cut-over, so a lifted
// bubble gets dark body text rather than white on a mid-tone. Light themes are
// left alone.
static uint16_t chatColorForTheme(uint16_t c565) {
    if (s_cfg.uiMode == UI_MODE_LIGHT) return c565;
    constexpr uint32_t kDarkThemeLumFloor = 150;
    uint32_t r = ((c565 >> 11) & 0x1F) << 3;
    uint32_t g = ((c565 >>  5) & 0x3F) << 2;
    uint32_t b = (c565 & 0x1F) << 3;
    const uint32_t lum = (299u * r + 587u * g + 114u * b) / 1000u;
    if (lum >= kDarkThemeLumFloor) return c565;
    const uint32_t num = kDarkThemeLumFloor - lum;
    const uint32_t den = 255u - lum;
    r += ((255u - r) * num + den - 1) / den;
    g += ((255u - g) * num + den - 1) / den;
    b += ((255u - b) * num + den - 1) / den;
    return rgb565((uint8_t)min<uint32_t>(r, 255), (uint8_t)min<uint32_t>(g, 255),
                  (uint8_t)min<uint32_t>(b, 255));
}

static inline uint16_t userMessageAccentColor565() {
    // User-chosen override from the config color picker takes precedence.
    if (s_cfg.userMsgColor < kUserMsgColorCount) {
        return chatColorForTheme(kUserMsgColors[s_cfg.userMsgColor].color);
    }
    // Default: keep classic yellow in dark mode, but use darker amber in light
    // mode for readable contrast against bright backgrounds.
    return (s_cfg.uiMode == UI_MODE_LIGHT) ? rgb565(0x7A, 0x4C, 0x00) : TFT_YELLOW;
}

static uint16_t liveLineTrafficColor(const DisplayLine &dl) {
#if defined(DEVICE_TDECK_PRO)
    LV_UNUSED(dl);
    return TFT_BLACK;
#else
    LiveTrafficClass cls = classifyLiveTraffic(dl);
    switch (cls) {
        case LIVE_TRAFFIC_ERROR:   return TFT_RED;
        case LIVE_TRAFFIC_TX_ACK:  return TFT_GREEN;
        case LIVE_TRAFFIC_RX_ACK:  return (uint16_t)0x57EA;
        case LIVE_TRAFFIC_TX_TEXT: return userMessageAccentColor565();
        case LIVE_TRAFFIC_TX_NODE: return (uint16_t)0xF39C;
        case LIVE_TRAFFIC_TX_POS:  return (uint16_t)0xFD20;
        case LIVE_TRAFFIC_TX_TLM:  return (uint16_t)0x07FF;
        case LIVE_TRAFFIC_TX_DM:   return TFT_MAGENTA;
        case LIVE_TRAFFIC_RX_TEXT: return TFT_CYAN;
        case LIVE_TRAFFIC_RX_NODE: return (uint16_t)0x7DFF;
        case LIVE_TRAFFIC_RX_POS:  return (uint16_t)0xFDF0;
        case LIVE_TRAFFIC_RX_TLM:  return (uint16_t)0xA7EA;
        case LIVE_TRAFFIC_RX_ENC:  return TFT_ORANGE;
        case LIVE_TRAFFIC_RX_OTHER:return (uint16_t)0x9DFF;
        case LIVE_TRAFFIC_TX_OTHER:return userMessageAccentColor565();
        case LIVE_TRAFFIC_DEFAULT:
        default:
            break;
    }

    return (dl.color == TFT_DARKGREY) ? TFT_WHITE : dl.color;
#endif
}

static lv_color_t liveLineBgColor(const DisplayLine &dl) {
#if defined(DEVICE_TDECK_PRO)
    LV_UNUSED(dl);
    return lv_color_make(255, 255, 255);
#else
    LiveTrafficClass cls = classifyLiveTraffic(dl);
    switch (cls) {
        case LIVE_TRAFFIC_ERROR:    return lv_color_hex(0x4A1D1D);
        case LIVE_TRAFFIC_TX_ACK:   return lv_color_hex(0x1E3E27);
        case LIVE_TRAFFIC_RX_ACK:   return lv_color_hex(0x1B3E34);
        case LIVE_TRAFFIC_TX_TEXT:  return lv_color_hex(0x4A4318);
        case LIVE_TRAFFIC_TX_NODE:  return lv_color_hex(0x4A2D1F);
        case LIVE_TRAFFIC_TX_POS:   return lv_color_hex(0x4A3418);
        case LIVE_TRAFFIC_TX_TLM:   return lv_color_hex(0x1A3B40);
        case LIVE_TRAFFIC_TX_DM:    return lv_color_hex(0x33224A);
        case LIVE_TRAFFIC_RX_TEXT:  return lv_color_hex(0x12345D);
        case LIVE_TRAFFIC_RX_NODE:  return lv_color_hex(0x1D2E58);
        case LIVE_TRAFFIC_RX_POS:   return lv_color_hex(0x1A3754);
        case LIVE_TRAFFIC_RX_TLM:   return lv_color_hex(0x1A3A3E);
        case LIVE_TRAFFIC_RX_ENC:   return lv_color_hex(0x4A3618);
        case LIVE_TRAFFIC_RX_OTHER: return lv_color_hex(0x102D52);
        case LIVE_TRAFFIC_TX_OTHER: return lv_color_hex(0x3E3619);
        case LIVE_TRAFFIC_DEFAULT:
        default:
            return lv_color_hex(0x10254A);
    }
#endif
}

static lv_color_t liveListBackdropColor() {
#if defined(DEVICE_TDECK_PRO)
    return lv_color_make(255, 255, 255);
#else
    if (s_cfg.uiMode == UI_MODE_LIGHT) {
        // Keep live-log canvas dark in light mode for text readability.
        return lv_color_make(0x0F, 0x2A, 0x5C);
    }
    return lv_color_hex(0x0F2A5C);
#endif
}

static lv_opa_t liveListBackdropOpa() {
#if defined(DEVICE_TDECK_PRO)
    return LV_OPA_COVER;
#else
    return (s_cfg.uiMode == UI_MODE_LIGHT) ? LV_OPA_COVER : LV_OPA_50;
#endif
}

static lv_color_t tftColorToLv(uint16_t c) {
    uint8_t r = (uint8_t)((((c >> 11) & 0x1F) * 255) / 31);
    uint8_t g = (uint8_t)((((c >> 5) & 0x3F) * 255) / 63);
    uint8_t b = (uint8_t)(((c & 0x1F) * 255) / 31);
    return lv_color_make(r, g, b);
}

static void closeLiveToolsModal() {
    // One object now. Tools used to be a panel inside a dimming backdrop, which
    // meant deleting the backdrop and letting it take the panel with it; it is a
    // full screen carrying its own nav bar since issue #94, so the panel is the
    // root of the tree and the bar is a child of it.
    if (lvObjValid(s_liveToolsModal)) {
        lv_obj_del(s_liveToolsModal);          // takes the rows and the bar with it
    }
    // The bar took its status cluster with it. Every screen that carries one owes
    // this; see onNavStatusClusterDeleted() for what happens when it is missed.
    clearNavBarStatusIcons();
    s_liveToolsModal = nullptr;
    memset(s_liveToolsRows, 0, sizeof(s_liveToolsRows));
    s_liveToolsSelection = LIVE_TOOL_LIVE;     // first enabled tool
}

static void closeLiveFilterModal() {
    if (lvObjValid(s_liveFilterBackdrop)) {
        lv_obj_del(s_liveFilterBackdrop);      // takes the modal and rows with it
    } else if (lvObjValid(s_liveFilterModal)) {
        lv_obj_del(s_liveFilterModal);
    }
    s_liveFilterBackdrop = nullptr;
    s_liveFilterModal = nullptr;
    s_liveFilterNoMqttRow = nullptr;
    memset(s_liveFilterRows, 0, sizeof(s_liveFilterRows));
}

static void closeLiveModal() {
    // This screen may have been carrying a copy of the nav bar, and its status
    // icons go with it.
    clearNavBarStatusIcons();
    // Tools and the filter picker are parented to the root screen, not to the
    // Live modal, so nothing else would take them down with the screen they
    // belong to.
    closeLiveToolsModal();
    closeLiveFilterModal();
    closeDmDeleteConfirm();
    s_liveClearFlashUntilMs = 0;
    if (s_liveModal && lv_obj_is_valid(s_liveModal)) {
        lv_obj_del(s_liveModal);
    }
    s_liveModal = nullptr;
    s_liveList = nullptr;
    s_liveFilterHeaderLabel = nullptr;
    s_lastRenderedLiveCount = -1;
    s_lastRenderedLiveScrollOff = -1;
    // The filter is a way of looking at the feed, not a setting: leaving Live
    // puts it back to All so it can never be silently in force next time.
    s_liveFilter = LIVE_FILTER_ALL;
    s_liveNoMqtt = false;
}

static void chartPushSample(ChartHist &h, float value) {
    if (h.count < ChartHist::CAP) {
        h.v[(h.head + h.count) % ChartHist::CAP] = value;
        h.count++;
    } else {
        h.v[h.head] = value;
        h.head = (h.head + 1) % ChartHist::CAP;
    }
    h.lastVal = value;
    h.hasLast = true;
    h.seq++;
}

static float chartSampleAt(const ChartHist &h, int index) {
    // index 0 = oldest, index count-1 = newest.
    if (index < 0 || index >= h.count) return 0.0f;
    return h.v[(h.head + index) % ChartHist::CAP];
}

// ── Web-config snapshot getters (see web_config.h) ─────────────
static void chartFillSnapshot(const ChartHist &h, WebChartSnapshot &out) {
    static_assert(ChartHist::CAP == WebChartSnapshot::CAP,
                  "ChartHist::CAP must match WebChartSnapshot::CAP");
    out.count = h.count;
    out.hasLast = h.hasLast;
    out.lastVal = h.lastVal;
    for (int i = 0; i < h.count && i < WebChartSnapshot::CAP; i++) {
        out.values[i] = chartSampleAt(h, i);
    }
    for (int i = h.count; i < WebChartSnapshot::CAP; i++) {
        out.values[i] = 0.0f;
    }
}
void webChartSnapshotChUtil(WebChartSnapshot &out)  { chartFillSnapshot(s_chUtilHist,  out); }
void webChartSnapshotAirUtil(WebChartSnapshot &out) { chartFillSnapshot(s_airUtilHist, out); }
void webChartSnapshotSnr(WebChartSnapshot &out)     { chartFillSnapshot(s_snrHist,     out); }
void webChartSnapshotRssi(WebChartSnapshot &out)    { chartFillSnapshot(s_rssiHist,    out); }

static void closeChUtilChartModal() {
    if (s_chUtilChartModal && lv_obj_is_valid(s_chUtilChartModal)) {
        lv_obj_del(s_chUtilChartModal);
    }
    s_chUtilChartModal = nullptr;
    s_chUtilChart = nullptr;
    s_chUtilSeries = nullptr;
    s_airUtilSeries = nullptr;
    s_chUtilStatsLabel = nullptr;
    s_chUtilRenderedSeq = 0;
    s_airUtilRenderedSeq = 0;
}

static void closeSnrRssiChartModal() {
    if (s_snrChartModal && lv_obj_is_valid(s_snrChartModal)) {
        lv_obj_del(s_snrChartModal);
    }
    s_snrChartModal = nullptr;
    s_snrChart = nullptr;
    s_snrSeries = nullptr;
    s_rssiSeries = nullptr;
    s_snrStatsLabel = nullptr;
    s_snrRenderedSeq = 0;
    s_rssiRenderedSeq = 0;
}

static void closeDmModal() {
    // This screen may have been carrying a copy of the nav bar, and its status
    // icons go with it.
    clearNavBarStatusIcons();
    closeDmNodePicker();
    closeDmDeleteConfirm();
    freshDotsForget(s_dmDots, 0);
    lvObjDeleteSafe(s_dmModal);
    s_dmConvPanel = nullptr;
    s_dmConvList = nullptr;
    s_dmMsgPanel = nullptr;
    s_dmMsgList = nullptr;
    s_dmConvTitle = nullptr;
    s_dmMsgTitle = nullptr;
    s_dmHintLabel = nullptr;
    s_dmDrawerScrim = nullptr;
    s_dmHeaderTitle = nullptr;
    s_dmDrawerOpen = false;
    s_dmDrawerAnimReady = false;
    s_dmConvCount = 0;
    s_dmSelection = -1;
    s_dmRenderedConvCount = -1;
    s_dmRenderedNodeId = 0;
    s_dmRenderedMsgCount = -1;
    s_dmRenderedUnreadTotal = -1;
    s_dmMsgPanelFocused = false;
    s_dmDeleteFlashMsg[0] = '\0';
    s_dmDeleteFlashUntilMs = 0;
    s_dmTouchPressStartMs = 0;
    s_dmTouchPressRowIdx = -1;
    s_dmTouchLongPressTriggered = false;
#if UI_TOUCH_ONLY_PROFILE
    // The rows that would have reported the release are gone with the modal.
    // Left set, these would suppress every future refresh of a reopened screen.
    s_dmRowPressActive = false;
    s_dmRefreshPending = false;
#endif
    memset(s_dmConvRows, 0, sizeof(s_dmConvRows));
    memset(s_dmConvNodeIds, 0, sizeof(s_dmConvNodeIds));
}

static void closeNodesModal() {
    // This screen may have been carrying a copy of the nav bar, and its status
    // icons go with it.
    clearNavBarStatusIcons();
    closeNodesFilterDialog();
    closeNodesActionMenu();
#if NODES_INFO_AS_MODAL
    // On the root screen too, and holding the section pointers cleared below.
    closeNodesInfoDlg();
#endif
#if HAS_NODE_LOCATE
    // Parented to the root screen, so closing the screen it was opened from
    // would otherwise strand it with nothing behind it.
    closeNodeLocateModal();
#endif
#if HAS_NODE_LOS
    closeNodeLosModal();
#endif
    lvObjDeleteSafe(s_nodesModal);
    nodesPanelWifiRestore();
    s_nodesMapSelectionNodeId = 0;
    s_nodesMapSelectionSinceMs = 0;
    s_nodesMapLastPrimePollMs = 0;
    s_nodesInfoPanel = nullptr;
    s_nodesDetail = nullptr;
    s_nodesListPanel = nullptr;
    s_nodesInfoFocused = false;
    memset(s_nodesSectionBox, 0, sizeof(s_nodesSectionBox));
    memset(s_nodesSectionKeys, 0, sizeof(s_nodesSectionKeys));
    memset(s_nodesSectionVals, 0, sizeof(s_nodesSectionVals));
    s_nodesMapPanel = nullptr;
    s_nodesMapTitle = nullptr;
    s_nodesMapCoords = nullptr;
    s_nodesMapMarker = nullptr;
    s_nodesMapTileLayer = nullptr;
    s_nodesMapImage = nullptr;
    s_nodesTitleLabel = nullptr;
    s_nodesHintLabel = nullptr;
    s_nodesFilterBtn = nullptr;
    s_nodesFilterBtnLabel = nullptr;
    s_nodesAllBtn = nullptr;
    s_nodesMapBtn = nullptr;
    s_nodesSearchBtnFocused = false;
    s_nodesTopBtn = kNodesTopSearch;
    s_nodesFilterDialog = nullptr;
    s_nodesFilterInput = nullptr;
    s_nodesFilterKeyboard = nullptr;
    s_nodesMapImageSrc[0] = '\0';
    s_nodesMapLastRenderUsedDetail = false;
    s_nodesMapLastDetailKey[0] = '\0';
    s_nodesMapLastDetailLod = -1;
    s_nodesMapLastBoundsValid = false;
    s_nodesMapLastLatMin = 0.0f;
    s_nodesMapLastLatMax = 0.0f;
    s_nodesMapLastLonMin = 0.0f;
    s_nodesMapLastLonMax = 0.0f;
    s_nodesList = nullptr;
    s_nodesListRowCount = 0;
    s_nodesSnapshotCount = 0;
    // The index is the archived half's snapshot, so it is dropped with the rest
    // of the frozen list rather than left to be re-shown on the next open.
    s_nodesArchivedCount = 0;
    nodeArchiveIndexClear();
    nodesArchivedDetailInvalidate();
    s_nodesFilteredCount = 0;
    s_nodesSelected = -1;
    s_nodesFilterOpen = false;
    s_nodesFilterLen = 0;
    s_nodesFilter[0] = '\0';
    s_nodesRenderStart = 0;
    memset(s_nodesFilteredIdx, 0, sizeof(s_nodesFilteredIdx));
    memset(s_nodesRenderedFilteredIdx, 0, sizeof(s_nodesRenderedFilteredIdx));
    s_nodesActionSelection = 0;
    s_nodesActionNodeId = 0;
    s_nodesActionPacketId = 0;
    s_nodesActionMsgMode = false;
    memset(s_nodesActionRows, 0, sizeof(s_nodesActionRows));
    memset(s_nodesListRows, 0, sizeof(s_nodesListRows));
}

