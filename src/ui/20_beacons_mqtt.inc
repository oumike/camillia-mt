// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: neighbours, beacons, MQTT monitor, discovery.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

// True when this node's packets have told us it is one hop away. Anything that
// never carried hop_start is "unknown", not "direct" — see NodeEntry::hasHops.
// Outside the Discovery guard below: the NeighborInfo announce decides what to
// advertise with it, and that runs on every board.
static inline bool nodeIsDirectNeighbor(const NodeEntry &e) {
    return e.hasHops && e.hops == 0 && e.lastHeardMs != 0;
}

// ── Beacons modal ────────────────────────────────────────────────────────────
// One card per sender, holding everything that beacon carried. Available on
// every board, Cardputer included: unlike Discovery there is no neighbor table
// behind it, just the handful of records the RX path already fills in.

static void closeBeaconsModal() {
    lvObjDeleteSafe(s_beaconsModal);
    s_beaconsList = nullptr;
    s_beaconsStatusLabel = nullptr;
    s_beaconsTxLabel = nullptr;
    memset(s_beaconAgeLabels, 0, sizeof(s_beaconAgeLabels));
    s_beaconsRenderedSig = 0;
}

// Our own beacon, from meshBeaconStatus(): off, when the next one goes, or what
// is holding it.
static void beaconsFormatTxStatus(char *out, size_t outLen) {
    const MeshBeaconStatus st = meshBeaconStatus();
    const uint8_t target = s_cfg.meshBeaconTargetPreset < PRESET_COUNT
                               ? s_cfg.meshBeaconTargetPreset : PRESET_LONG_FAST;
    const char *targetName = kPresets[target].channelName;
    switch (st.state) {
        case MeshBeaconStatus::WAITING: {
            char last[24] = {};
            if (st.lastEpoch) formatChatClock(st.lastEpoch, last, sizeof(last));
            if (!last[0]) snprintf(last, sizeof(last), "%s", TR("never"));
            const unsigned mins = (unsigned)((st.nextInMs + 59999UL) / 60000UL);
            snprintf(out, outLen, TR("Your beacon: %s \xC2\xB7 next in %u min \xC2\xB7 last %s"),
                     targetName, mins, last);
        } break;
        case MeshBeaconStatus::HELD_SCAN:
            snprintf(out, outLen, "%s", TR("Your beacon: held - Discovery scan"));
            break;
        case MeshBeaconStatus::HELD_AIRTIME:
            snprintf(out, outLen, "%s", TR("Your beacon: held - airtime"));
            break;
        case MeshBeaconStatus::HELD_PRESET:
            snprintf(out, outLen, TR("Your beacon: %s not available in %s"),
                     targetName, s_cfg.region);
            break;
        case MeshBeaconStatus::OFF:
        default:
            snprintf(out, outLen, "%s", lic::active() ? TR("Your beacon: off while licensed")
                                                      : TR("Your beacon: off"));
            break;
    }
}

static void beaconsRefreshTxLabel() {
    if (!s_beaconsTxLabel) return;
    char line[112];
    beaconsFormatTxStatus(line, sizeof(line));
    const char *shown = lv_label_get_text(s_beaconsTxLabel);
    if (shown && strcmp(shown, line) == 0) return;
    lv_label_set_text(s_beaconsTxLabel, line);
}

// Compact elapsed time. Coarse on purpose: these gaps are minutes to hours, and
// a second-by-second readout on an hours-old beacon is noise, not information.
static void beaconsFormatSpan(uint32_t ms, char *out, size_t outLen) {
    const uint32_t s = ms / 1000UL;
    if (s < 60UL) {
        snprintf(out, outLen, "%lus", (unsigned long)s);
    } else if (s < 3600UL) {
        snprintf(out, outLen, "%lum", (unsigned long)(s / 60UL));
    } else if (s < 86400UL) {
        snprintf(out, outLen, "%luh%lum",
                 (unsigned long)(s / 3600UL), (unsigned long)((s % 3600UL) / 60UL));
    } else {
        snprintf(out, outLen, "%lud", (unsigned long)(s / 86400UL));
    }
}

// "Heard 4m ago" plus, once a sender has repeated, how far apart the repeats
// have been — the number that says when it is worth looking again.
static void beaconsFormatHeard(const BeaconOffer &o, char *out, size_t outLen) {
    const uint32_t now = millis();
    char age[16];
    beaconsFormatSpan((uint32_t)(now - o.rxMs), age, sizeof(age));
    if (o.hits > 1) {
        char gap[16];
        beaconsFormatSpan((uint32_t)(o.rxMs - o.firstMs) / (uint32_t)(o.hits - 1),
                          gap, sizeof(gap));
        snprintf(out, outLen, TR("Heard %s ago   %u seen, ~%s apart"),
                 age, (unsigned)o.hits, gap);
    } else {
        snprintf(out, outLen, TR("Heard %s ago   1 seen"), age);
    }
}

// The sender's own name when our node table happens to know it. Usually it does
// not — a beaconing node lives on another mesh and its NodeInfo never reaches
// us — so the hex id is the normal case here rather than the fallback.
static void beaconsSenderLabel(uint32_t id, char *out, size_t outLen) {
    const NodeEntry *e = Nodes.find(id);
    if (e && e->hasName && e->longName[0]) {
        utf8util::copyTruncate(out, outLen, e->longName);
        return;
    }
    liveNodeLabel(id, out, outLen, true);
}

static lv_obj_t *beaconsMakeLabel(lv_obj_t *parent, const lv_font_t *font,
                                  uint32_t color, const char *text) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_width(lbl, lv_pct(100));
    lv_obj_set_style_text_font(lbl, font, 0);
#if defined(DEVICE_TDECK_PRO)
    LV_UNUSED(color);
    lv_obj_set_style_text_color(lbl, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
#endif
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_label_set_text(lbl, text);
    return lbl;
}

// Rebuilds every card. Called only when the set actually changed — the ages
// inside them are refreshed in place, see refreshBeaconsModal().
static void beaconsBuildList() {
    if (!s_beaconsList) return;
    lv_obj_clean(s_beaconsList);
    memset(s_beaconAgeLabels, 0, sizeof(s_beaconAgeLabels));

    if (s_beaconOfferCount == 0) {
        // Two different silences, and telling them apart is the whole value of
        // this text: nothing to hear, versus not listening.
        beaconsMakeLabel(s_beaconsList, kBeaconsBodyFont, kBeaconsBodyColor,
                         s_cfg.meshBeaconListen
                             ? TR("No beacons heard yet.\n\n"
                               "A beacon is only audible while its sender has retuned onto "
                               "this channel, preset and region, and it repeats on that "
                               "node's own schedule - minutes to hours apart. This list "
                               "keeps what it hears until you clear it. Sending your own "
                               "beacon is a separate setting, under Mesh Beacons in Config.")
                             : TR("Listening for beacons is off.\n\n"
                               "Turn on Listen under Mesh Beacons in Config (or under "
                               "Modules in web config) to decode beacons from other "
                               "meshes. An offer is never applied to your radio. Sending "
                               "your own beacon is a separate setting."));
        return;
    }

    char name[40];
    char line[112];

    for (int i = 0; i < s_beaconOfferCount; i++) {
        const BeaconOffer &o = s_beaconOffers[i];

        lv_obj_t *card = lv_obj_create(s_beaconsList);
        lv_obj_set_width(card, lv_pct(100));
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(card, lv_color_make(0, 0, 0), 0);
    #else
        lv_obj_set_style_bg_color(card, lv_color_hex(0x16386F), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_50, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(0x335D9D), 0);
    #endif
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 4, 0);
        lv_obj_set_style_pad_all(card, 4, 0);
        lv_obj_set_style_pad_row(card, 1, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);

        beaconsSenderLabel(o.sender, name, sizeof(name));
        beaconsMakeLabel(card, kBeaconsTitleFont, kBeaconsTitleColor, name);

        // Wrapped, not clipped: the message is the one field the sender wrote by
        // hand, and half of it is worth less than the two lines it costs.
        if (o.message[0]) {
            snprintf(line, sizeof(line), "\"%s\"", o.message);
            beaconsMakeLabel(card, kBeaconsBodyFont, kBeaconsBodyColor, line);
        }

        // What is on offer: channel, preset, region — whichever the beacon
        // carried. A trailing * means the channel came with a key, and "AEAD"
        // that it uses Meshtastic 2.8.1's authenticated encryption -- a channel
        // set up here with the same name and key but AEAD off would not hear it.
        // Shown even when empty, because "this beacon offered nothing" is itself
        // an answer.
        char offer[72] = {};
        int n = 0;
        if (o.hasChannel) {
            n += snprintf(offer + n, sizeof(offer) - n, "%s%s%s",
                          o.channelName[0] ? o.channelName : "(unnamed)",
                          o.channelHasPsk ? "*" : "",
                          o.channelUsesAead ? " AEAD" : "");
        }
        if (o.preset >= 0 && o.preset < PRESET_COUNT) {
            n += snprintf(offer + n, sizeof(offer) - n, "%s%s",
                          n ? " / " : "", kPresets[o.preset].channelName);
        }
        if (o.region[0]) {
            n += snprintf(offer + n, sizeof(offer) - n, "%s%s", n ? " / " : "", o.region);
        }
        if (o.hasSlot && n < (int)sizeof(offer)) {
            char slot[24];
            snprintf(slot, sizeof(slot), TR("slot %u"), (unsigned)o.slot);
            n += snprintf(offer + n, sizeof(offer) - n, "%s%s", n ? " / " : "", slot);
        }
        snprintf(line, sizeof(line), TR("Offers: %s"), offer[0] ? offer : TR("(nothing)"));
        beaconsMakeLabel(card, kBeaconsBodyFont, kBeaconsBodyColor, line);

        // How it reached us. The id is here rather than in the title so a known
        // name does not push it off the card entirely.
        char hops[16];
        if (o.hasHops) snprintf(hops, sizeof(hops), "%u hop%s",
                                (unsigned)o.hops, (o.hops == 1) ? "" : "s");
        else           snprintf(hops, sizeof(hops), TR("? hops"));
        snprintf(line, sizeof(line), "!%08lx   %.1f dB   %.0f dBm   %s",
                 (unsigned long)o.sender, o.snr, o.rssi, hops);
        beaconsMakeLabel(card, kBeaconsBodyFont, kBeaconsDimColor, line);

        beaconsFormatHeard(o, line, sizeof(line));
        s_beaconAgeLabels[i] = beaconsMakeLabel(card, kBeaconsBodyFont,
                                                kBeaconsDimColor, line);
    }
}

static void refreshBeaconsModal(bool force) {
    if (!s_beaconsModal || !s_beaconsList) return;
    if (!lvObjValid(s_beaconsModal)) {
        s_beaconsModal = nullptr;
        s_beaconsList = nullptr;
        s_beaconsStatusLabel = nullptr;
        s_beaconsTxLabel = nullptr;
        memset(s_beaconAgeLabels, 0, sizeof(s_beaconAgeLabels));
        return;
    }

    // One counter covers every change worth a rebuild: a new sender, a repeat
    // from a known one, and a clear all bump it.
    const uint32_t sig = s_beaconOfferSeq * 31u
                       + (uint32_t)s_beaconOfferCount
                       + (s_cfg.meshBeaconListen ? 0x8000u : 0u);
    if (force || sig != s_beaconsRenderedSig) {
        s_beaconsRenderedSig = sig;
        if (s_beaconsStatusLabel) {
            char status[64];
            if (!s_cfg.meshBeaconListen) {
                snprintf(status, sizeof(status), TR("Listening: off"));
            } else {
                snprintf(status, sizeof(status), TR("Listening   %d sender%s heard"),
                         s_beaconOfferCount, (s_beaconOfferCount == 1) ? "" : "s");
            }
            lv_label_set_text(s_beaconsStatusLabel, status);
        }
        beaconsRefreshTxLabel();
        beaconsBuildList();
        return;
    }

    // Nothing new arrived, so only the clocks moved. Rewriting just those labels
    // leaves the cards — and the scroll position — alone.
    //
    // Once a second, and only when the text actually changed. lv_label_set_text
    // invalidates unconditionally, so doing this on every UI tick would put the
    // whole list into a full redraw ~30 times a second to move a number that
    // changes at most once a second, and usually once a minute.
    static uint32_t s_lastAgeTickMs = 0;
    const uint32_t now = millis();
    if ((uint32_t)(now - s_lastAgeTickMs) < 1000UL) return;
    s_lastAgeTickMs = now;

    beaconsRefreshTxLabel();
    char line[112];
    for (int i = 0; i < s_beaconOfferCount && i < kBeaconOfferMax; i++) {
        if (!s_beaconAgeLabels[i]) continue;
        beaconsFormatHeard(s_beaconOffers[i], line, sizeof(line));
        const char *shown = lv_label_get_text(s_beaconAgeLabels[i]);
        if (shown && strcmp(shown, line) == 0) continue;
        lv_label_set_text(s_beaconAgeLabels[i], line);
    }
}

// Drops the collected beacons. Nothing else holds them, so unlike Discovery's
// clear there is no watermark to keep and nothing to rebuild from: the list
// refills only as senders beacon again.
static void beaconsClear() {
    s_beaconOfferCount = 0;
    s_beaconOfferSeq++;
    refreshBeaconsModal(true);
}

#if HAS_WEATHER
// ── Weather ──────────────────────────────────────────────────────────────────
// Current conditions for wherever this node is, through an operator-run proxy.
// See docs/WEATHER.md for the contract and why a proxy is needed at all.
//
// Fetched on open and then cached, rather than polled: Wi-Fi is not always up,
// and a weather screen has no business being a reason to bring the radio up or
// keep it there.
static lv_obj_t   *s_weatherModal  = nullptr;
static lv_obj_t   *s_weatherStatus = nullptr;
static lv_obj_t   *s_weatherBody   = nullptr;
static lv_obj_t   *s_weatherTitle  = nullptr;   // "Weather" / "Weather - City, ST"
static lv_obj_t   *s_weatherTemp   = nullptr;   // the big number
static lv_obj_t   *s_weatherDesc   = nullptr;   // "Clear", under it
static lv_timer_t *s_weatherTimer  = nullptr;

// Theme-aware ink. The modal follows the light/dark UI mode the rest of the
// settings modals do, and the hero block takes the *theme's* accent rather than
// a colour of its own, so it stays in key with whatever palette is chosen.
//
// The T-Deck Pro is exempt from all of it: that panel is 1-bit, so LVGL
// thresholds every colour to pure black or white and an accent would land on
// whichever side of the threshold it happened to fall. Black on white is the
// only thing that reads there.
struct WeatherInk {
    lv_color_t bg, border, title, hero, body, muted;
};

static WeatherInk weatherInk() {
    WeatherInk k;
#if defined(DEVICE_TDECK_PRO)
    k.bg     = lv_color_make(255, 255, 255);
    k.border = lv_color_make(0, 0, 0);
    k.title  = lv_color_make(0, 0, 0);
    k.hero   = lv_color_make(0, 0, 0);
    k.body   = lv_color_make(0, 0, 0);
    k.muted  = lv_color_make(0, 0, 0);
#else
    const bool light = (s_cfg.uiMode == UI_MODE_LIGHT);
    k.bg     = light ? lv_color_hex(0xEAF1FB) : lv_color_hex(0x0E285B);
    k.border = light ? lv_color_hex(0x6E8FB8) : lv_color_hex(0x5C86C6);
    k.title  = light ? lv_color_hex(0x16233A) : lv_color_hex(0xD9E8FF);
    // The accent is the same value in both modes — it is chosen to sit on a
    // dark panel, so painting small text in it on a light one can come out
    // washed. Dark mode takes it as-is; light mode carries it a long way toward
    // the body ink, which keeps the hue and buys back the contrast. This is why
    // the palette uses `accent` as a background elsewhere and puts textMain on
    // top, rather than drawing type in it.
    k.hero   = light ? lvColorFrom565(blend565(s_ui.accent, s_ui.textMain, 150))
                     : lvColorFrom565(s_ui.selectAccent);
    k.body   = light ? lv_color_hex(0x13243D) : lv_color_hex(0xE8F1FF);
    k.muted  = light ? lv_color_hex(0x4A5B75) : lv_color_hex(0xA7C7FF);
#endif
    return k;
}

// Type sizes for the screen. The Cardputer's panel is 240x135 — less than half
// the height every other board brings — and the roomy layout simply does not
// fit on it: title, a 32 px hero, four detail lines, a status line and a legend
// come to about 170 px. It gets its own set, and a three-line body below, which
// together land inside 135 with the legend still on screen.
#if UI_CARDPUTER_COMPACT
static const lv_font_t *const kWeatherHeroFont = &lv_font_montserrat_18;
static const lv_font_t *const kWeatherDescFont = &lv_font_montserrat_12;
static const lv_font_t *const kWeatherBodyFont = &lv_font_montserrat_10;
static constexpr bool kWeatherCompactBody = true;
// Temperature and sky share one line here. Two rows of hero plus a title, three
// detail lines, a status line and a legend still overran 135 px and clipped the
// bottom; folding the two into one row is worth more than the size difference
// between them, which was never doing much at 18 px against 12 px anyway.
static constexpr bool kWeatherHeroOneLine = true;
static constexpr int  kWeatherRowPad      = 1;
#else
static const lv_font_t *const kWeatherHeroFont = &lv_font_montserrat_32;
static const lv_font_t *const kWeatherDescFont = &lv_font_montserrat_18;
static const lv_font_t *const kWeatherBodyFont = &lv_font_montserrat_14;
static constexpr bool kWeatherCompactBody = false;
static constexpr bool kWeatherHeroOneLine = false;
static constexpr int  kWeatherRowPad      = 4;
#endif

static const char *weatherCompass(int deg) {
    static const char *const d[] = {"N","NE","E","SE","S","SW","W","NW"};
    if (deg < 0) deg = 0;
    return d[(int)((deg + 22) / 45) % 8];
}

static void weatherRenderReading() {
    if (!lvObjValid(s_weatherBody) || !lvObjValid(s_weatherStatus)) return;
    WeatherReading r;
    if (!weatherLatest(r)) return;

    if (lvObjValid(s_weatherTemp)) {
        char big[48];
        if (kWeatherHeroOneLine) snprintf(big, sizeof(big), "%d%s  %s",
                                          r.temp, r.tempUnit, r.desc);
        else                     snprintf(big, sizeof(big), "%d%s", r.temp, r.tempUnit);
        lv_label_set_text(s_weatherTemp, big);
    }
    if (lvObjValid(s_weatherDesc) && !kWeatherHeroOneLine) {
        lv_label_set_text(s_weatherDesc, r.desc);
    }
    if (lvObjValid(s_weatherTitle)) {
        // A hyphen, not an em dash: these faces draw ASCII and little else, and
        // a dash outside that range would come back as a missing-glyph box —
        // the same trap the lock screen's message rows fell into.
        char t[64];
        if (r.place[0]) snprintf(t, sizeof(t), TR("Weather - %s"), r.place);
        else            snprintf(t, sizeof(t), TR("Weather"));
        lv_label_set_text(s_weatherTitle, t);
    }

    // Everything the hero block does not say, one fact per line. "Feels like"
    // lives here rather than beside the big number: it is the same reading
    // qualified, and putting two temperatures in the headline makes neither of
    // them the answer to "how warm is it".
    char body[192];
    if (kWeatherCompactBody) {
        // Three lines instead of four, pairing the two smallest facts. Dropping
        // one outright would have been the other way to find the pixels, and
        // this keeps all of them.
        snprintf(body, sizeof(body),
                 TR("Feels %d%s   Hum %d%%\n"
                 "Wind %d %s %s\n"
                 "Gusting %d %s"),
                 r.feels, r.tempUnit, r.humidityPct,
                 r.wind, r.windUnit, weatherCompass(r.dirDeg),
                 r.gust, r.windUnit);
    } else {
        snprintf(body, sizeof(body),
                 TR("Feels like   %d%s\n"
                 "Humidity     %d%%\n"
                 "Wind         %d %s from %s\n"
                 "Gusting      %d %s"),
                 r.feels, r.tempUnit,
                 r.humidityPct,
                 r.wind, r.windUnit, weatherCompass(r.dirDeg),
                 r.gust, r.windUnit);
    }
    lv_label_set_text(s_weatherBody, body);

    // The age matters here in a way it does not on a live screen: a reading
    // shown after a failed refresh is still the last true thing we know, and
    // the operator has to be able to tell one from the other.
    const uint32_t ageS = weatherAgeMs() / 1000UL;
    char status[96];
    const bool stale = (weatherState() != WEATHER_OK);
    if (ageS < 60) {
        snprintf(status, sizeof(status), TR("%s%.2f, %.2f (rounded)"),
                 stale ? TR("Stale - ") : "", r.lat, r.lon);
    } else {
        snprintf(status, sizeof(status), TR("%s%lu min ago - %.2f, %.2f (rounded)"),
                 stale ? TR("Stale - ") : "", (unsigned long)(ageS / 60UL), r.lat, r.lon);
    }
    lv_label_set_text(s_weatherStatus, status);
}

static void weatherPoll(lv_timer_t *t) {
    LV_UNUSED(t);
    if (!lvObjValid(s_weatherModal)) return;
    if (weatherState() == WEATHER_FETCHING) return;

    if (s_weatherTimer) { lv_timer_del(s_weatherTimer); s_weatherTimer = nullptr; }

    const WeatherState st = weatherState();
    if (st == WEATHER_OK) { weatherRenderReading(); return; }

    // A stale reading is worth more than an error alone, so when one exists it
    // is drawn and the status line carries the failure instead.
    WeatherReading r;
    const bool haveStale = weatherLatest(r);
    if (haveStale) weatherRenderReading();

    if (!lvObjValid(s_weatherStatus)) return;
    // Every failure names the thing that can actually be changed. "Failed"
    // alone is indistinguishable from a bug.
    char msg[112];
    switch (st) {
        case WEATHER_ERR_NO_SERVER:
            snprintf(msg, sizeof(msg), TR("No weather server set.\nWeb Config -> Weather Server"));
            break;
        case WEATHER_ERR_NO_WIFI:
            snprintf(msg, sizeof(msg), TR("Wi-Fi needed to fetch conditions."));
            break;
        case WEATHER_ERR_NO_POSITION:
            snprintf(msg, sizeof(msg), TR("No position yet.\nNeeds a GPS fix or a set location."));
            break;
        case WEATHER_ERR_BADREPLY:
            snprintf(msg, sizeof(msg), TR("Server answered, but not with\nweather data. Check the proxy."));
            break;
        default: {
            const int code = weatherHttpCode();
            // A name that will not resolve is worth saying out loud: it points
            // at the address in Web Config or at DNS on the network, neither of
            // which "could not reach" would send anyone to look at.
            if (code > 0) {
                snprintf(msg, sizeof(msg), TR("Weather server error (HTTP %d)."), code);
            } else if (code == WX_ERR_DNS || code == WX_ERR_DNS_HOLD) {
                snprintf(msg, sizeof(msg),
                         TR("Can't resolve the weather server.\nCheck the address and DNS."));
            } else if (code == WX_ERR_NO_WIFI) {
                snprintf(msg, sizeof(msg), TR("Wi-Fi needed to fetch conditions."));
            } else {
                snprintf(msg, sizeof(msg), TR("Could not reach the weather server."));
            }
            break;
        }
    }
    if (haveStale) {
        // Keep the age on screen next to the reason the refresh failed.
        char both[192];
        snprintf(both, sizeof(both), TR("%s\n%lu min old"),
                 msg, (unsigned long)(weatherAgeMs() / 60000UL));
        lv_label_set_text(s_weatherStatus, both);
    } else {
        lv_label_set_text(s_weatherStatus, msg);
        if (lvObjValid(s_weatherBody)) lv_label_set_text(s_weatherBody, "");
        if (lvObjValid(s_weatherTemp)) lv_label_set_text(s_weatherTemp, "--");
        if (lvObjValid(s_weatherDesc) && !kWeatherHeroOneLine) {
            lv_label_set_text(s_weatherDesc, "");
        }
    }
}

static void closeWeatherModal() {
    if (s_weatherTimer) { lv_timer_del(s_weatherTimer); s_weatherTimer = nullptr; }
    if (lvObjValid(s_weatherModal)) lv_obj_del(s_weatherModal);
    // Weather has carried a bar, and therefore a status cluster, since long
    // before Tools and Help did -- and never nulled these. Same crash, reachable
    // from Tools -> Weather -> close.
    clearNavBarStatusIcons();
    s_weatherModal = nullptr;
    s_weatherStatus = nullptr;
    s_weatherBody = nullptr;
    s_weatherTitle = nullptr;
    s_weatherTemp = nullptr;
    s_weatherDesc = nullptr;
    weatherReset();
}

static void openWeatherModal() {
    if (!s_rootScreen || s_weatherModal) return;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);

    const WeatherInk ink = weatherInk();

    s_weatherModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_weatherModal, w, h);
    lv_obj_align(s_weatherModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_weatherModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_weatherModal, ink.bg, 0);
    lv_obj_set_style_border_color(s_weatherModal, ink.border, 0);
    lv_obj_set_style_bg_opa(s_weatherModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_weatherModal, 1, 0);
    // 4, not 6: appendHeltecBottomNav() cancels exactly this padding and a 1 px
    // border to put its bar on the display edges, so a modal that carries the
    // bar has to use the numbers the helper is written against or the bar sits
    // inset from the screen by the difference.
    lv_obj_set_style_pad_all(s_weatherModal, 4, 0);
#if UI_CORNER_SAFE_X > 0
    // Down out of the rounded top corners (board.h). The close X floats in the
    // top-right corner and would sit under the curve; floating children still
    // offset by the parent's padding, so the X and the title row move together.
    // Not on the P4 upright: no X there, and the title places itself.
#if HAS_TOOL_STACK_LAYOUT
    if (!toolStackLayout())
#endif
    lv_obj_set_style_pad_top(s_weatherModal, 4 + UI_CORNER_SAFE_X, 0);
#endif
    lv_obj_set_style_pad_row(s_weatherModal, kWeatherRowPad, 0);
    lv_obj_set_flex_flow(s_weatherModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_weatherModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    lv_obj_t *title = lv_label_create(s_weatherModal);
    s_weatherTitle = title;
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, ink.title, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_label_set_text(title, TR("Weather"));

    // No X on any build: the nav bar below is the way out. Upright, Tools'
    // title.
#if HAS_TOOL_STACK_LAYOUT
    if (toolStackLayout()) stackTitleOnXLine(title, /*reserveX=*/false);
#endif

    // The headline: the two things you opened the screen for, at a size that
    // reads from across a room. montserrat_32 rather than a larger face because
    // it is already linked on every board that has this screen — the lock-screen
    // clock uses it — so the size is free where a new one would not be.
    s_weatherTemp = lv_label_create(s_weatherModal);
    lv_obj_set_width(s_weatherTemp, lv_pct(100));
    lv_obj_set_style_text_font(s_weatherTemp, kWeatherHeroFont, 0);
    lv_obj_set_style_text_color(s_weatherTemp, ink.hero, 0);
    lv_obj_set_style_text_align(s_weatherTemp, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_bottom(s_weatherTemp, 0, 0);
    lv_label_set_text(s_weatherTemp, "--");

    if (!kWeatherHeroOneLine) {
        s_weatherDesc = lv_label_create(s_weatherModal);
        lv_obj_set_width(s_weatherDesc, lv_pct(100));
        lv_obj_set_style_text_font(s_weatherDesc, kWeatherDescFont, 0);
        lv_obj_set_style_text_color(s_weatherDesc, ink.hero, 0);
        lv_obj_set_style_text_align(s_weatherDesc, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(s_weatherDesc, LV_LABEL_LONG_WRAP);
        lv_label_set_text(s_weatherDesc, "");
    }

    // A rule, not a gap: the block above is one reading and the block below is
    // its supporting detail, and a line says that where whitespace only hints.
    lv_obj_t *rule = lv_obj_create(s_weatherModal);
    lv_obj_set_width(rule, lv_pct(100));
    lv_obj_set_height(rule, 1);
    lv_obj_clear_flag(rule, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(rule, 0, 0);
    lv_obj_set_style_radius(rule, 0, 0);
    lv_obj_set_style_pad_all(rule, 0, 0);
    lv_obj_set_style_bg_color(rule, ink.border, 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
    lv_obj_set_style_margin_top(rule, kWeatherRowPad, 0);
    lv_obj_set_style_margin_bottom(rule, kWeatherRowPad, 0);

    s_weatherBody = lv_label_create(s_weatherModal);
    lv_obj_set_width(s_weatherBody, lv_pct(100));
    lv_obj_set_style_text_font(s_weatherBody, kWeatherBodyFont, 0);
    lv_obj_set_style_text_color(s_weatherBody, ink.body, 0);
    lv_label_set_long_mode(s_weatherBody, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_weatherBody, "");

    s_weatherStatus = lv_label_create(s_weatherModal);
    lv_obj_set_width(s_weatherStatus, lv_pct(100));
    lv_obj_set_style_text_font(s_weatherStatus, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_weatherStatus, ink.muted, 0);
    lv_label_set_long_mode(s_weatherStatus, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_weatherStatus, "");

#if !UI_TOUCH_ONLY_PROFILE
    lv_obj_t *hint = lv_label_create(s_weatherModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, ink.muted, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(hint, TR("R=Refresh  %s=Back"), modalCloseKeyLabel());
#endif

    // Last child, so its spacer reserves the bottom of the column and nothing
    // above ends up underneath the bar. Tools is the active cell because that
    // is the surface this screen was opened from — the same relationship Live
    // has with its own entry.
    //
    // Added before the cached-reading return below, or the bar would appear on
    // a cold open and vanish on a warm one.
    appendHeltecBottomNav(s_weatherModal, HELTEC_NAV_TOOLS);

    // A reading inside the TTL is shown as-is. Reopening the screen should not
    // spend time, power and somebody else's API quota on a number that has not
    // changed since the last look.
    WeatherReading cached;
    if (weatherLatest(cached) && weatherAgeMs() < kWeatherTtlMs) {
        weatherRenderReading();
        return;
    }

    weatherRefresh();
}

// Starts a fetch and puts the screen into its waiting state. Separate from
// openWeatherModal() because R re-runs exactly this.
static void weatherRefresh() {
    if (!lvObjValid(s_weatherStatus)) return;

    double lat = 0, lon = 0;
    if (!nodeLosSelfPosition(lat, lon)) {
        lv_label_set_text(s_weatherStatus,
                          TR("No position yet.\nNeeds a GPS fix or a set location."));
        return;
    }

    weatherReset();
    lv_label_set_text(s_weatherStatus, TR("Fetching..."));
    if (!weatherRequest(s_cfg.weatherServer, lat, lon,
                        s_cfg.displayUnits != 0)) {
        weatherPoll(nullptr);    // publishes whatever refusal reason was set
        return;
    }
    if (s_weatherTimer) lv_timer_del(s_weatherTimer);
    s_weatherTimer = lv_timer_create(weatherPoll, 250, nullptr);
}
#endif  // HAS_WEATHER

static void openBeaconsModal() {
    // A UI rebuild deletes the root screen out from under us; without this
    // the stale pointer would lock the surface shut for the rest of the boot.
    if (s_beaconsModal && !lvObjAlive(s_beaconsModal)) {
        s_beaconsModal = nullptr;
        s_beaconsList = nullptr;
        s_beaconsStatusLabel = nullptr;
        s_beaconsTxLabel = nullptr;
        memset(s_beaconAgeLabels, 0, sizeof(s_beaconAgeLabels));
    }
    if (!s_rootScreen || s_beaconsModal) return;

    const int modalW = lv_disp_get_hor_res(NULL);
    const int modalH = lv_disp_get_ver_res(NULL);

    s_beaconsModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_beaconsModal, modalW, modalH);
    lv_obj_align(s_beaconsModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_beaconsModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_beaconsModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_beaconsModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_beaconsModal, 1, 0);
    lv_obj_set_style_border_color(s_beaconsModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_beaconsModal, 4, 0);
    lv_obj_set_style_pad_row(s_beaconsModal, 4, 0);
    lv_obj_set_flex_flow(s_beaconsModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_beaconsModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    // Portrait (P4, Heltec V4): Tools' title in place of the header bar. No X
    // in either orientation -- the nav bar added at the end is the way out.
#if HAS_TOOL_STACK_LAYOUT
    if (toolStackLayout()) {
        toolStackTitle(s_beaconsModal, TR("Beacons"));
    } else
#endif
    {
    lv_obj_t *header = lv_obj_create(s_beaconsModal);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_height(header, 26);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x123266), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_70, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_left(header, 4, 0);
    lv_obj_set_style_pad_right(header, 4, 0);
    lv_obj_set_style_pad_top(header, 1, 0);
    lv_obj_set_style_pad_bottom(header, 1, 0);

    cornerSafeHeader(header);
    lv_obj_t *title = lv_label_create(header);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(title, TR("Beacons"));
    lv_obj_center(title);
    }   // header bar

#if UI_TOUCH_ONLY_PROFILE
    // Touch build: no keyboard, so the action is a button, in the row under
    // the title (toolActionRow()).
    {
        lv_obj_t *actions = toolActionRow(s_beaconsModal);
        toolActionBtn(actions, TR("Clear"),
                      [](lv_event_t *e) { LV_UNUSED(e); beaconsClear(); });
    }
#endif

    s_beaconsStatusLabel = lv_label_create(s_beaconsModal);
    lv_obj_set_width(s_beaconsStatusLabel, lv_pct(100));
    lv_obj_set_style_text_font(s_beaconsStatusLabel, &lv_font_montserrat_10, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(s_beaconsStatusLabel, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(s_beaconsStatusLabel, lv_color_hex(0xB8D4FF), 0);
#endif
    lv_obj_set_style_text_align(s_beaconsStatusLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_beaconsStatusLabel, "-");

    // Our own beacon, above the list of everyone else's.
    s_beaconsTxLabel = lv_label_create(s_beaconsModal);
    lv_obj_set_width(s_beaconsTxLabel, lv_pct(100));
    lv_obj_set_style_text_font(s_beaconsTxLabel, &lv_font_montserrat_10, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(s_beaconsTxLabel, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(s_beaconsTxLabel, lv_color_hex(0xB8D4FF), 0);
#endif
    lv_obj_set_style_text_align(s_beaconsTxLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_beaconsTxLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_beaconsTxLabel, "-");

    s_beaconsList = lv_obj_create(s_beaconsModal);
    lv_obj_set_width(s_beaconsList, lv_pct(100));
    lv_obj_set_flex_grow(s_beaconsList, 1);
    lv_obj_add_flag(s_beaconsList, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_beaconsList);
    lv_obj_set_scrollbar_mode(s_beaconsList, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(s_beaconsList, lv_color_hex(0x123266), 0);
    lv_obj_set_style_bg_opa(s_beaconsList, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_beaconsList, 1, 0);
    lv_obj_set_style_border_color(s_beaconsList, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_all(s_beaconsList, 3, 0);
    lv_obj_set_style_pad_row(s_beaconsList, 4, 0);
    lv_obj_set_style_width(s_beaconsList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_beaconsList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_beaconsList, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(s_beaconsList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_beaconsList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

#if !UI_TOUCH_ONLY_PROFILE
    lv_obj_t *hint = lv_label_create(s_beaconsModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_label_set_text_fmt(hint, TR("C = Clear   %s = Back"), modalCloseKeyLabel());
#endif

    // The way out of this screen on every build: it has no X. Tools lit, as
    // the screen it was opened from. Last, so its spacer is the final child of
    // the column. A no-op where the bar is switched off.
    appendHeltecBottomNav(s_beaconsModal, HELTEC_NAV_TOOLS);

    refreshBeaconsModal(true);
}

// ── MQTT Monitor modal ───────────────────────────────────────────────────────
// What is actually arriving under the configured root: a scrolling grid of
// channel + message count, two cells to a line. Not a message viewer — the
// payloads are counted and thrown away, which is what keeps this cheap enough to
// leave up. The gateway half of each topic is merged away upstream in the
// bridge, so three gateways carrying KAM-NET are one cell reading 3, not three
// cells reading 1.
//
// Everything about it is transient by design. Counting starts when the screen
// opens and the table is freed when it closes, so reopening always begins from
// zero, and the bridge's own state — connection, subscription, downlink — is
// untouched either way. It rides the subscription the bridge already holds
// rather than opening a second client, so a board with no broker session shows
// why instead of dialing one of its own.
#if FEATURE_MQTT_MONITOR

// Cell geometry for the two-column grid. The narrowest panel this screen is
// built for is 320 px wide, which leaves ~150 px per cell — enough for a channel
// name and its count without either being clipped.
static constexpr int kMqttMonCellPct   = 49;
static constexpr int kMqttMonCountW    = 44;

static lv_obj_t *s_mqttMonModal = nullptr;
static lv_obj_t *s_mqttMonList = nullptr;
static lv_obj_t *s_mqttMonStatusLabel = nullptr;
// One per row, so a ticking count is a label rewrite rather than a rebuild of a
// list the user is reading (and scrolled).
static lv_obj_t *s_mqttMonCountLabels[MQTT_MONITOR_TOPIC_SLOTS] = {};
static int       s_mqttMonRenderedRows = -1;
static uint32_t  s_mqttMonRenderedTopicSeq = 0;
static uint32_t  s_mqttMonStartMs = 0;

// ── Send-to-channel ──────────────────────────────────────────────────────────
// Puts the top few topics on the mesh, for the times when the interesting thing
// about a broker is worth telling the people not standing next to you. It is a
// transmission on a shared channel, so it is deliberately awkward: pick the
// channel, confirm it, and then wait a minute before you can do it again.
static constexpr uint32_t kMqttSendCooldownMs = 60000;
static constexpr int      kMqttSendTopN       = 5;

static lv_obj_t *s_mqttSendBackdrop = nullptr;   // channel picker
static lv_obj_t *s_mqttSendModal    = nullptr;
static lv_obj_t *s_mqttSendRows[MESH_CHANNELS] = {};
static int       s_mqttSendSelection = 0;

static lv_obj_t *s_mqttConfirmBackdrop = nullptr;  // "really send?" prompt
static lv_obj_t *s_mqttConfirmModal    = nullptr;
static int       s_mqttConfirmChan     = -1;

// millis() of the last send. 0 means "never", which is why the elapsed test
// below is written to treat it as ready rather than as a very old send.
static uint32_t  s_mqttSendLastMs = 0;

// A short-lived line that outranks the running status until it expires — the
// status is rewritten twice a second, so anything said there without this would
// be gone before it was read.
static char      s_mqttMonNotice[72] = {};
static uint32_t  s_mqttMonNoticeUntilMs = 0;

static void mqttMonNotice(const char *msg, uint32_t holdMs = 3000) {
    snprintf(s_mqttMonNotice, sizeof(s_mqttMonNotice), "%s", msg ? msg : "");
    uint32_t until = millis() + holdMs;
    if (until == 0) until = 1;          // 0 is the "no notice" sentinel
    s_mqttMonNoticeUntilMs = until;
}

static bool mqttMonNoticeActive() {
    if (s_mqttMonNoticeUntilMs == 0) return false;
    if ((int32_t)(millis() - s_mqttMonNoticeUntilMs) >= 0) {
        s_mqttMonNoticeUntilMs = 0;
        return false;
    }
    return true;
}

// Seconds still to wait, or 0 when a send is allowed.
static uint32_t mqttSendCooldownLeftS() {
    if (s_mqttSendLastMs == 0) return 0;
    const uint32_t elapsed = (uint32_t)(millis() - s_mqttSendLastMs);
    if (elapsed >= kMqttSendCooldownMs) return 0;
    return (kMqttSendCooldownMs - elapsed + 999UL) / 1000UL;
}

// The top N channels by message count, most first. Ties keep first-seen order,
// which is the order the screen shows — so the message agrees with the list the
// sender was looking at when they pressed Send.
static int mqttMonTopIndices(int *out, int maxOut) {
    const int total = mqttMonitorTopicCount();
    int n = 0;
    for (int slot = 0; slot < maxOut && slot < total; slot++) {
        int best = -1;
        uint32_t bestCount = 0;
        for (int i = 0; i < total; i++) {
            bool taken = false;
            for (int j = 0; j < n; j++) {
                if (out[j] == i) { taken = true; break; }
            }
            if (taken) continue;
            const MqttTopicStat *st = mqttMonitorTopicAt(i);
            if (!st) continue;
            if (best < 0 || st->count > bestCount) { best = i; bestCount = st->count; }
        }
        if (best < 0) break;
        out[n++] = best;
    }
    return n;
}

// ── Scan ─────────────────────────────────────────────────────────────────────
// A timed count, set up the way Discovery sets up a sweep: W (Heltec: the Scan
// button) asks how long to listen and whether to save while it runs, then the
// count starts over so the result describes exactly that window. When the
// window closes the counting carries on as before -- the scan only decides what
// the saved file covers and when it is called final.
//
// Everything still lives and dies with the screen. Closing it ends a scan early,
// with one last write first, since the table is freed straight after.
struct MqttScanDurationOption {
    uint32_t    ms;
    const char *label;
};
static const MqttScanDurationOption kMqttScanDurations[] = {
    {    60000UL, TR_NOOP("1 min")   },
    {   300000UL, TR_NOOP("5 min")   },
    {   600000UL, TR_NOOP("10 min")  },
    {   900000UL, TR_NOOP("15 min")  },
    {  1800000UL, TR_NOOP("30 min")  },
    {  3600000UL, TR_NOOP("1 hour")  },
    {  7200000UL, TR_NOOP("2 hours") },
    { 21600000UL, TR_NOOP("6 hours") },
};
static constexpr int kMqttScanDurationCount =
    (int)(sizeof(kMqttScanDurations) / sizeof(kMqttScanDurations[0]));

static uint32_t s_mqttScanWindowMs = 300000UL;   // last chosen; 5 min until then
static uint32_t s_mqttScanStartedMs = 0;         // 0 = no scan running
// "Save while scanning", from MQTT Scan Settings. Off at boot and remembered
// for the session, like Discovery's.
static bool     s_mqttScanSaveWhileRunning = false;

static bool mqttScanRunning() { return s_mqttScanStartedMs != 0; }

#if HAS_FILE_STORAGE
// One file per scan, picked when it starts and rewritten as the counts move, so
// a long scan leaves one current snapshot on the card even if it is cut short.
// Rate-limited like Discovery's, since each write is the whole file.
static constexpr uint32_t kMqttScanSaveMinGapMs = 5000;
static bool     s_mqttScanSaveActive = false;
static char     s_mqttScanSavePath[64] = {};
static uint32_t s_mqttScanSaveTotal = 0;
static uint32_t s_mqttScanSaveMs = 0;

// Same rules as discoveryJsonEscape(), which lives behind FEATURE_DISCOVERY.
static void mqttScanJsonEscape(const char *in, char *out, size_t outLen) {
    if (!out || outLen == 0) return;
    size_t o = 0;
    for (size_t i = 0; in && in[i] && o + 2 < outLen; i++) {
        const unsigned char c = (unsigned char)in[i];
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c < 0x20) {
            out[o++] = ' ';
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
}

// A fresh /camillia/mqtt-<stamp>.json that does not exist yet.
static bool mqttScanPickSavePath(char *path, size_t pathLen, char *msg, size_t msgLen) {
    if (!storageBegin()) {
        snprintf(msg, msgLen, TR("No %s - not saving"), storageName());
        return false;
    }
    storageFs().mkdir("/camillia");

    char stamp[32];
    const time_t nowEpoch = time(nullptr);
    if (nowEpoch >= 1700000000) {
        struct tm lt;
        localtime_r(&nowEpoch, &lt);
        strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &lt);
    } else {
        snprintf(stamp, sizeof(stamp), "boot-%lus", (unsigned long)(millis() / 1000UL));
    }

    snprintf(path, pathLen, "/camillia/mqtt-%s.json", stamp);
    for (int attempt = 2; attempt <= 9 && storageFs().exists(path); attempt++) {
        snprintf(path, pathLen, "/camillia/mqtt-%s-%d.json", stamp, attempt);
    }
    if (storageFs().exists(path)) {
        snprintf(msg, msgLen, TR("Save failed - too many this second"));
        return false;
    }
    return true;
}

// Writes the current table to `path`, replacing whatever is there. `done` marks
// the final write, so a file cut off by a reboot reads as unfinished.
static bool mqttScanWriteJson(const char *path, bool done, char *msg, size_t msgLen) {
    if (!storageBegin()) {
        snprintf(msg, msgLen, TR("No %s - not saved"), storageName());
        return false;
    }
    File f = storageFs().open(path, FILE_WRITE);
    if (!f) {
        snprintf(msg, msgLen, TR("Save failed - cannot write %s"), storageName());
        return false;
    }

    const uint32_t now = millis();
    const time_t nowEpoch = time(nullptr);
    char rootEsc[2 * sizeof(s_cfg.mqttRoot)];
    mqttScanJsonEscape(s_cfg.mqttRoot, rootEsc, sizeof(rootEsc));

    f.print("{\n");
    f.printf("  \"node\": \"!%08lX\",\n", (unsigned long)s_myNodeId);
    f.printf("  \"root\": \"%s\",\n", rootEsc);
    f.printf("  \"filter\": \"%s/2/e/#\",\n", rootEsc);
    if (nowEpoch >= 1700000000) {
        char iso[32];
        struct tm lt;
        localtime_r(&nowEpoch, &lt);
        strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%S", &lt);
        f.printf("  \"generated\": \"%s\",\n", iso);
    } else {
        f.print("  \"generated\": null,\n");   // clock not set; do not invent one
    }
    f.printf("  \"uptimeMs\": %lu,\n", (unsigned long)now);
    f.printf("  \"scan\": {\"windowMs\": %lu, \"elapsedMs\": %lu, \"done\": %s},\n",
             (unsigned long)s_mqttScanWindowMs,
             (unsigned long)(now - s_mqttMonStartMs),
             done ? "true" : "false");
    f.printf("  \"counts\": {\"channels\": %d, \"messages\": %lu, \"offList\": %lu},\n",
             mqttMonitorTopicCount(),
             (unsigned long)mqttMonitorTotalMsgs(),
             (unsigned long)mqttMonitorOtherMsgs());

    // First-seen order, the order the screen shows.
    f.print("  \"channels\": [");
    const int rows = mqttMonitorTopicCount();
    bool first = true;
    for (int i = 0; i < rows; i++) {
        const MqttTopicStat *st = mqttMonitorTopicAt(i);
        if (!st) break;
        char chanEsc[2 * MQTT_MONITOR_TOPIC_MAX];
        mqttScanJsonEscape(st->channel, chanEsc, sizeof(chanEsc));
        f.printf("%s\n    {\"channel\": \"%s\", \"count\": %lu}",
                 first ? "" : ",", chanEsc, (unsigned long)st->count);
        first = false;
    }
    f.print(first ? "]\n}\n" : "\n  ]\n}\n");

    const size_t bytes = (size_t)f.position();
    f.close();

    Serial.printf("[mqtt-mon] saved %s (%u bytes)\n", path, (unsigned)bytes);
    const char *name = strrchr(path, '/');
    snprintf(msg, msgLen, TR("Saved %s"), name ? name + 1 : path);
    return true;
}

static void mqttScanSaveBegin() {
    s_mqttScanSaveActive = false;
    char msg[72];
    if (!mqttScanPickSavePath(s_mqttScanSavePath, sizeof(s_mqttScanSavePath),
                              msg, sizeof(msg))
        || !mqttScanWriteJson(s_mqttScanSavePath, false, msg, sizeof(msg))) {
        // The scan goes ahead regardless; it just says it is not being saved.
        mqttMonNotice(msg, 5000);
        Serial.printf("[mqtt-mon] save while scanning unavailable: %s\n", msg);
        return;
    }
    s_mqttScanSaveActive = true;
    s_mqttScanSaveTotal = mqttMonitorTotalMsgs();
    s_mqttScanSaveMs = millis();
    Serial.printf("[mqtt-mon] saving as it scans to %s\n", s_mqttScanSavePath);
}

// Periodic rewrite while the scan runs: at most every few seconds, and only when
// something new has arrived (the total covers every count, off-list included).
static void mqttScanSaveService() {
    if (!s_mqttScanSaveActive) return;
    const uint32_t now = millis();
    if ((uint32_t)(now - s_mqttScanSaveMs) < kMqttScanSaveMinGapMs) return;
    const uint32_t total = mqttMonitorTotalMsgs();
    if (total == s_mqttScanSaveTotal) return;
    s_mqttScanSaveTotal = total;
    s_mqttScanSaveMs = now;
    char msg[72];
    if (!mqttScanWriteJson(s_mqttScanSavePath, false, msg, sizeof(msg))) {
        // Keep trying: a card that was briefly busy should not end the saving.
        Serial.printf("[mqtt-mon] live save failed: %s\n", msg);
    }
}
#else
static inline void mqttScanSaveBegin() {}
static inline void mqttScanSaveService() {}
#endif  // HAS_FILE_STORAGE

// Ends the scan in flight, if there is one: the final write, then a notice
// saying how it went. Must run before mqttMonitorStop() frees the table.
static void mqttScanFinish(const char *why) {
    if (!mqttScanRunning()) return;
    s_mqttScanStartedMs = 0;
    char msg[72];
    snprintf(msg, sizeof(msg), TR("Scan %s"), why);
#if HAS_FILE_STORAGE
    if (s_mqttScanSaveActive) {
        s_mqttScanSaveActive = false;
        char saveMsg[72];
        if (mqttScanWriteJson(s_mqttScanSavePath, true, saveMsg, sizeof(saveMsg))) {
            const char *name = strrchr(s_mqttScanSavePath, '/');
            snprintf(msg, sizeof(msg), TR("Scan %s - saved %s"), why,
                     name ? name + 1 : s_mqttScanSavePath);
        } else {
            snprintf(msg, sizeof(msg), "%s", saveMsg);
        }
    }
#endif
    mqttMonNotice(msg, 8000);
    Serial.printf("[mqtt-mon] %s\n", msg);
}

// From the UI tick while the screen is up: closes the window when it is over,
// and keeps the file current until then.
static void mqttScanService() {
    if (!mqttScanRunning()) return;
    if ((uint32_t)(millis() - s_mqttScanStartedMs) >= s_mqttScanWindowMs) {
        mqttScanFinish("done");
        return;
    }
    mqttScanSaveService();
}

static void mqttMonitorReset();   // below; a scan starts from a clean count

static const char *mqttScanDurationLabelFor(int idx) {
    if (idx < 0 || idx >= kMqttScanDurationCount) idx = 0;
    return TR(kMqttScanDurations[idx].label);
}

static void mqttScanDurationApply(int idx) {
    if (!s_mqttMonModal) return;
    if (idx < 0 || idx >= kMqttScanDurationCount) idx = 0;
    s_mqttScanWindowMs = kMqttScanDurations[idx].ms;
    mqttMonitorReset();              // also ends a scan already in flight
    s_mqttScanStartedMs = millis();
    if (s_mqttScanStartedMs == 0) s_mqttScanStartedMs = 1;   // 0 means "none"
    if (s_mqttScanSaveWhileRunning) mqttScanSaveBegin();
    Serial.printf("[mqtt-mon] scan started (%s)\n", mqttScanDurationLabelFor(idx));
}

// The Scan button and W. Opens on whatever length was used last.
static void openMqttScanModal() {
    if (!s_mqttMonModal) return;
    int startIdx = 0;
    for (int i = 0; i < kMqttScanDurationCount; i++) {
        if (kMqttScanDurations[i].ms == s_mqttScanWindowMs) { startIdx = i; break; }
    }
#if HAS_FILE_STORAGE
    static const char *const kSaveWhileLabel = TR_NOOP("Save while scanning");
    bool *const saveWhileValue = &s_mqttScanSaveWhileRunning;
#else
    static const char *const kSaveWhileLabel = nullptr;
    bool *const saveWhileValue = nullptr;
#endif
    static const CfgSliderPicker kScanSpec = {
        TR_NOOP("MQTT Scan Settings"),
        kMqttScanDurationCount,
        mqttScanDurationLabelFor,
        mqttScanDurationApply,
        TR_NOOP("1 min"),
        TR_NOOP("6 hours"),
        kSaveWhileLabel,
        saveWhileValue,
    };
    openCfgSliderModal(&kScanSpec, startIdx);
}


static void closeMqttMonitorModal() {
    if (!s_mqttMonModal) return;
    mqttScanFinish("stopped");
    // Both are parented to the root screen, so neither goes down with the modal
    // that opened them.
    closeMqttConfirmModal();
    closeMqttSendModal();
    lvObjDeleteSafe(s_mqttMonModal);
    s_mqttMonList = nullptr;
    s_mqttMonStatusLabel = nullptr;
    memset(s_mqttMonCountLabels, 0, sizeof(s_mqttMonCountLabels));
    s_mqttMonRenderedRows = -1;
    s_mqttMonRenderedTopicSeq = 0;
    // The whole point of the screen being transient: the table goes back to the
    // heap the moment it stops being looked at.
    mqttMonitorStop();
}

// Why nothing is arriving, in the order the user would have to fix it. Returns
// nullptr once the broker session is up, which is the only case with no problem
// to report.
static const char *mqttMonitorBlockedReason() {
    if (!mqttMonitorActive())                return TR("Not counting - out of memory");
    if (!s_cfg.wifiEnabled)                  return TR("WiFi is off");
    if (!s_cfg.mqttEnabled)                  return TR("MQTT bridge is off");
    if (!wifiHasActiveCreds())               return TR("No WiFi network configured");
    if (WiFi.status() != WL_CONNECTED)       return TR("WiFi not connected");
    if (!mqttBridgeConnected())              return TR("Connecting to broker...");
    return nullptr;
}

// Saturated counters print with a trailing '+' so a pegged number reads as "at
// least this many" rather than as a number that stopped being true.
static void mqttMonitorFormatCount(uint32_t n, char *out, size_t outLen) {
    if (n >= kMqttMonitorCountMax) snprintf(out, outLen, "%lu+", (unsigned long)n);
    else                           snprintf(out, outLen, "%lu", (unsigned long)n);
}

static void mqttMonitorBuildList() {
    if (!s_mqttMonList) return;
    lv_obj_clean(s_mqttMonList);
    memset(s_mqttMonCountLabels, 0, sizeof(s_mqttMonCountLabels));

    const int rows = mqttMonitorTopicCount();
    s_mqttMonRenderedRows = rows;

    if (rows == 0) {
        char empty[220];
        if (!mqttMonitorActive()) {
            // The table is ~2 KB. If that could not be found there is no
            // counting happening at all, which is worth saying plainly.
            snprintf(empty, sizeof(empty),
                     TR("Not enough free heap to hold the topic table.\n\n"
                     "Close a few screens (or reboot) and try again."));
        } else if (const char *why = mqttMonitorBlockedReason()) {
            snprintf(empty, sizeof(empty),
                     TR("%s.\n\nNothing can arrive until the bridge has a broker "
                     "session. Fix it in Config (or web config) and this list "
                     "fills in on its own."), why);
        } else {
            snprintf(empty, sizeof(empty),
                     TR("Watching %s/2/e/#\n\nNo messages yet. Channels appear here "
                     "as they are published, newest at the bottom, with the "
                     "number of messages seen on each."),
                     s_cfg.mqttRoot);
        }
        beaconsMakeLabel(s_mqttMonList, kBeaconsBodyFont, kBeaconsBodyColor, empty);
        return;
    }

    char countText[16];
    for (int i = 0; i < rows && i < MQTT_MONITOR_TOPIC_SLOTS; i++) {
        const MqttTopicStat *st = mqttMonitorTopicAt(i);
        if (!st) break;

        // Two cells per line: a channel name and a count are a handful of
        // characters, and one per full-width line left most of the panel empty.
        // 49% leaves the gutter that pad_column draws between the columns, the
        // same split the channel and Tools grids use.
        lv_obj_t *row = lv_obj_create(s_mqttMonList);
        lv_obj_set_width(row, lv_pct(kMqttMonCellPct));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(row, lv_color_make(0, 0, 0), 0);
    #else
        lv_obj_set_style_bg_color(row, lv_color_hex(0x16386F), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_50, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x335D9D), 0);
    #endif
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 3, 0);
        lv_obj_set_style_pad_column(row, 4, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        // Channel takes the width that is left; the count is the column that has
        // to stay readable, so it is the one with a reserved size.
        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_flex_grow(name, 1);
        lv_obj_set_style_text_font(name, kBeaconsBodyFont, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(kBeaconsBodyColor), 0);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_label_set_text(name, st->channel);

        mqttMonitorFormatCount(st->count, countText, sizeof(countText));
        lv_obj_t *count = lv_label_create(row);
        lv_obj_set_width(count, kMqttMonCountW);
        lv_obj_set_style_text_font(count, kBeaconsBodyFont, 0);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_text_color(count, lv_color_make(0, 0, 0), 0);
    #else
        lv_obj_set_style_text_color(count, lv_color_hex(kBeaconsTitleColor), 0);
    #endif
        lv_obj_set_style_text_align(count, LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_long_mode(count, LV_LABEL_LONG_CLIP);
        lv_label_set_text(count, countText);
        s_mqttMonCountLabels[i] = count;
    }
}

static void refreshMqttMonitorModal(bool force) {
    if (!s_mqttMonModal || !s_mqttMonList) return;
    if (!lvObjValid(s_mqttMonModal)) {
        s_mqttMonModal = nullptr;
        s_mqttMonList = nullptr;
        s_mqttMonStatusLabel = nullptr;
        memset(s_mqttMonCountLabels, 0, sizeof(s_mqttMonCountLabels));
        mqttScanFinish("stopped");
        mqttMonitorStop();
        return;
    }

    // Before the throttle below: the window has to close on time, and the
    // notice it leaves has to be up before the status line is written.
    mqttScanService();

    // The list only changes shape when a topic is seen for the first time, and
    // that is exactly what the topic sequence counts. Everything else that moves
    // on this screen is a number inside a row that already exists.
    const uint32_t seq = mqttMonitorTopicSeq();
    const bool rebuild = force || seq != s_mqttMonRenderedTopicSeq
                      || mqttMonitorTopicCount() != s_mqttMonRenderedRows;
    if (rebuild) {
        s_mqttMonRenderedTopicSeq = seq;
        mqttMonitorBuildList();
    }

    // Twice a second: on a busy root every one of these numbers changes many
    // times a second, and lv_label_set_text invalidates unconditionally — at UI
    // tick rate this screen would spend the whole loop redrawing itself.
    static uint32_t s_lastTallyMs = 0;
    const uint32_t now = millis();
    if (!rebuild && (uint32_t)(now - s_lastTallyMs) < 500UL) return;
    s_lastTallyMs = now;

    if (s_mqttMonStatusLabel) {
        char status[96];
        const char *why = mqttMonitorBlockedReason();
        if (mqttMonNoticeActive()) {
            // Sent / refused / cooling down: outranks the running tally until it
            // expires, because the tally would overwrite it within half a second.
            snprintf(status, sizeof(status), "%s", s_mqttMonNotice);
        } else if (why) {
            snprintf(status, sizeof(status), "%s", why);
        } else {
            char total[16];
            mqttMonitorFormatCount(mqttMonitorTotalMsgs(), total, sizeof(total));
            // How long this session has been counting, so the message count has
            // a denominator — 400 messages means nothing without it.
            char span[16];
            beaconsFormatSpan((uint32_t)(now - s_mqttMonStartMs), span, sizeof(span));
            if (mqttScanRunning()) {
                // "5m/1h": counting towards the chosen window, so a long scan
                // does not read like a short one that has overrun.
                char window[16];
                beaconsFormatSpan(s_mqttScanWindowMs, window, sizeof(window));
                const size_t used = strlen(span);
                snprintf(span + used, sizeof(span) - used, "/%s", window);
            }
            // The file name is on the notice the scan leaves when it ends; while
            // it runs a word is all the line has room for.
#if HAS_FILE_STORAGE
            const char *saving = s_mqttScanSaveActive ? "  saving" : "";
#else
            const char *saving = "";
#endif
            const uint32_t other = mqttMonitorOtherMsgs();
            if (other) {
                char otherText[16];
                mqttMonitorFormatCount(other, otherText, sizeof(otherText));
                // The table is full, so say so rather than letting the list read
                // as the whole picture.
                snprintf(status, sizeof(status), TR("%d chans  %s msgs  %s%s  (+%s off-list)"),
                         mqttMonitorTopicCount(), total, span, saving, otherText);
            } else {
                snprintf(status, sizeof(status), TR("%d chans  %s msgs  %s%s"),
                         mqttMonitorTopicCount(), total, span, saving);
            }
        }
        const char *shown = lv_label_get_text(s_mqttMonStatusLabel);
        if (!shown || strcmp(shown, status) != 0) {
            lv_label_set_text(s_mqttMonStatusLabel, status);
        }
    }

    char countText[16];
    const int rows = mqttMonitorTopicCount();
    for (int i = 0; i < rows && i < MQTT_MONITOR_TOPIC_SLOTS; i++) {
        if (!s_mqttMonCountLabels[i]) continue;
        const MqttTopicStat *st = mqttMonitorTopicAt(i);
        if (!st) break;
        mqttMonitorFormatCount(st->count, countText, sizeof(countText));
        const char *shown = lv_label_get_text(s_mqttMonCountLabels[i]);
        if (shown && strcmp(shown, countText) == 0) continue;
        lv_label_set_text(s_mqttMonCountLabels[i], countText);
    }
}

// Start over without leaving the screen: same thing closing and reopening does,
// minus the teardown.
static void mqttMonitorReset() {
    mqttScanFinish("stopped");   // what it had is saved before the count goes
    mqttMonitorStop();
    mqttMonitorStart();
    s_mqttMonStartMs = millis();
    s_mqttMonRenderedTopicSeq = mqttMonitorTopicSeq();
    s_mqttMonRenderedRows = -1;
    refreshMqttMonitorModal(true);
}

static void openMqttMonitorModal() {
    // Same guard the other tool surfaces carry: a UI rebuild deletes the root
    // screen out from under the stale pointer.
    if (s_mqttMonModal && !lvObjAlive(s_mqttMonModal)) {
        s_mqttMonModal = nullptr;
        s_mqttMonList = nullptr;
        s_mqttMonStatusLabel = nullptr;
        memset(s_mqttMonCountLabels, 0, sizeof(s_mqttMonCountLabels));
        mqttScanFinish("stopped");
        mqttMonitorStop();
    }
    if (!s_rootScreen || s_mqttMonModal) return;

    // A failure here is not fatal to the screen: the list and status both say so
    // on their own, which beats refusing to open with no explanation.
    (void)mqttMonitorStart();
    s_mqttMonStartMs = millis();
    s_mqttMonRenderedTopicSeq = mqttMonitorTopicSeq();
    s_mqttMonRenderedRows = -1;

    const int modalW = lv_disp_get_hor_res(NULL);
    const int modalH = lv_disp_get_ver_res(NULL);

    s_mqttMonModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_mqttMonModal, modalW, modalH);
    lv_obj_align(s_mqttMonModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_mqttMonModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_mqttMonModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_mqttMonModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_mqttMonModal, 1, 0);
    lv_obj_set_style_border_color(s_mqttMonModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_mqttMonModal, 4, 0);
    lv_obj_set_style_pad_row(s_mqttMonModal, 4, 0);
    lv_obj_set_flex_flow(s_mqttMonModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_mqttMonModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    // The topic is the title, across the whole width in either orientation --
    // there is no X and nothing beside it; Scan / Send / Reset are in the row
    // under it. Portrait (P4, Heltec V4): Tools' title in place of the header
    // bar. The nav bar added at the end is the way out.
#if HAS_TOOL_STACK_LAYOUT
    if (toolStackLayout()) {
        char topic[64];
        snprintf(topic, sizeof(topic), "%s/2/e/#", s_cfg.mqttRoot);
        toolStackTitle(s_mqttMonModal, topic);
    } else
#endif
    {
    lv_obj_t *header = lv_obj_create(s_mqttMonModal);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_height(header, 26);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x123266), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_70, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_left(header, 4, 0);
    lv_obj_set_style_pad_right(header, 4, 0);
    lv_obj_set_style_pad_top(header, 1, 0);
    lv_obj_set_style_pad_bottom(header, 1, 0);

    // The root is the whole subject of the screen, so it is the title.
    char titleText[64];
    snprintf(titleText, sizeof(titleText), "%s/2/e/#", s_cfg.mqttRoot);
    cornerSafeHeader(header);
    lv_obj_t *title = lv_label_create(header);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_label_set_text(title, titleText);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(title);
    }   // header bar

#if UI_TOUCH_ONLY_PROFILE
    {
        lv_obj_t *actions = toolActionRow(s_mqttMonModal);
        toolActionBtn(actions, TR("Scan"),
                      [](lv_event_t *e) { LV_UNUSED(e); openMqttScanModal(); });
        toolActionBtn(actions, TR("Send"),
                      [](lv_event_t *e) { LV_UNUSED(e); openMqttSendModal(); });
        toolActionBtn(actions, TR("Reset"),
                      [](lv_event_t *e) { LV_UNUSED(e); mqttMonitorReset(); });
    }
#endif

    s_mqttMonStatusLabel = lv_label_create(s_mqttMonModal);
    lv_obj_set_width(s_mqttMonStatusLabel, lv_pct(100));
    lv_obj_set_style_text_font(s_mqttMonStatusLabel, &lv_font_montserrat_10, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(s_mqttMonStatusLabel, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(s_mqttMonStatusLabel, lv_color_hex(0xB8D4FF), 0);
#endif
    lv_obj_set_style_text_align(s_mqttMonStatusLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_mqttMonStatusLabel, "-");

    s_mqttMonList = lv_obj_create(s_mqttMonModal);
    lv_obj_set_width(s_mqttMonList, lv_pct(100));
    lv_obj_set_flex_grow(s_mqttMonList, 1);
    lv_obj_add_flag(s_mqttMonList, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_mqttMonList);
    lv_obj_set_scrollbar_mode(s_mqttMonList, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(s_mqttMonList, lv_color_hex(0x123266), 0);
    lv_obj_set_style_bg_opa(s_mqttMonList, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_mqttMonList, 1, 0);
    lv_obj_set_style_border_color(s_mqttMonList, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_all(s_mqttMonList, 3, 0);
    lv_obj_set_style_pad_row(s_mqttMonList, 3, 0);
    lv_obj_set_style_pad_column(s_mqttMonList, 3, 0);
    lv_obj_set_style_width(s_mqttMonList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_mqttMonList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_mqttMonList, LV_OPA_70, LV_PART_SCROLLBAR);
    // Row-wrap rather than a column: cells fill left to right, top to bottom, so
    // a channel that appears while you are watching lands where reading order
    // says it should — and an odd count leaves one gap, at the end, not a hole
    // in the middle of a column-major fill.
    lv_obj_set_flex_flow(s_mqttMonList, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_mqttMonList, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

#if !UI_TOUCH_ONLY_PROFILE
    lv_obj_t *hint = lv_label_create(s_mqttMonModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_label_set_text_fmt(hint, TR("W = Scan   S = Send   C = Reset   %s = Back"),
                          modalCloseKeyLabel());
#endif

    // The way out of this screen on every build: it has no X. Tools lit, as
    // the screen it was opened from. Last, so its spacer is the final child of
    // the column. A no-op where the bar is switched off.
    appendHeltecBottomNav(s_mqttMonModal, HELTEC_NAV_TOOLS);

    refreshMqttMonitorModal(true);
}


// "MQTT msh/US/MI 12m: KAM-NET 42, CFW 18, LongFast 7"
//
// Built up entry by entry and stopped at the first one that will not fit, so the
// text is always a whole list of whole entries rather than something cut mid
// number. Meshtastic caps a text at MESH_TEXT_MAX_LEN bytes.
static void mqttMonBuildSendText(char *out, size_t outLen) {
    char span[16];
    beaconsFormatSpan((uint32_t)(millis() - s_mqttMonStartMs), span, sizeof(span));

    int n = snprintf(out, outLen, "MQTT %s %s:", s_cfg.mqttRoot, span);
    if (n < 0 || (size_t)n >= outLen) return;

    int top[kMqttSendTopN];
    const int count = mqttMonTopIndices(top, kMqttSendTopN);
    for (int i = 0; i < count; i++) {
        const MqttTopicStat *st = mqttMonitorTopicAt(top[i]);
        if (!st) continue;
        char entry[MQTT_MONITOR_TOPIC_MAX + 24];
        char countText[16];
        mqttMonitorFormatCount(st->count, countText, sizeof(countText));
        snprintf(entry, sizeof(entry), "%s %s %s",
                 (i == 0) ? "" : ",", st->channel, countText);
        if ((size_t)n + strlen(entry) >= outLen) break;   // whole entries only
        n += snprintf(out + n, outLen - n, "%s", entry);
    }
}

// ── Send: channel picker ─────────────────────────────────────────────────────
static void closeMqttSendModal() {
    if (lvObjValid(s_mqttSendBackdrop)) {
        lv_obj_del(s_mqttSendBackdrop);
    } else if (lvObjValid(s_mqttSendModal)) {
        lv_obj_del(s_mqttSendModal);
    }
    s_mqttSendBackdrop = nullptr;
    s_mqttSendModal = nullptr;
    memset(s_mqttSendRows, 0, sizeof(s_mqttSendRows));
}

static void closeMqttConfirmModal() {
    if (lvObjValid(s_mqttConfirmBackdrop)) {
        lv_obj_del(s_mqttConfirmBackdrop);
    } else if (lvObjValid(s_mqttConfirmModal)) {
        lv_obj_del(s_mqttConfirmModal);
    }
    s_mqttConfirmBackdrop = nullptr;
    s_mqttConfirmModal = nullptr;
    s_mqttConfirmChan = -1;
}

// The send itself. Everything above this point is asking; this is the only place
// that transmits.
static void mqttSendConfirmed() {
    const int chan = s_mqttConfirmChan;
    closeMqttConfirmModal();
    if (chan < 0 || chan >= MESH_CHANNELS) return;

    // Re-checked here rather than trusting the gate that opened the picker: the
    // confirm prompt can sit on screen indefinitely, and a stale check is how
    // two sends land seconds apart.
    const uint32_t waitS = mqttSendCooldownLeftS();
    if (waitS > 0) {
        char msg[48];
        snprintf(msg, sizeof(msg), TR("Wait %lus before sending again"), (unsigned long)waitS);
        mqttMonNotice(msg);
        return;
    }

    if (s_myNodeId == 0) deriveNodeId();
    if (s_myNodeId == 0) {
        mqttMonNotice(TR("Cannot send: no node id"));
        return;
    }

    char text[MESH_TEXT_MAX_LEN + 1];
    mqttMonBuildSendText(text, sizeof(text));

    const bool ok = Channels.sendText(s_myNodeId, text, s_cfg.okToMqtt, chan, 0);
    char msg[64];
    if (ok) {
        // Only a real transmission starts the clock. A failed send leaves the
        // user free to try again rather than locked out for a minute over
        // something that never reached the air.
        s_mqttSendLastMs = millis();
        if (s_mqttSendLastMs == 0) s_mqttSendLastMs = 1;
        const char *nm = channelName(chan);
        snprintf(msg, sizeof(msg), TR("Sent to %s"), (nm && nm[0]) ? nm : TR("channel"));
    } else {
        snprintf(msg, sizeof(msg), TR("Send failed"));
    }
    mqttMonNotice(msg);
    refreshMqttMonitorModal(true);
}

static void openMqttConfirmModal(int chan) {
    if (!s_rootScreen || chan < 0 || chan >= MESH_CHANNELS) return;
    if (s_mqttConfirmModal) closeMqttConfirmModal();
    s_mqttConfirmChan = chan;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 16;
    if (modalW > kChanModalMaxW) modalW = kChanModalMaxW;
    if (modalW < 120) modalW = w - 4;

    s_mqttConfirmBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_mqttConfirmBackdrop, w, h);
    lv_obj_align(s_mqttConfirmBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_mqttConfirmBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_mqttConfirmBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_mqttConfirmBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_mqttConfirmBackdrop, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_mqttConfirmBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_mqttConfirmBackdrop, 0, 0);
    // No dismiss-on-backdrop here, unlike the picker: this is the last gate
    // before a transmission, so leaving it wants a deliberate No.

    s_mqttConfirmModal = lv_obj_create(s_mqttConfirmBackdrop);
    lv_obj_set_size(s_mqttConfirmModal, modalW, LV_SIZE_CONTENT);
    lv_obj_align(s_mqttConfirmModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_mqttConfirmModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_mqttConfirmModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_mqttConfirmModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_mqttConfirmModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_mqttConfirmModal, 1, 0);
    lv_obj_set_style_border_color(s_mqttConfirmModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_mqttConfirmModal, kChanModalPad, 0);
    lv_obj_set_style_pad_row(s_mqttConfirmModal, kChanModalGap, 0);
    lv_obj_set_flex_flow(s_mqttConfirmModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_mqttConfirmModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_mqttConfirmBackdrop);

    lv_obj_t *title = lv_label_create(s_mqttConfirmModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, kChanModalTitleFont, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Send to channel?"));

    // The message itself, verbatim. Showing what will be transmitted — rather
    // than a description of it — is the whole point of a confirmation step.
    char preview[MESH_TEXT_MAX_LEN + 1];
    mqttMonBuildSendText(preview, sizeof(preview));

    const char *nm = channelName(chan);
    char line[64];
    snprintf(line, sizeof(line), TR("Channel %d  %s"), chan, (nm && nm[0]) ? nm : TR("(unnamed)"));
    lv_obj_t *chanLbl = lv_label_create(s_mqttConfirmModal);
    lv_obj_set_width(chanLbl, lv_pct(100));
    lv_obj_set_style_text_font(chanLbl, &lv_font_montserrat_10, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(chanLbl, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(chanLbl, lv_color_hex(0xFFC98A), 0);
#endif
    lv_obj_set_style_text_align(chanLbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(chanLbl, line);

    lv_obj_t *body = lv_label_create(s_mqttConfirmModal);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_style_text_font(body, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(body, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(body, preview);

#if UI_TOUCH_ONLY_PROFILE
    lv_obj_t *btnRow = lv_obj_create(s_mqttConfirmModal);
    lv_obj_set_width(btnRow, lv_pct(100));
    lv_obj_set_height(btnRow, LV_SIZE_CONTENT);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_style_pad_column(btnRow, 8, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    auto makeConfirmBtn = [](lv_obj_t *parent, const char *text, uint32_t border,
                             lv_event_cb_t cb) {
        lv_obj_t *btn = lv_btn_create(parent);
        lv_obj_set_size(btn, 84, 26);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x16386F), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_80, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(border), 0);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xE8F1FF), 0);
        lv_label_set_text(lbl, TR(text));
        lv_obj_center(lbl);
    };
    makeConfirmBtn(btnRow, TR_NOOP("Cancel"), 0x8FB5E6,
                   [](lv_event_t *e) { LV_UNUSED(e); closeMqttConfirmModal(); });
    makeConfirmBtn(btnRow, TR_NOOP("Send"), 0xFFC98A,
                   [](lv_event_t *e) { LV_UNUSED(e); mqttSendConfirmed(); });
#else
    lv_obj_t *hint = lv_label_create(s_mqttConfirmModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(hint, TR("Enter=Send   %s=Cancel"), modalCloseKeyLabel());
#endif
}

static void onMqttSendRowPressed(lv_event_t *e) {
    const int chan = (int)(intptr_t)lv_event_get_user_data(e);
    closeMqttSendModal();
    openMqttConfirmModal(chan);
}

static void onMqttSendBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_mqttSendBackdrop) return;
    closeMqttSendModal();
}

static void refreshMqttSendSelection() {
    paintPickerRows(s_mqttSendRows, MESH_CHANNELS, s_mqttSendSelection, PICKER_SCROLL_RECURSIVE);
}

static void openMqttSendModal() {
    if (!s_rootScreen || !s_mqttMonModal) return;
    if (s_mqttSendModal) closeMqttSendModal();

    // Both gates are checked before the picker opens, so a refusal costs one
    // keypress rather than a channel choice and a confirmation.
    const uint32_t waitS = mqttSendCooldownLeftS();
    if (waitS > 0) {
        char msg[48];
        snprintf(msg, sizeof(msg), TR("Wait %lus before sending again"), (unsigned long)waitS);
        mqttMonNotice(msg);
        refreshMqttMonitorModal(true);
        return;
    }
    if (mqttMonitorTopicCount() <= 0) {
        mqttMonNotice(TR("Nothing recorded yet - nothing to send"));
        refreshMqttMonitorModal(true);
        return;
    }

    if (s_mqttSendSelection < 0 || s_mqttSendSelection >= MESH_CHANNELS) s_mqttSendSelection = 0;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 16;
    if (modalW > kChanModalMaxW) modalW = kChanModalMaxW;
    if (modalW < 120) modalW = w - 4;

    s_mqttSendBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_mqttSendBackdrop, w, h);
    lv_obj_align(s_mqttSendBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_mqttSendBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_mqttSendBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_mqttSendBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_mqttSendBackdrop, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_mqttSendBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_mqttSendBackdrop, 0, 0);
    lv_obj_add_event_cb(s_mqttSendBackdrop, onMqttSendBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_mqttSendModal = lv_obj_create(s_mqttSendBackdrop);
    lv_obj_set_size(s_mqttSendModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_mqttSendModal,
                                (h > 40) ? (h - 2 * kChanModalPad) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_mqttSendModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_mqttSendModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_mqttSendModal, LV_DIR_VER);
    lv_obj_add_flag(s_mqttSendModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_mqttSendModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_mqttSendModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_mqttSendModal, 1, 0);
    lv_obj_set_style_border_color(s_mqttSendModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_mqttSendModal, kChanModalPad, 0);
    lv_obj_set_style_pad_row(s_mqttSendModal, kChanModalGap, 0);
    lv_obj_set_flex_flow(s_mqttSendModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_mqttSendModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_mqttSendBackdrop);

    lv_obj_t *title = lv_label_create(s_mqttSendModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, kChanModalTitleFont, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Send top 5 to"));

    lv_obj_t *hint = lv_label_create(s_mqttSendModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
#if UI_TOUCH_ONLY_PROFILE
    lv_label_set_text(hint, TR("Tap a channel"));
#else
    lv_label_set_text_fmt(hint, TR("Move  Enter=Pick  %s=Back"), modalCloseKeyLabel());
#endif

    const lv_color_t rowTextColor = (s_cfg.uiMode == UI_MODE_LIGHT)
                                        ? lv_color_hex(0x13233D) : lv_color_hex(0xD9E8FF);

    lv_obj_t *grid = lv_obj_create(s_mqttSendModal);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, kChanModalGap, 0);
    lv_obj_set_style_pad_column(grid, kChanModalGap, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);

    for (int pos = 0; pos < MESH_CHANNELS; pos++) {
        // Column-major, so the slots read 0-3 down the left and 4-7 down the
        // right — the same order every other channel grid uses.
        const int i = (pos % kChanModalCols) * kChanModalRowsPerCol + (pos / kChanModalCols);
        if (i >= MESH_CHANNELS) continue;

        lv_obj_t *row = lv_btn_create(grid);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_remove_style_all(row);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    #endif
        s_mqttSendRows[i] = row;
        lv_obj_set_width(row, lv_pct(kChanModalCellPct));
        lv_obj_set_height(row, kChanModalRowH);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_left(row, 5, 0);
        lv_obj_set_style_pad_right(row, 5, 0);
        lv_obj_set_style_pad_top(row, 1, 0);
        lv_obj_set_style_pad_bottom(row, 1, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_add_event_cb(row, onMqttSendRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(row);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_obj_set_style_text_font(lbl, kChanModalRowFont, 0);
        lv_obj_set_style_text_color(lbl, rowTextColor, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        char slotText[28];
        chanCfgRowLabel(i, slotText, sizeof(slotText));
        lv_label_set_text(lbl, slotText);
        lv_obj_center(lbl);
    }

    lv_obj_update_layout(s_mqttSendModal);
    refreshMqttSendSelection();
}

#endif  // FEATURE_MQTT_MONITOR

// ── Discovery modal ──────────────────────────────────────────────────────────
#if FEATURE_DISCOVERY

static void closeDiscoveryModal() {
    closeDiscoveryPresetModal();
    lvObjDeleteSafe(s_discoveryModal);
    s_discoveryStatusLabel = nullptr;
    s_discoveryWardriveLabel = nullptr;
    s_discoveryList = nullptr;
    s_discoveryClearLabel = nullptr;
    s_discoveryHintLabel = nullptr;
    memset(s_discoveryColBoxes, 0, sizeof(s_discoveryColBoxes));
    s_discoveryRenderedSig = 0;
    // s_discoverySweepStartedMs is deliberately left alone: a sweep already on
    // the air keeps running, and serviceDiscoverySweep() still closes it out.
    //
    // A preset scan is the exception, and the difference is that it is not one
    // packet already gone — it is the radio parked somewhere the node does not
    // live, deaf to its own mesh for as long as it stays there. Leaving the
    // screen has to bring it home, so the scan is abandoned here rather than
    // left to run its window out with nobody watching.
    //
    // prepareGlobalNavigation() calls this, so every Alt+letter shortcut and
    // every M9 hardware button routes through the restore for free.
    discoveryEndPresetScan(/*aborted=*/true);   // no-op unless one is running
}

// Clear watermark: Discovery ignores anything it has not heard since this
// moment. 0 = never cleared.
//
// A watermark rather than wiping the node table, because the two overlap but
// are not the same thing. Discovery reads live state that the Nodes screen, the
// recency sort and the NeighborInfo announce all read too — clearing "this
// screen" must not throw away when we last heard from someone. Everything here
// repopulates from the next packet each node sends, which is what makes the
// button useful: it answers "who is out there *now*", not "who has this device
// ever met".
static uint32_t s_discoveryClearedMs = 0;

// Nodes we have actually received a packet from — as opposed to ones we only
// know about because a neighbor listed them — and heard from since the last
// clear. Signed difference so the comparison survives the millis() wrap.
static inline bool discoveryHeardFrom(const NodeEntry &e) {
    if (e.nodeId == 0 || e.lastHeardMs == 0) return false;
    // Showing a preset scan: only what the radio itself heard while it was
    // parked there counts. Every group in this modal funnels through here, so
    // one gate scopes DIRECT, the hop buckets and the no-distance bucket alike.
    if (s_presetScanResultFromMs != 0) {
        if (e.lastHeardViaMqtt) return false;
        if ((int32_t)(e.lastHeardMs - s_presetScanResultFromMs) < 0) return false;
        // And not after the radio came home: the result stays on screen once the
        // scan ends, and without this the node table's own preset refills it.
        if (s_presetScanResultToMs != 0 &&
            (int32_t)(e.lastHeardMs - s_presetScanResultToMs) > 0) return false;
        return true;
    }
    // Kept out of the radio groups: it has its own below. A node last seen over
    // the bridge has no SNR of ours and no hop count it earned on our air, so
    // listing it under DIRECT or "2 HOPS" states something we did not measure.
    // hasHops in particular survives from an earlier RF sighting, which is how
    // a bridged node could appear as a direct neighbour.
    if (e.lastHeardViaMqtt && s_cfg.mqttEnabled) return false;
    if (s_discoveryClearedMs == 0) return true;
    return (int32_t)(e.lastHeardMs - s_discoveryClearedMs) >= 0;
}

// Defined with the preset-scan block far below; the summary line and the group
// builders above it are the callers that come first.
static const char *discoveryPresetLabel(uint8_t preset);

// Leaves the preset-scan view. Called wherever the screen goes back to asking
// about the node table at large, so a scan result cannot outlive the question
// it answered.
static void discoveryClearPresetScanView() {
    s_presetScanResultFromMs = 0;
    s_presetScanResultToMs = 0;
    s_presetScanResultPreset = 0;
}

// Nodes whose most recent packet came from the broker rather than the air.
// Only meaningful with the bridge on, and never while a preset scan's result is
// what is being shown -- MQTT is not on the scanned preset by definition.
static inline bool discoveryViaMqtt(const NodeEntry &e) {
    if (!s_cfg.mqttEnabled) return false;
    if (s_presetScanResultFromMs != 0) return false;
    if (e.nodeId == 0 || e.lastHeardMs == 0) return false;
    if (!e.lastHeardViaMqtt) return false;
    if (s_discoveryClearedMs == 0) return true;
    return (int32_t)(e.lastHeardMs - s_discoveryClearedMs) >= 0;
}

// A neighbor report this screen is willing to draw. The same two rules the node
// groups follow, asked of the report's own arrival rather than of the node it
// names — HEARD ABOUT is built entirely out of these, and it was the one group
// with no transport gate at all on it. A NeighborInfo that arrives over the
// bridge lists a bridged mesh's links, so while a preset scan is on screen it
// was filling the scan's own result with nodes from the preset the radio just
// left: the exact "MQTT traffic on LongFast while scanning MediumSlow" the
// node groups had already been taught to refuse.
static inline bool discoveryReportUsable(const NeighborReport &r) {
    if (r.nodeId == 0) return false;
    if (s_presetScanResultFromMs != 0) {
        if (r.viaMqtt) return false;
        if ((int32_t)(r.updatedMs - s_presetScanResultFromMs) < 0) return false;
        if (s_presetScanResultToMs != 0 &&
            (int32_t)(r.updatedMs - s_presetScanResultToMs) > 0) return false;
        return true;
    }
    // Outside a scan, the same line discoveryHeardFrom() draws for nodes: with
    // the bridge on, what came off the broker is not what the radio can reach.
    return !(r.viaMqtt && s_cfg.mqttEnabled);
}

// Live reports Discovery would actually draw, which is what its counters have to
// say — Nodes.neighborReportCount() counts the table, filters and all.
static int discoveryUsableReportCount() {
    int n = 0;
    for (int r = 0; r < MAX_NEIGHBOR_REPORTS; r++) {
        const NeighborReport *rep = Nodes.neighborReportAt(r);
        if (rep && discoveryReportUsable(*rep)) n++;
    }
    return n;
}

// Direct as Discovery counts it: one hop away, and heard since the last clear.
// nodeIsDirectNeighbor() stays watermark-free for the NeighborInfo announce,
// which advertises real neighbors regardless of what this screen is showing.
static inline bool discoveryIsDirect(const NodeEntry &e) {
    return discoveryHeardFrom(e) && e.hasHops && e.hops == 0;
}

// A group is a header label plus one body label holding its rows, because LVGL
// cannot mix font sizes inside a single label. Rows accumulate into one shared
// scratch buffer — the labels copy their text, so the buffer is free again the
// moment a section is closed.
static char s_discoverySectionText[720];

static lv_obj_t *discoveryMakeLabel(lv_obj_t *parent, const char *text, bool header) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_width(lbl, lv_pct(100));
    lv_obj_set_style_text_font(lbl, header ? kDiscoveryHeaderFont : kDiscoveryBodyFont, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(lbl, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(lbl,
                                header ? lv_color_hex(kDiscoveryHeaderColor)
                                       : lv_color_hex(kDiscoveryBodyColor), 0);
#endif
    // A little air above a heading, none above the rows it introduces, so the
    // groups read as blocks rather than as one continuous list.
    lv_obj_set_style_pad_top(lbl, header ? 4 : 0, 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_label_set_text(lbl, text);
    return lbl;
}

// One group under construction. The header label is created on the first row
// rather than up front, which is what lets an empty group decide for itself
// whether it is worth drawing — and still keeps the header above its rows,
// since the body label is only added when the group is finished.
struct DiscoverySection {
    lv_obj_t   *col;
    const char *header;
    int         rows;
};

static void discoverySectionRow(DiscoverySection &s, const char *fmt, ...) {
    if (s.rows == 0) {
        discoveryMakeLabel(s.col, s.header, true);
        s_discoverySectionText[0] = '\0';
    }
    s.rows++;

    size_t used = strlen(s_discoverySectionText);
    const size_t cap = sizeof(s_discoverySectionText);
    if (used + 1 >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(s_discoverySectionText + used, cap - used, fmt, ap);
    va_end(ap);
    // Roll back a truncated row: half a row reads as corrupt data, where a list
    // that simply stops reads as a list that ran out of room.
    if (n < 0 || (size_t)n >= cap - used) s_discoverySectionText[used] = '\0';
}

// emptyText draws when nothing was added; pass null to leave the group out
// entirely, which is what the per-hop buckets want.
static void discoverySectionFinish(DiscoverySection &s, const char *emptyText) {
    if (s.rows > 0) {
        // Drop the last row's newline: kept, it draws an empty line under every
        // group, which the header's own top padding already provides.
        size_t len = strlen(s_discoverySectionText);
        if (len > 0 && s_discoverySectionText[len - 1] == '\n') {
            s_discoverySectionText[len - 1] = '\0';
        }
        discoveryMakeLabel(s.col, s_discoverySectionText, false);
        return;
    }
    if (!emptyText) return;
    discoveryMakeLabel(s.col, s.header, true);
    discoveryMakeLabel(s.col, emptyText, false);
}

// Long name when we have one, short name otherwise, hex as the last resort.
// Truncated to what a column can actually hold: a 40-character name would wrap
// every row on a three-column Pager, and a wrapped list is harder to scan than
// a clipped one.
// Takes the entry rather than the id: every caller already has one in hand, and
// Nodes.find() is a linear scan — looking it up again once per row would make
// drawing the list quadratic in the size of the node table.
static void discoveryNodeLabel(const NodeEntry &e, char *out, size_t outLen) {
#if defined(DEVICE_TDISPLAY_P4)
    // The multi-column length every other board sets through kDiscoveryNameMax.
    if (kDiscoveryCols > 1 && outLen > 19) outLen = 19;
#endif
    if (e.hasName && e.longName[0]) {
        utf8util::copyTruncate(out, outLen, e.longName);
        return;
    }
    liveNodeLabel(e.nodeId, out, outLen, true);
}

// Direct neighbors, with the SNR we measured ourselves — the one number here
// that is our own reading, so it keeps its decimal.
static void discoveryBuildDirect(lv_obj_t *col) {
    DiscoverySection s = { col, "DIRECT", 0 };
    const int total = Nodes.count();
    char label[kDiscoveryNameMax];
    for (int i = 0; i < total; i++) {
        const NodeEntry *e = Nodes.at(i);
        if (!e || e->nodeId == 0 || e->nodeId == s_myNodeId) continue;
        if (!discoveryIsDirect(*e)) continue;
        discoveryNodeLabel(*e, label, sizeof(label));
        discoverySectionRow(s, "  %s  %.1f dB\n", label, e->snr);
    }
    discoverySectionFinish(s, "  (none)");
}

// Everything else we have heard, bucketed by distance, then the ones whose
// packets never said. hop_limit is 3 bits on the wire, so 7 is the ceiling.
static void discoveryBuildDistance(lv_obj_t *col) {
    const int total = Nodes.count();
    char label[kDiscoveryNameMax];
    char header[12];
    int listed = 0;

    for (int hop = 1; hop <= 7; hop++) {
        snprintf(header, sizeof(header), "%d HOP%s", hop, hop == 1 ? "" : "S");
        DiscoverySection s = { col, header, 0 };
        for (int i = 0; i < total; i++) {
            const NodeEntry *e = Nodes.at(i);
            if (!e || e->nodeId == 0 || e->nodeId == s_myNodeId) continue;
            if (!discoveryHeardFrom(*e) || !e->hasHops || e->hops != hop) continue;
            discoveryNodeLabel(*e, label, sizeof(label));
            discoverySectionRow(s, "  %s\n", label);
            listed++;
        }
        // No empty text: an unused hop distance is not a fact worth a heading.
        discoverySectionFinish(s, nullptr);
    }

    DiscoverySection unknown = { col, TR("DISTANCE UNKNOWN"), 0 };
    for (int i = 0; i < total; i++) {
        const NodeEntry *e = Nodes.at(i);
        if (!e || e->nodeId == 0 || e->nodeId == s_myNodeId) continue;
        if (!discoveryHeardFrom(*e) || e->hasHops) continue;
        discoveryNodeLabel(*e, label, sizeof(label));
        discoverySectionRow(unknown, "  %s\n", label);
        listed++;
    }
    discoverySectionFinish(unknown, nullptr);

    if (listed == 0) {
        DiscoverySection none = { col, "MULTI-HOP", 0 };
        discoverySectionFinish(none, "  (none)");
    }
}

// The group nothing else in the UI can show: nodes that exist in a neighbor's
// report and have never sent us anything. Deduplicated against a small seen
// list — a node named by three reporters is still one node.
static void discoveryBuildHeardAbout(lv_obj_t *col) {
    DiscoverySection s = { col, TR("HEARD ABOUT"), 0 };
    constexpr int kMaxHeardAbout = 24;
    uint32_t seen[kMaxHeardAbout];
    int seenCount = 0;
    char label[kDiscoveryNameMax];
    char via[20];

    for (int r = 0; r < MAX_NEIGHBOR_REPORTS; r++) {
        const NeighborReport *rep = Nodes.neighborReportAt(r);
        if (!rep || !discoveryReportUsable(*rep)) continue;
        for (int j = 0; j < rep->count; j++) {
            const uint32_t id = rep->ids[j];
            if (id == 0 || id == s_myNodeId) continue;
            const NodeEntry *known = Nodes.find(id);
            if (known && discoveryHeardFrom(*known)) continue;   // already grouped above
            bool dup = false;
            for (int k = 0; k < seenCount; k++) {
                if (seen[k] == id) { dup = true; break; }
            }
            if (dup) continue;
            if (seenCount < kMaxHeardAbout) seen[seenCount++] = id;

            // known is null for a node only ever named by someone else, which
            // is the usual case in this group — it falls back to the hex id.
            if (known) discoveryNodeLabel(*known, label, sizeof(label));
            else       liveNodeLabel(id, label, sizeof(label), true);
            // The reporter stays a short name: it is the secondary half of the
            // row, and two long names would wrap every line in this group.
            liveNodeLabel(rep->nodeId, via, sizeof(via), true);
            // Whole dB, and no column padding: this group gets a third of the
            // Pager and half a T-Deck, and a wrapped row costs more than the
            // quarter-dB does. It is the reporter's reading of that link, not
            // ours, so the precision was never worth much here anyway.
            discoverySectionRow(s, TR("  %s via %s (%.0f dB)\n"),
                                label, via, (float)rep->snrQ4[j] / 4.0f);
        }
    }
    discoverySectionFinish(s, "  (none)");
}

// Nodes the broker told us about. A separate group rather than a marker on the
// existing rows: these are reachable over the internet, not over the radio, and
// the whole question this screen answers is what the radio can get to.
static void discoveryBuildViaMqtt(lv_obj_t *col) {
    if (!s_cfg.mqttEnabled) return;       // nothing bridged, nothing to say
    if (s_presetScanResultFromMs != 0) return;   // not on the scanned preset
    DiscoverySection s = { col, TR("VIA MQTT"), 0 };
    const int total = Nodes.count();
    char label[kDiscoveryNameMax];
    for (int i = 0; i < total; i++) {
        const NodeEntry *e = Nodes.at(i);
        if (!e || e->nodeId == 0 || e->nodeId == s_myNodeId) continue;
        if (!discoveryViaMqtt(*e)) continue;
        discoveryNodeLabel(*e, label, sizeof(label));
        discoverySectionRow(s, "  %s\n", label);
    }
    discoverySectionFinish(s, "  (none)");
}

// Rebuilds every column's contents. The summary rides on the DIRECT column,
// which is column 0 on every layout.
static void discoveryWardriveFormat(char *out, size_t outLen) {
    char gps[32];
    if (!gpsIsEnabled()) {
        snprintf(gps, sizeof(gps), "%s", TR("GPS off"));
    } else if (!gpsHasFix()) {
        snprintf(gps, sizeof(gps), "%s", TR("No GPS fix"));
    } else {
#if defined(DEVICE_TDECK_PRO)
        // E-paper: the satellite count jitters every second or two, and each
        // change would be a panel refresh. Fix state alone changes rarely.
        snprintf(gps, sizeof(gps), "%s", TR("GPS fix"));
#else
        snprintf(gps, sizeof(gps), TR("GPS %u sat"), (unsigned)gpsSats());
#endif
    }
    if (wardriveLogIsFull()) {
        snprintf(out, outLen, "%s | %s", gps, TR("Log full"));
    } else {
        char log[48];
#if defined(DEVICE_TDECK_PRO)
        // Same reason: nodes grow slowly, lines grow with every sighting.
        snprintf(log, sizeof(log), TR("Log: %lu node(s)"),
                 (unsigned long)wardriveLogNodes());
#else
        snprintf(log, sizeof(log), TR("Log %lu/%lu"),
                 (unsigned long)wardriveLogNodes(), (unsigned long)wardriveLogLines());
#endif
        snprintf(out, outLen, "%s | %s", gps, log);
    }
}

static void discoveryBuildColumns() {
    for (int i = 0; i < kDiscoveryCols; i++) {
        if (s_discoveryColBoxes[i]) lv_obj_clean(s_discoveryColBoxes[i]);
    }
    s_discoveryWardriveLabel = nullptr;   // went with the clean above
    if (!s_discoveryColBoxes[kDiscoveryColDirect]) return;

    // Counts sit here rather than on the status line, which belongs to the
    // sweep: a refusal message must not cost the user the summary.
    char summary[64];
    if (s_presetScanResultFromMs != 0) {
        // Nodes.count() is the whole table, which is exactly what this line must
        // not say while the screen is scoped to one scan.
        int onPreset = 0;
        const int total = Nodes.count();
        for (int i = 0; i < total; i++) {
            const NodeEntry *e = Nodes.at(i);
            if (!e || e->nodeId == 0 || e->nodeId == s_myNodeId) continue;
            if (discoveryHeardFrom(*e)) onPreset++;
        }
        snprintf(summary, sizeof(summary), TR("%s: %d node(s) heard"),
                 discoveryPresetLabel(s_presetScanResultPreset), onPreset);
    } else {
        snprintf(summary, sizeof(summary), TR("%d node(s), %d report(s)"),
                 Nodes.count(), discoveryUsableReportCount());
    }
    discoveryMakeLabel(s_discoveryColBoxes[kDiscoveryColDirect], summary, false);
    // Wardriving at a glance, on its own line so it never pushes the summary
    // into a wrap: whether sightings are being positioned right now, and how
    // much the log has taken. A drive with no fix is the failure that otherwise
    // only shows up at upload time. Nothing at all unless the log is on.
    if (wardriveLogIsEnabled()) {
        discoveryWardriveFormat(s_discoveryWardriveText, sizeof(s_discoveryWardriveText));
        s_discoveryWardriveLabel = discoveryMakeLabel(s_discoveryColBoxes[kDiscoveryColDirect],
                                                      s_discoveryWardriveText, false);
    }

    discoveryBuildDirect(s_discoveryColBoxes[kDiscoveryColDirect]);
    discoveryBuildDistance(s_discoveryColBoxes[kDiscoveryColDistance]);
    discoveryBuildHeardAbout(s_discoveryColBoxes[kDiscoveryColHeard]);
    discoveryBuildViaMqtt(s_discoveryColBoxes[kDiscoveryColHeard]);
}

static void discoverySetStatus(const char *text) {
    snprintf(s_discoveryStatus, sizeof(s_discoveryStatus), "%s", text ? text : "");
    if (s_discoveryStatusLabel) {
        lv_label_set_text(s_discoveryStatusLabel, s_discoveryStatus);
    }
}

#if HAS_FILE_STORAGE
// ── Saving a snapshot ────────────────────────────────────────────────────────
// Anywhere a file can be written, which is what HAS_FILE_STORAGE answers —
// storage.h says to prefer it over HAS_SD_CARD for exactly this question, and
// everything below already goes through storageFs()/storageName() rather than
// naming a backend.
//
// This used to be SD-only, on the grounds that snapshots accumulate one per
// press and "a small internal partition is the wrong place to let that happen
// unattended". The premise does not hold: the internal-flash boards carry a
// 3.3 MB littlefs partition (partitions_16mb_fs.csv), which is thousands of
// snapshots, and the restriction was costing more than it saved — see the Save
// button in openDiscoveryModal() for the board it locked out of its own SD card.

// Minimal JSON string escaping. Quote and backslash get escaped; anything below
// 0x20 becomes a space rather than \uXXXX, which keeps the worst case at 2x the
// input and lets the caller size a buffer without guessing. UTF-8 above 0x7F
// passes through untouched — JSON strings are UTF-8 already.
static void discoveryJsonEscape(const char *in, char *out, size_t outLen) {
    if (!out || outLen == 0) return;
    size_t o = 0;
    for (size_t i = 0; in && in[i] && o + 2 < outLen; i++) {
        const unsigned char c = (unsigned char)in[i];
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c < 0x20) {
            out[o++] = ' ';
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
}

// One node, as a JSON object. `extra` appends already-formatted fields. The
// separator is written before the element rather than after it, so no caller
// has to know which element is last.
static void discoveryJsonNode(File &f, bool &first, const NodeEntry *e,
                              uint32_t nodeId, const char *extra) {
    char shortEsc[24] = {};
    char longEsc[96]  = {};
    if (e) {
        discoveryJsonEscape(e->shortName, shortEsc, sizeof(shortEsc));
        discoveryJsonEscape(e->longName, longEsc, sizeof(longEsc));
    }
    if (!first) f.print(",\n");
    first = false;
    f.printf("    {\"id\": \"!%08lX\", \"short\": \"%s\", \"long\": \"%s\"%s}",
             (unsigned long)nodeId, shortEsc, longEsc, extra ? extra : "");
}

// Picks a fresh /camillia/discovery-<stamp>.json that does not exist yet. Split
// from the write so a run that saves as it goes can pick its file once and keep
// rewriting it. Fills msg on failure.
static bool discoveryPickSavePath(char *path, size_t pathLen, char *msg, size_t msgLen) {
    if (!storageBegin()) {
        snprintf(msg, msgLen, TR("No %s - not saved"), storageName());
        return false;
    }
    storageFs().mkdir("/camillia");

    // Wall clock when we have one; uptime otherwise. Either way the name has to
    // be unique, so a same-second second press does not silently overwrite the
    // first — hence the suffix probe below.
    char stamp[32];
    const time_t nowEpoch = time(nullptr);
    if (nowEpoch >= 1700000000) {
        struct tm lt;
        localtime_r(&nowEpoch, &lt);
        strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &lt);
    } else {
        snprintf(stamp, sizeof(stamp), "boot-%lus", (unsigned long)(millis() / 1000UL));
    }

    snprintf(path, pathLen, "/camillia/discovery-%s.json", stamp);
    for (int attempt = 2; attempt <= 9 && storageFs().exists(path); attempt++) {
        snprintf(path, pathLen, "/camillia/discovery-%s-%d.json", stamp, attempt);
    }
    if (storageFs().exists(path)) {
        snprintf(msg, msgLen, TR("Save failed - too many this second"));
        return false;
    }
    return true;
}

// Writes the current picture to `path`, replacing whatever is there. Fills msg
// with what to put on the status line either way.
static bool discoveryWriteJson(const char *path, char *msg, size_t msgLen) {
    if (!storageBegin()) {
        snprintf(msg, msgLen, TR("No %s - not saved"), storageName());
        return false;
    }
    const time_t nowEpoch = time(nullptr);

    File f = storageFs().open(path, FILE_WRITE);
    if (!f) {
        snprintf(msg, msgLen, TR("Save failed - cannot write %s"), storageName());
        return false;
    }

    const int total = Nodes.count();
    char extra[48];

    f.print("{\n");
    f.printf("  \"node\": \"!%08lX\",\n", (unsigned long)s_myNodeId);
    if (nowEpoch >= 1700000000) {
        char iso[32];
        struct tm lt;
        localtime_r(&nowEpoch, &lt);
        strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%S", &lt);
        f.printf("  \"generated\": \"%s\",\n", iso);
    } else {
        // Explicitly null rather than a fabricated date: the clock was not set,
        // and a snapshot that lies about when it was taken is worse than one
        // that admits it does not know.
        f.print("  \"generated\": null,\n");
    }
    f.printf("  \"uptimeMs\": %lu,\n", (unsigned long)millis());
    f.printf("  \"counts\": {\"nodes\": %d, \"reports\": %d},\n",
             total, discoveryUsableReportCount());

    // direct
    bool first = true;
    f.print("  \"direct\": [\n");
    for (int i = 0; i < total; i++) {
        const NodeEntry *e = Nodes.at(i);
        if (!e || e->nodeId == 0 || e->nodeId == s_myNodeId) continue;
        if (!discoveryIsDirect(*e)) continue;
        snprintf(extra, sizeof(extra), ", \"snr\": %.2f", e->snr);
        discoveryJsonNode(f, first, e, e->nodeId, extra);
    }
    f.print("\n  ],\n");

    // by hop count
    first = true;
    f.print("  \"hops\": [\n");
    for (int hop = 1; hop <= 7; hop++) {
        for (int i = 0; i < total; i++) {
            const NodeEntry *e = Nodes.at(i);
            if (!e || e->nodeId == 0 || e->nodeId == s_myNodeId) continue;
            if (!discoveryHeardFrom(*e) || !e->hasHops || e->hops != hop) continue;
            snprintf(extra, sizeof(extra), ", \"hops\": %d, \"snr\": %.2f", hop, e->snr);
            discoveryJsonNode(f, first, e, e->nodeId, extra);
        }
    }
    f.print("\n  ],\n");

    // heard from, distance unknown
    first = true;
    f.print("  \"distanceUnknown\": [\n");
    for (int i = 0; i < total; i++) {
        const NodeEntry *e = Nodes.at(i);
        if (!e || e->nodeId == 0 || e->nodeId == s_myNodeId) continue;
        if (!discoveryHeardFrom(*e) || e->hasHops) continue;
        snprintf(extra, sizeof(extra), ", \"snr\": %.2f", e->snr);
        discoveryJsonNode(f, first, e, e->nodeId, extra);
    }
    f.print("\n  ],\n");

    // heard about only, with who reported them
    first = true;
    f.print("  \"heardAbout\": [\n");
    for (int r = 0; r < MAX_NEIGHBOR_REPORTS; r++) {
        const NeighborReport *rep = Nodes.neighborReportAt(r);
        if (!rep || !discoveryReportUsable(*rep)) continue;
        for (int j = 0; j < rep->count; j++) {
            const uint32_t id = rep->ids[j];
            if (id == 0 || id == s_myNodeId) continue;
            const NodeEntry *known = Nodes.find(id);
            if (known && discoveryHeardFrom(*known)) continue;
            snprintf(extra, sizeof(extra), ", \"via\": \"!%08lX\", \"snr\": %.2f",
                     (unsigned long)rep->nodeId, (float)rep->snrQ4[j] / 4.0f);
            discoveryJsonNode(f, first, known, id, extra);
        }
    }
    f.print("\n  ],\n");

    // The raw reports, so the graph can be rebuilt from the file rather than
    // only the grouping this screen happened to draw.
    first = true;
    f.print("  \"reports\": [\n");
    for (int r = 0; r < MAX_NEIGHBOR_REPORTS; r++) {
        const NeighborReport *rep = Nodes.neighborReportAt(r);
        if (!rep) continue;
        if (!first) f.print(",\n");
        first = false;
        f.printf("    {\"id\": \"!%08lX\", \"intervalS\": %lu, \"ageMs\": %lu, "
                 "\"viaMqtt\": %s, \"neighbors\": [",
                 (unsigned long)rep->nodeId,
                 (unsigned long)rep->intervalS,
                 (unsigned long)(millis() - rep->updatedMs),
                 rep->viaMqtt ? "true" : "false");
        for (int j = 0; j < rep->count; j++) {
            f.printf("%s{\"id\": \"!%08lX\", \"snr\": %.2f}",
                     (j == 0) ? "" : ", ",
                     (unsigned long)rep->ids[j],
                     (float)rep->snrQ4[j] / 4.0f);
        }
        f.print("]}");
    }
    f.print("\n  ]\n}\n");

    // position(), not size(): the write handle knows exactly how much went out,
    // where size() can lag until the card is flushed.
    const size_t bytes = (size_t)f.position();
    f.close();

    Serial.printf("[discovery] saved %s (%u bytes)\n", path, (unsigned)bytes);
    // Name only: the full path does not fit the status line, and it is always
    // the same directory.
    const char *name = strrchr(path, '/');
    snprintf(msg, msgLen, TR("Saved %s"), name ? name + 1 : path);
    return true;
}

// The Save button: a new file each press, so an earlier snapshot is never lost.
static bool discoverySaveJson(char *msg, size_t msgLen) {
    char path[64];
    if (!discoveryPickSavePath(path, sizeof(path), msg, msgLen)) return false;
    return discoveryWriteJson(path, msg, msgLen);
}
#endif  // HAS_FILE_STORAGE

// Empties the screen — every group, not just the one Discovery stores itself.
//
// The neighbor reports are dropped outright, because Discovery owns them. The
// other groups are a live read of the node table, which Discovery does not own
// and must not damage: the Nodes screen, the recency sort and the NeighborInfo
// announce all read the same records. So rather than deleting anything there,
// the clear drops a watermark and Discovery stops counting whatever it has not
// heard since. The screen goes blank and refills from live traffic, which is
// the question it is really being asked — who is out there *now*.
//
// Nothing here is destructive: nodes reappear as they transmit, and reports as
// each node makes its next NeighborInfo broadcast.
//
// `keepScanView` is the preset scan's case and the reason this takes a flag at
// all: a scan clears the screen as part of *starting*, and the scoping it set a
// few lines earlier has to survive its own wipe. Without it the clear reset
// s_presetScanResultFromMs to 0 the instant the scan began — so the screen spent
// the whole five minutes back on the unscoped view, drawing the VIA MQTT group
// and everything else the broker delivered while the radio sat on somebody
// else's preset. That is the bug: not the scan filter failing, the scan filter
// being switched off immediately after being switched on.
static void discoveryClear(bool keepScanView = false) {
    const int droppedReports = Nodes.clearNeighbors();
    // Beacons are not touched here: they live on the Beacons tool now, and they
    // arrive far too rarely to be worth throwing away with a screen that is
    // deliberately about the last few minutes.
    if (!keepScanView) discoveryClearPresetScanView();   // a Clear is a new question
    s_discoveryClearedMs = millis();
    if (s_discoveryClearedMs == 0) s_discoveryClearedMs = 1;   // 0 means "never cleared"

    char msg[72];
    snprintf(msg, sizeof(msg), TR("Cleared (%d report(s)) - rebuilding from traffic"),
             droppedReports);
    discoverySetStatus(msg);
}

// One NODEINFO_APP broadcast with want_response, and never more than that.
// Every guard here maps to a specific Meshtastic rule; each refusal says why,
// because a Sweep button that silently does nothing is indistinguishable from
// a broken one.
//
// Shared with the preset scan, which sends the identical broadcast and so owes
// the mesh the identical restraint — the only difference is which preset hears
// it. Returns false with the reason in msg.
static bool discoverySweepAllowed(char *msg, size_t msgLen) {
    if (s_discoverySweepStartedMs != 0) {
        snprintf(msg, msgLen, TR("Sweep already running"));
        return false;
    }
    if (!Radio.isReady() || s_myNodeId == 0) {
        snprintf(msg, msgLen, TR("Radio not ready"));
        return false;
    }
    // Two separate rules, and the refusal names whichever is actually binding
    // so the countdown it prints is the one being waited on. From the last
    // sweep's start: the upstream one-per-minute floor. From its end: the quiet
    // gap that stops a sweep being re-fired the instant the last one lands. For
    // a 60 s sweep they coincide at 75 s from the start; after a five-minute
    // preset scan only the second one is left to do any work.
    uint32_t waitMs = 0;
    if (s_discoveryLastSweepMs != 0) {
        const uint32_t since = millis() - s_discoveryLastSweepMs;
        if (since < kDiscoverySweepMinGapMs) waitMs = kDiscoverySweepMinGapMs - since;
    }
    if (s_discoverySweepEndedMs != 0) {
        const uint32_t since = millis() - s_discoverySweepEndedMs;
        if (since < kDiscoverySweepCooldownMs) {
            const uint32_t left = kDiscoverySweepCooldownMs - since;
            if (left > waitMs) waitMs = left;
        }
    }
    if (waitMs > 0) {
        snprintf(msg, msgLen, TR("Wait %lus between sweeps"),
                 (unsigned long)((waitMs + 999UL) / 1000UL));
        return false;
    }
    // Measured on the preset we are on, which for a preset scan is not the one
    // the sweep will land on. It still bounds the thing it exists to bound —
    // how often *this* node asks a mesh to answer — so it is applied either way.
    const float chUtil = Radio.channelUtilPercent();
    if (chUtil >= kDiscoverySweepMaxChUtil) {
        snprintf(msg, msgLen, TR("Channel busy (%.0f%%) - not sweeping"), chUtil);
        return false;
    }

    // Every NODEINFO broadcast shares one 15 s window, and the periodic announce
    // opens it on the first loop pass after boot — so the very first sweep after
    // powering on lands inside it. That is a wait, not a failure, and saying
    // "send failed" for it sends people looking for a radio problem that is not
    // there. Checked before sending so the message can name the remaining time.
    const uint32_t cooldownMs = Channels.nodeInfoBroadcastCooldownMs();
    if (cooldownMs > 0) {
        snprintf(msg, msgLen, TR("Radio busy - retry in %lus"),
                 (unsigned long)((cooldownMs + 999UL) / 1000UL));
        return false;
    }
    return true;
}

static void discoveryStartSweep() {
    char why[72];
    if (!discoverySweepAllowed(why, sizeof(why))) {
        discoverySetStatus(why);
        return;
    }
    // Asking about our own preset again, so the scan-scoped view has to go --
    // otherwise the sweep would fill a list still filtered to someone else's.
    discoveryClearPresetScanView();

    const bool ok = Channels.sendDiscoverySweep(s_myNodeId,
                                                s_cfg.nodeLong,
                                                s_cfg.nodeShort,
                                                kDiscoverySweepHopLimit);
    if (!ok) {
        discoverySetStatus(TR("Sweep send failed"));
        return;
    }

    // Sampled only after the send, never before the guards — same reason
    // serviceTracerouteTimeout() takes no `now`: a start time even a few
    // milliseconds in the future wraps the unsigned subtraction below to ~49
    // days and expires the sweep the instant it starts.
    uint32_t now = millis();
    if (now == 0) now = 1;   // 0 is the "no sweep in flight" sentinel
    s_discoverySweepStartedMs = now;
    s_discoveryLastSweepMs = now;
    s_discoverySweepBaseNodes = Nodes.count();
    discoverySetStatus(TR("Sweeping..."));
}

// ── Preset scan ──────────────────────────────────────────────────────────────

// Human label for a preset index, for status lines and picker rows.
static const char *discoveryPresetLabel(uint8_t preset) {
    return (preset < PRESET_COUNT) ? kPresets[preset].name : "?";
}

// The preset the node is configured for, or -1 when custom modem settings are
// in force and no preset is in play. Only used to leave a preset out of the
// picker — there is nothing to scan for on the one we are already on.
static int discoveryCurrentPreset() {
    if (!s_cfg.loraUsePreset) return -1;
    return (s_cfg.modemPreset < PRESET_COUNT) ? (int)s_cfg.modemPreset : -1;
}

// Parks the radio on `preset` for a scan. The retune itself, and why it never
// touches s_cfg, is tempTuneEnter()'s; channel 0 is renamed (renameCh0) so the
// sweep carries the scanned mesh's hash rather than ours.
//
// Keys are not swapped, which bounds what a scan can see: it finds the mesh
// that shares this node's primary key on that preset — the public one, when
// the node is on the default key. openDiscoveryPresetModal() says so rather
// than letting it be discovered.
static void discoveryTuneToPreset(uint8_t preset) {
    tempTuneEnter(preset, s_presetScanSnap, /*renameCh0=*/true);
}

static void discoveryEndPresetScan(bool aborted) {
    if (!s_presetScanActive) return;
    s_presetScanActive = false;
    // An abort takes the sweep with it. The window only means anything while the
    // radio is on the preset being swept, so letting it run on after the retune
    // would close a scan that is no longer happening and report a count for it.
    // The completion path has already cleared this.
    if (aborted) s_discoverySweepStartedMs = 0;

    tempTuneLeave(s_presetScanSnap);

    // The cooldown runs from here whether the scan finished or was cut short: an
    // abort has just retuned the radio, which is exactly when it should not be
    // asked to go straight back out again.
    uint32_t now = millis();
    if (now == 0) now = 1;
    s_discoverySweepEndedMs = now;
    // Closes the result window at the same instant. Everything the radio hears
    // from here is on this node's own preset again, and the list on screen is
    // still headed with the scanned one.
    s_presetScanResultToMs = now;

    Serial.printf("[discovery] preset scan %s (%s) - radio back on %.4f MHz\n",
                  aborted ? "aborted" : "done",
                  discoveryPresetLabel(s_presetScanPreset),
                  (double)s_presetScanSnap.freq);
}

// Elapsed/total for a window that can now be six hours. "43/21600s" is complete
// and unreadable; the unit is dropped down to whatever keeps the number small.
static void discoveryFormatDuration(char *out, size_t cap, uint32_t secs) {
    if (secs < 60) {
        snprintf(out, cap, "%lus", (unsigned long)secs);
    } else if (secs < 3600) {
        snprintf(out, cap, "%lum", (unsigned long)(secs / 60UL));
    } else if (secs % 3600UL == 0) {
        snprintf(out, cap, "%luh", (unsigned long)(secs / 3600UL));
    } else {
        snprintf(out, cap, "%luh%02lum",
                 (unsigned long)(secs / 3600UL),
                 (unsigned long)((secs % 3600UL) / 60UL));
    }
}

// Is something in flight that a stop gesture should stop, rather than clear?
static bool discoveryRunInFlight() {
    return s_presetScanActive || s_discoverySweepStartedMs != 0;
}

#if !UI_TOUCH_ONLY_PROFILE
// The key-hint line. A function rather than two literals, because the C entry
// has to name whichever of its two jobs is live and the build path and the
// refresh path must not word it differently.
static void discoverySetHintText() {
    if (!lvObjValid(s_discoveryHintLabel)) return;
    const char *cWord = discoveryRunInFlight() ? TR("Cancel") : TR("Clear");
#if HAS_FILE_STORAGE
    lv_label_set_text_fmt(s_discoveryHintLabel,
                          TR("W = Sweep   P = Preset   C = %s   S = Save   %s = Back"),
                          cWord, modalCloseKeyLabel());
#else
    lv_label_set_text_fmt(s_discoveryHintLabel,
                          TR("W = Sweep   P = Preset   C = %s   %s = Back"),
                          cWord, modalCloseKeyLabel());
#endif
}
#endif


// Ends a sweep or a scan early, keeping whatever it has already collected. The
// results are not thrown away: a run cut short has still heard what it heard,
// and someone who wanted the list gone has Clear for that -- which is the whole
// reason this is a separate verb rather than a longer Clear.
static void discoveryCancelRun() {
    if (s_presetScanActive) {
        // Takes the sweep window and the retune with it; see the abort path
        // there. That function says nothing to the user, so the status is ours.
        discoveryEndPresetScan(/*aborted=*/true);
        discoverySetStatus(TR("Scan cancelled"));
        return;
    }
    if (s_discoverySweepStartedMs == 0) return;
    s_discoverySweepStartedMs = 0;

    // The cooldown runs from here, exactly as it would had the window closed on
    // its own. Cancelling stops us listening; it does not un-ask the question --
    // the broadcast has gone out and every node within three hops is already
    // composing a reply. Letting a cancel hand back a fresh sweep immediately
    // would make it the way to bypass the rate limit.
    uint32_t now = millis();
    if (now == 0) now = 1;
    s_discoverySweepEndedMs = now;
    discoverySetStatus(TR("Sweep cancelled"));
}

// Retunes to `preset`, sweeps it, and leaves serviceDiscoverySweep() to bring
// the radio home when the window closes.
static void discoveryStartPresetScan(uint8_t preset) {
    if (preset >= PRESET_COUNT) return;
    if (s_presetScanActive) {
        discoverySetStatus(TR("Preset scan already running"));
        return;
    }
    if (preset == discoveryCurrentPreset()) {
        discoverySetStatus(TR("Already on that preset"));
        return;
    }
    char why[72];
    if (!discoverySweepAllowed(why, sizeof(why))) {
        discoverySetStatus(why);
        return;
    }

    // Guards first, then retune, then send — and if the send fails the radio
    // goes home before the failure is reported, so a refused scan never leaves
    // the node parked somewhere it does not live.
    s_presetScanActive = true;
    // From here the screen reports this scan and nothing else. Set at the start
    // rather than at the end so the list is already scoped while it fills, and
    // deliberately left set afterwards -- the result has to survive being read.
    uint32_t scanFrom = millis();
    if (scanFrom == 0) scanFrom = 1;          // 0 is the "not scoped" sentinel
    s_presetScanResultFromMs = scanFrom;
    s_presetScanResultToMs = 0;               // open until the radio comes home
    s_presetScanResultPreset = preset;
    s_presetScanPreset = preset;
    discoveryTuneToPreset(preset);

    if (!Channels.sendDiscoverySweep(s_myNodeId,
                                     s_cfg.nodeLong,
                                     s_cfg.nodeShort,
                                     kDiscoverySweepHopLimit)) {
        discoveryEndPresetScan(/*aborted=*/true);
        discoverySetStatus(TR("Scan send failed"));
        return;
    }

    // Findings have to be readable as an answer to "what is on MediumFast",
    // which they are not while nodes heard on this node's own preset are still
    // listed alongside them. The clear watermark does it without touching the
    // node table — see discoveryClear(). The results stay on screen after the
    // radio comes home, which is the point of running the scan at all.
    discoveryClear(/*keepScanView=*/true);

    uint32_t now = millis();
    if (now == 0) now = 1;   // 0 is the "no sweep in flight" sentinel
    s_discoverySweepStartedMs = now;
    s_discoveryLastSweepMs = now;
    s_discoverySweepBaseNodes = Nodes.count();

    char msg[72];
    snprintf(msg, sizeof(msg), TR("Scanning %s..."), discoveryPresetLabel(preset));
    discoverySetStatus(msg);
    Serial.printf("[discovery] preset scan started: %s (%.1f kHz SF%u CR4/%u)\n",
                  discoveryPresetLabel(preset), (double)kPresets[preset].bw,
                  (unsigned)kPresets[preset].sf, (unsigned)kPresets[preset].cr);
}

// A hash of what the screen would show: who has been heard, at what distance
// and SNR, plus the neighbor reports. The screen rebuilds when it changes, and a
// run saving as it goes rewrites its file when it changes.
//
// This has to hash the nodes themselves, not just count them. The obvious
// cheap signature -- node count plus report count -- misses the case that
// matters most: after a Clear, a node reappears by being *heard from* again,
// which changes neither count. `meshDirty` does not save us either, because
// processMeshPacket() returns false for NodeInfo, Position, Telemetry and
// NeighborInfo -- precisely the packets that refill this screen.
static uint32_t discoveryResultSig() {
    uint32_t sig = (uint32_t)discoveryUsableReportCount() * 7919u
                 + s_discoveryClearedMs;
    const int total = Nodes.count();
    for (int i = 0; i < total; i++) {
        const NodeEntry *e = Nodes.at(i);
        if (!e || !discoveryHeardFrom(*e)) continue;
        // Whole-dB SNR: the reading jitters constantly, and rebuilding the tree
        // (or rewriting the file) for a tenth of a dB is not worth it.
        sig += e->nodeId
             ^ ((uint32_t)e->hops << 24)
             ^ (e->hasHops ? 0x5A5A5A5Au : 0u)
             ^ ((uint32_t)(int32_t)e->snr << 8);
    }
    return sig;
}

// ── Save while discovering ───────────────────────────────────────────────────
// One file per run, picked when it starts and rewritten as results change, so a
// six-hour sweep leaves one current snapshot rather than dozens -- and whatever
// was heard is on the card even if the run is cut short by a reboot or a flat
// battery, which the Save button at the end cannot promise.
//
// Rate-limited: a busy mesh changes the result set many times a minute, and each
// write is the whole file. One final write when the run ends, however it ends.
#if HAS_FILE_STORAGE
static constexpr uint32_t kDiscoveryLiveSaveMinGapMs = 5000;
static bool     s_discoveryLiveSaveActive = false;
static char     s_discoveryLiveSavePath[64] = {};
static uint32_t s_discoveryLiveSaveSig = 0;
static uint32_t s_discoveryLiveSaveMs = 0;

static void discoveryLiveSaveBegin() {
    s_discoveryLiveSaveActive = false;
    char msg[72];
    if (!discoveryPickSavePath(s_discoveryLiveSavePath, sizeof(s_discoveryLiveSavePath),
                               msg, sizeof(msg))
        || !discoveryWriteJson(s_discoveryLiveSavePath, msg, sizeof(msg))) {
        // The run goes ahead regardless; it just says it is not being saved.
        discoverySetStatus(msg);
        Serial.printf("[discovery] save while discovering unavailable: %s\n", msg);
        return;
    }
    s_discoveryLiveSaveActive = true;
    s_discoveryLiveSaveSig = discoveryResultSig();
    s_discoveryLiveSaveMs = millis();
    Serial.printf("[discovery] saving as it runs to %s\n", s_discoveryLiveSavePath);
}

static void discoveryLiveSaveService() {
    if (!s_discoveryLiveSaveActive) return;
    char msg[72];
    if (!discoveryRunInFlight()) {
        // Ended -- completed, cancelled or aborted. The final picture, then done.
        s_discoveryLiveSaveActive = false;
        const bool ok = discoveryWriteJson(s_discoveryLiveSavePath, msg, sizeof(msg));
        if (!ok) {
            discoverySetStatus(msg);
        } else if (s_discoveryStatus[0]) {
            // Keep the run's own summary and say where it went.
            char line[sizeof(s_discoveryStatus)];
            snprintf(line, sizeof(line), TR("%s - saved"), s_discoveryStatus);
            discoverySetStatus(line);
        }
        refreshDiscoveryModal(true);
        return;
    }
    const uint32_t now = millis();
    if ((uint32_t)(now - s_discoveryLiveSaveMs) < kDiscoveryLiveSaveMinGapMs) return;
    const uint32_t sig = discoveryResultSig();
    if (sig == s_discoveryLiveSaveSig) return;
    s_discoveryLiveSaveSig = sig;
    s_discoveryLiveSaveMs = now;
    if (!discoveryWriteJson(s_discoveryLiveSavePath, msg, sizeof(msg))) {
        // Keep trying: a card that was briefly busy should not end the saving.
        Serial.printf("[discovery] live save failed: %s\n", msg);
    }
}
#else
static inline void discoveryLiveSaveBegin() {}
static inline void discoveryLiveSaveService() {}
#endif

// Closes out a sweep once its collection window has passed. Runs from the main
// loop whether or not the modal is open, so a sweep started and then abandoned
// still clears and still counts.
static void serviceDiscoverySweepWindow() {
    // A UI rebuild deletes the root screen — and the modal with it — without
    // going through closeDiscoveryModal(). For a plain sweep that costs nothing;
    // for a scan it would strand the radio on a foreign preset until the next
    // reboot, so the modal being gone is itself a reason to come home.
    if (s_presetScanActive && !lvObjAlive(s_discoveryModal)) {
        discoveryEndPresetScan(/*aborted=*/true);
        return;
    }
    if (s_discoverySweepStartedMs == 0) return;
    const uint32_t windowMs = s_presetScanActive ? s_discoveryPresetScanWindowMs
                                                 : s_discoverySweepWindowMs;
    const uint32_t elapsedMs = millis() - s_discoverySweepStartedMs;
    if (elapsedMs < windowMs) return;

    int found;
    if (s_presetScanActive) {
        // Counted by who was actually heard, not by how much the table grew.
        // The delta counted anything that arrived during the window, and the
        // MQTT bridge keeps delivering while the radio is parked elsewhere --
        // which is how a scan reported nodes that were never on that preset.
        // discoveryHeardFrom() is already scoped to the scan, so this agrees
        // with the list by construction rather than by coincidence.
        found = 0;
        const int total = Nodes.count();
        for (int i = 0; i < total; i++) {
            const NodeEntry *e = Nodes.at(i);
            if (!e || e->nodeId == 0 || e->nodeId == s_myNodeId) continue;
            if (discoveryHeardFrom(*e)) found++;
        }
    } else {
        found = Nodes.count() - s_discoverySweepBaseNodes;
        if (found < 0) found = 0;   // the table evicts; a shrinking count is not -2 nodes
    }
    s_discoverySweepStartedMs = 0;

    char msg[72];
    if (s_presetScanActive) {
        // Names the preset, because "4 new" means something very different when
        // those four are on another mesh entirely.
        const uint8_t scanned = s_presetScanPreset;
        discoveryEndPresetScan(/*aborted=*/false);
        snprintf(msg, sizeof(msg), TR("%s: %d on preset (%lus)"),
                 discoveryPresetLabel(scanned), found,
                 (unsigned long)(elapsedMs / 1000UL));
    } else {
        uint32_t endedMs = millis();
        if (endedMs == 0) endedMs = 1;
        s_discoverySweepEndedMs = endedMs;
        snprintf(msg, sizeof(msg), TR("Sweep done: %d new (%lus)"),
                 found, (unsigned long)(elapsedMs / 1000UL));
    }
    discoverySetStatus(msg);
    refreshDiscoveryModal(true);
}

// The window check, then the live save -- in that order, so a run that has
// just closed gets its final write on the same pass.
static void serviceDiscoverySweep() {
    serviceDiscoverySweepWindow();
    discoveryLiveSaveService();
}

static void refreshDiscoveryModal(bool force) {
    if (!s_discoveryModal || !s_discoveryColBoxes[0] || !s_discoveryStatusLabel) return;
    if (!lvObjValid(s_discoveryModal)) {
        s_discoveryModal = nullptr;
        s_discoveryStatusLabel = nullptr;
        s_discoveryWardriveLabel = nullptr;
        s_discoveryList = nullptr;
        s_discoveryClearLabel = nullptr;
        s_discoveryHintLabel = nullptr;
        memset(s_discoveryColBoxes, 0, sizeof(s_discoveryColBoxes));
        return;
    }

    // The sweep counter ticks once a second and changes nothing in the list, so
    // it drives the status line only. Rebuilding the columns tears down and
    // recreates every label in them, which is not something to do to a screen
    // the user is reading just because a timer moved.
    if (s_discoverySweepStartedMs != 0) {
        const uint32_t elapsedS = (millis() - s_discoverySweepStartedMs) / 1000UL;
        char msg[72];
        char sofar[12];
        char total[12];
        discoveryFormatDuration(sofar, sizeof(sofar), elapsedS);
        if (s_presetScanActive) {
            discoveryFormatDuration(total, sizeof(total),
                                    s_discoveryPresetScanWindowMs / 1000UL);
            snprintf(msg, sizeof(msg), TR("Scanning %s %s/%s"),
                     discoveryPresetLabel(s_presetScanPreset), sofar, total);
        } else {
            // Counts up to the chosen window, so a six-hour sweep does not look
            // like a one-minute sweep that has badly overrun.
            discoveryFormatDuration(total, sizeof(total),
                                    s_discoverySweepWindowMs / 1000UL);
            snprintf(msg, sizeof(msg), TR("Sweeping... %s/%s"), sofar, total);
        }
#if HAS_FILE_STORAGE
        // Saving as it goes: name the file on the same line, so there is no
        // question where the results are landing while it runs.
        if (s_discoveryLiveSaveActive) {
            const char *name = strrchr(s_discoveryLiveSavePath, '/');
            name = name ? name + 1 : s_discoveryLiveSavePath;
            const size_t used = strlen(msg);
            snprintf(msg + used, sizeof(msg) - used, TR(" - saving to %s"), name);
        }
#endif
        discoverySetStatus(msg);
    } else if (!s_discoveryStatus[0]) {
        discoverySetStatus(TR("Ready"));
    }

    // The button and the key hint both describe what C does, so they are
    // retitled together and from one condition -- two conditions would be two
    // chances for the screen to offer a button and a hint that disagree.
    //
    // Beside the status line, on the same tick and for the same reason: all
    // three are answering "what is this screen doing right now". Gated on the
    // answer having changed, because this runs on the UI tick and
    // lv_label_set_text() invalidates.
    {
        const bool cancels = discoveryRunInFlight();
        if (cancels != s_discoveryActionsShowCancel) {
            s_discoveryActionsShowCancel = cancels;
            if (lvObjValid(s_discoveryClearLabel)) {
                lv_label_set_text(s_discoveryClearLabel, cancels ? TR("Cancel") : TR("Clear"));
            }
#if !UI_TOUCH_ONLY_PROFILE
            discoverySetHintText();
#endif
        }
    }

    // Rebuilding tears down and recreates every label in the columns, so it is
    // gated on the visible set actually having changed (discoveryResultSig()).
    // That is one cheap pass over the node table, and only while the modal is
    // open -- the price of the screen being live at all.
    const uint32_t sig = discoveryResultSig();
    // The wardriving line is not part of discoveryResultSig(), which also drives
    // "save while discovering" rewrites that a GPS fix flapping must not trigger.
    // Its presence follows the log switch (a rebuild adds or drops it); its text
    // is set in place, and only when it changed, since lv_label_set_text()
    // invalidates.
    const bool wantWardrive = wardriveLogIsEnabled();
    const bool haveWardrive = s_discoveryWardriveLabel != nullptr;
    if (!force && sig == s_discoveryRenderedSig && wantWardrive == haveWardrive) {
        if (haveWardrive && lvObjValid(s_discoveryWardriveLabel)) {
            char text[sizeof(s_discoveryWardriveText)];
            discoveryWardriveFormat(text, sizeof(text));
            if (strcmp(text, s_discoveryWardriveText) != 0) {
                memcpy(s_discoveryWardriveText, text, sizeof(text));
                lv_label_set_text(s_discoveryWardriveLabel, s_discoveryWardriveText);
            }
        }
        return;
    }
    s_discoveryRenderedSig = sig;

    discoveryBuildColumns();
}

// ── Preset picker ────────────────────────────────────────────────────────────
// Sits above Discovery and chooses the preset to scan. Unlike the chat-name and
// font-size pickers, a tap here selects rather than commits: the Scan button and
// Enter are the only ways to start one. A stray tap on those costs a repaint; a
// stray tap on this costs five minutes off the node's own mesh.

static void closeDiscoveryPresetModal() {
    if (lvObjValid(s_presetPickBackdrop)) {
        lv_obj_del(s_presetPickBackdrop);
    } else if (lvObjValid(s_presetPickModal)) {
        lv_obj_del(s_presetPickModal);
    }
    s_presetPickBackdrop = nullptr;
    s_presetPickModal = nullptr;
    memset(s_presetPickRows, 0, sizeof(s_presetPickRows));
    s_presetPickCount = 0;
    s_presetPickSelection = 0;
}

static void refreshDiscoveryPresetSelection() {
    if (!s_presetPickModal) return;
    paintPickerRows(s_presetPickRows, s_presetPickCount, s_presetPickSelection, PICKER_SCROLL_VIEW);
}

// ── How long to listen ───────────────────────────────────────────────────────
// Asked before the run starts rather than after, because neither a sweep nor a
// scan can be lengthened once it is going: the window is what decides when the
// results are called final.
static const char *discoveryDurationLabelFor(int idx) {
    if (idx < 0 || idx >= kDiscoveryDurationCount) idx = 0;
    return TR(kDiscoveryDurations[idx].label);
}

static void discoveryDurationApply(int idx) {
    if (idx < 0 || idx >= kDiscoveryDurationCount) idx = 0;
    const uint32_t chosen = kDiscoveryDurations[idx].ms;
    const uint8_t preset = s_discoveryPendingPreset;
    s_discoveryPendingPreset = kDiscoveryNoPresetPending;   // consumed either way

    if (preset == kDiscoveryNoPresetPending) {
        s_discoverySweepWindowMs = chosen;
        discoveryStartSweep();          // refuses, with a reason, when it must
    } else {
        s_discoveryPresetScanWindowMs = chosen;
        discoveryStartPresetScan(preset);
    }
    // Only for a run that actually started: a refusal already said why, and
    // there is nothing to save.
    if (s_discoverySaveWhileRunning && discoveryRunInFlight()) discoveryLiveSaveBegin();
    refreshDiscoveryModal(true);
}

// `preset` is kDiscoveryNoPresetPending for a plain sweep, or the preset a scan
// is about to retune to. Opens on whatever that kind of run last used, which is
// the board's own default until someone changes it.
static void openDiscoveryDurationModal(uint8_t preset) {
    // Refusals first. A sweep can be turned down for channel utilization, for
    // the minimum gap since the last one, or for the cooldown after one closed,
    // and none of those become less true for being asked about afterwards --
    // being made to choose a duration and only then told "not yet" is a worse
    // answer than "not yet". discoveryStartSweep() checks again when it runs;
    // this is the same check moved in front of the question, not a substitute.
    if (preset == kDiscoveryNoPresetPending) {
        char why[72];
        if (!discoverySweepAllowed(why, sizeof(why))) {
            discoverySetStatus(why);
            refreshDiscoveryModal(true);
            return;
        }
    }
    s_discoveryPendingPreset = preset;
    const uint32_t current = (preset == kDiscoveryNoPresetPending)
                                 ? s_discoverySweepWindowMs
                                 : s_discoveryPresetScanWindowMs;
    int startIdx = 0;
    for (int i = 0; i < kDiscoveryDurationCount; i++) {
        if (kDiscoveryDurations[i].ms == current) { startIdx = i; break; }
    }
    // One title for both: the modal is where a run is set up, and the duration
    // is now one of two settings on it. The checkbox is offered only where there
    // is storage to save to.
#if HAS_FILE_STORAGE
    static const char *const kSaveWhileLabel = TR_NOOP("Save while discovering");
    bool *const saveWhileValue = &s_discoverySaveWhileRunning;
#else
    static const char *const kSaveWhileLabel = nullptr;
    bool *const saveWhileValue = nullptr;
#endif
    static const CfgSliderPicker kRunSpec = {
        TR_NOOP("Sweep/Scan Settings"),
        kDiscoveryDurationCount,
        discoveryDurationLabelFor,
        discoveryDurationApply,
        TR_NOOP("30 sec"),
        TR_NOOP("6 hours"),
        kSaveWhileLabel,
        saveWhileValue,
    };
    openCfgSliderModal(&kRunSpec, startIdx);
}

static void discoveryPresetPickCommit() {
    if (s_presetPickSelection < 0 || s_presetPickSelection >= s_presetPickCount) return;
    const uint8_t preset = s_presetPickChoices[s_presetPickSelection];
    // Closed before the next modal opens, so the status line the scan writes
    // ends up on the screen the user is looking at rather than behind this one.
    closeDiscoveryPresetModal();
    // How long, then go. The scan itself starts from discoveryDurationApply().
    openDiscoveryDurationModal(preset);
}

static void onDiscoveryPresetRowPressed(lv_event_t *e) {
    const int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_presetPickCount) return;
    s_presetPickSelection = idx;
    refreshDiscoveryPresetSelection();
}

static void openDiscoveryPresetModal() {
    if (!s_rootScreen || !s_discoveryModal) return;
    if (s_presetPickModal || s_presetPickBackdrop) return;
    if (s_presetScanActive) {
        discoverySetStatus(TR("Preset scan already running"));
        refreshDiscoveryModal(true);
        return;
    }

    // Every preset but the one in force. With custom modem settings active no
    // preset is current, so all of them are offered.
    const int current = discoveryCurrentPreset();
    s_presetPickCount = 0;
    for (uint8_t i = 0; i < PRESET_COUNT; i++) {
        if ((int)i == current) continue;
        s_presetPickChoices[s_presetPickCount++] = i;
    }
    if (s_presetPickCount == 0) return;
    s_presetPickSelection = 0;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 300) modalW = 300;

    s_presetPickBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_presetPickBackdrop, w, h);
    lv_obj_align(s_presetPickBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_presetPickBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_presetPickBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_presetPickBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_presetPickBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_presetPickBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_presetPickBackdrop, 0, 0);
    lv_obj_add_event_cb(s_presetPickBackdrop,
                        [](lv_event_t *e) { LV_UNUSED(e); closeDiscoveryPresetModal(); },
                        LV_EVENT_CLICKED, nullptr);

    s_presetPickModal = lv_obj_create(s_presetPickBackdrop);
    lv_obj_set_size(s_presetPickModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_presetPickModal, (h > 40) ? (h - 16) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_presetPickModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_presetPickModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_presetPickModal, LV_OBJ_FLAG_CLICKABLE);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_color(s_presetPickModal, lv_color_make(255, 255, 255), 0);
    lv_obj_set_style_border_color(s_presetPickModal, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(s_presetPickModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_border_color(s_presetPickModal, lv_color_hex(0x5C86C6), 0);
#endif
    lv_obj_set_style_bg_opa(s_presetPickModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_presetPickModal, 1, 0);
    lv_obj_set_style_pad_all(s_presetPickModal, 8, 0);
    lv_obj_set_style_pad_row(s_presetPickModal, 5, 0);
    lv_obj_set_flex_flow(s_presetPickModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_presetPickModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_presetPickBackdrop);

#if defined(DEVICE_TDECK_PRO)
    const lv_color_t titleColor = lv_color_make(0, 0, 0);
    const lv_color_t hintColor  = lv_color_make(0, 0, 0);
    const lv_color_t rowColor   = lv_color_make(0, 0, 0);
#else
    const lv_color_t titleColor = lv_color_hex(0xD9E8FF);
    const lv_color_t hintColor  = lv_color_hex(0xA7C7FF);
    const lv_color_t rowColor   = (s_cfg.uiMode == UI_MODE_LIGHT)
                                      ? lv_color_hex(0x13233D) : lv_color_hex(0xD9E8FF);
#endif

    lv_obj_t *title = lv_label_create(s_presetPickModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, titleColor, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Scan a preset"));

    // The cost, before the user commits rather than after. A node parked on
    // another preset hears nothing sent to it on its own — no messages, no ACKs,
    // no traceroute replies — and five minutes is long enough to matter.
    lv_obj_t *warn = lv_label_create(s_presetPickModal);
    lv_obj_set_width(warn, lv_pct(100));
    lv_obj_set_style_text_font(warn, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(warn, hintColor, 0);
    lv_obj_set_style_text_align(warn, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(warn, LV_LABEL_LONG_WRAP);
    lv_label_set_text(warn, TR("5 min on that preset. This node hears nothing on its own "
                            "while it runs, and finds only meshes sharing your primary "
                            "channel key."));

    lv_obj_t *list = lv_obj_create(s_presetPickModal);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_height(list, LV_SIZE_CONTENT);
    // Capped so the Scan button below stays on screen rather than scrolling off
    // with the rows.
    lv_obj_set_style_max_height(list, (h > 150) ? (h - 118) : LV_SIZE_CONTENT, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(list);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_style_pad_right(list, 2, 0);

    for (int i = 0; i < s_presetPickCount; i++) {
        const PresetParams &p = kPresets[s_presetPickChoices[i]];
        lv_obj_t *row = lv_btn_create(list);
#if defined(DEVICE_TDECK_PRO)
        lv_obj_remove_style_all(row);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
#endif
        s_presetPickRows[i] = row;
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 5, 0);
        lv_obj_set_style_pad_row(row, 1, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);
        lv_obj_add_event_cb(row, onDiscoveryPresetRowPressed, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(name, rowColor, 0);
        lv_label_set_text(name, p.name);

        if (kModalRowDescriptions) {
            lv_obj_t *desc = lv_label_create(row);
            lv_obj_set_style_text_font(desc, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(desc, rowColor, 0);
#if defined(DEVICE_TDECK_PRO)
            lv_obj_set_style_text_opa(desc, LV_OPA_COVER, 0);
#else
            lv_obj_set_style_text_opa(desc, LV_OPA_70, 0);
#endif
            lv_label_set_text_fmt(desc, TR("%s  SF%u  %.0f kHz"),
                                  p.channelName, (unsigned)p.sf, (double)p.bw);
        }
    }

    lv_obj_t *btnRow = lv_obj_create(s_presetPickModal);
    lv_obj_set_width(btnRow, lv_pct(100));
    lv_obj_set_height(btnRow, LV_SIZE_CONTENT);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_style_pad_column(btnRow, 10, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    auto makePickBtn = [](lv_obj_t *parent, const char *text, uint32_t color,
                          lv_event_cb_t cb) {
        lv_obj_t *btn = lv_btn_create(parent);
        lv_obj_set_height(btn, 26);
        lv_obj_set_style_min_width(btn, 76, 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
#if defined(DEVICE_TDECK_PRO)
        LV_UNUSED(color);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_make(0, 0, 0), 0);
#else
        lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
#endif
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
#if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_text_color(lbl, lv_color_make(0, 0, 0), 0);
#else
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
#endif
        lv_label_set_text(lbl, TR(text));
        lv_obj_center(lbl);
    };
    makePickBtn(btnRow, TR_NOOP("Cancel"), 0x6B3030,
                [](lv_event_t *e) { LV_UNUSED(e); closeDiscoveryPresetModal(); });
    makePickBtn(btnRow, TR_NOOP("Scan"), 0x2F6B30,
                [](lv_event_t *e) { LV_UNUSED(e); discoveryPresetPickCommit(); });

    lv_obj_t *hint = lv_label_create(s_presetPickModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, hintColor, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
#if UI_TOUCH_ONLY_PROFILE
    lv_label_set_text(hint, TR("Tap a preset, then Scan"));
#else
    lv_label_set_text_fmt(hint, TR("Move  Enter=Scan  %s=Cancel"), modalCloseKeyLabel());
#endif

    refreshDiscoveryPresetSelection();
}

static void openDiscoveryModal() {
    // A UI rebuild deletes the root screen out from under us; without this
    // the stale pointer would lock the surface shut for the rest of the boot.
    if (s_discoveryModal && !lvObjAlive(s_discoveryModal)) {
        s_discoveryModal = nullptr;
        s_discoveryStatusLabel = nullptr;
        s_discoveryList = nullptr;
        memset(s_discoveryColBoxes, 0, sizeof(s_discoveryColBoxes));
    }
    if (!s_rootScreen || s_discoveryModal) return;

    // Drop a stale sweep message, but keep one from a sweep that is running or
    // that just finished — closing and reopening is exactly what someone does
    // while waiting for the window to close.
    const bool sweepIsCurrent = s_discoverySweepStartedMs != 0
        || (s_discoveryLastSweepMs != 0
            && (uint32_t)(millis() - s_discoveryLastSweepMs)
                   < s_discoverySweepWindowMs + kDiscoverySweepMinGapMs);
    if (!sweepIsCurrent) s_discoveryStatus[0] = '\0';

    int modalW = lv_disp_get_hor_res(NULL);
    int modalH = lv_disp_get_ver_res(NULL);

    s_discoveryModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_discoveryModal, modalW, modalH);
    lv_obj_align(s_discoveryModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_discoveryModal, LV_OBJ_FLAG_SCROLLABLE);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_color(s_discoveryModal, lv_color_make(255, 255, 255), 0);
    lv_obj_set_style_border_color(s_discoveryModal, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(s_discoveryModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_border_color(s_discoveryModal, lv_color_hex(0x5C86C6), 0);
#endif
    lv_obj_set_style_bg_opa(s_discoveryModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_discoveryModal, 1, 0);
    lv_obj_set_style_pad_all(s_discoveryModal, 4, 0);
    lv_obj_set_style_pad_row(s_discoveryModal, 4, 0);
    lv_obj_set_flex_flow(s_discoveryModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_discoveryModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    // Portrait (P4, Heltec V4): Tools' title in place of the header bar, no X -- the nav
    // bar added at the end is the way out -- and bigger action buttons under
    // it, as on every screen Tools opens (toolStackLayout()).
#if HAS_TOOL_STACK_LAYOUT
    const bool discoveryBig = toolStackLayout();
    if (discoveryBig) {
        toolStackTitle(s_discoveryModal, TR("Discovery"));
    } else
#else
    constexpr bool discoveryBig = false;
#endif
    {
    lv_obj_t *header = lv_obj_create(s_discoveryModal);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_height(header, 26);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(header, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(header, lv_color_hex(0x123266), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_70, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0x335D9D), 0);
#endif
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_pad_left(header, 4, 0);
    lv_obj_set_style_pad_right(header, 4, 0);
    lv_obj_set_style_pad_top(header, 1, 0);
    lv_obj_set_style_pad_bottom(header, 1, 0);

    cornerSafeHeader(header);
    lv_obj_t *title = lv_label_create(header);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(title, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
#endif
    lv_label_set_text(title, TR("Discovery"));
    lv_obj_center(title);
    }   // header bar
    (void)discoveryBig;

#if UI_TOUCH_ONLY_PROFILE
    // Actions get their own full-width row rather than sharing the header with
    // the title and the corner X, as they used to. Two right-aligned 44px
    // buttons fitted there; four do not — on the 240px vertical build they would
    // leave 28px for the title. A flex row of equal shares also gives bigger tap
    // targets (~55px on the narrowest panel) than the fixed widths did, and adding
    // a fifth action later is a line rather than a re-layout.
    lv_obj_t *actionRow = toolActionRow(s_discoveryModal);
    auto makeDiscoveryBtn = [](lv_obj_t *parent, const char *text,
                               lv_event_cb_t cb) -> lv_obj_t * {
        return toolActionBtn(parent, TR(text), cb);
    };
    makeDiscoveryBtn(actionRow, TR_NOOP("Sweep"), [](lv_event_t *e) {
        LV_UNUSED(e);
        // Asks how long first; the sweep starts from discoveryDurationApply().
        openDiscoveryDurationModal(kDiscoveryNoPresetPending);
    });
    // The keyboard boards reach this with P; on a touch-only build a button is
    // the only way in.
    makeDiscoveryBtn(actionRow, TR_NOOP("Preset"), [](lv_event_t *e) {
        LV_UNUSED(e);
        openDiscoveryPresetModal();
    });
    // Built with whichever word is true right now: a plain sweep survives closing
    // and reopening this screen, so the modal can be built with one in flight.
    // refreshDiscoveryModal() keeps it honest from there.
    // Seeded here, not left at whatever the last modal left behind, so the first
    // refresh tick does not think nothing has changed and skip both labels.
    s_discoveryActionsShowCancel = discoveryRunInFlight();
    s_discoveryClearLabel = makeDiscoveryBtn(
        actionRow, s_discoveryActionsShowCancel ? TR_NOOP("Cancel") : TR_NOOP("Clear"), [](lv_event_t *e) {
            LV_UNUSED(e);
            // The same two meanings as the C key, for the same reason -- see the
            // note there. This is the only way in on a build with no keyboard.
            if (discoveryRunInFlight()) discoveryCancelRun();
            else                        discoveryClear();
            refreshDiscoveryModal(true);
        });
#if HAS_FILE_STORAGE
    // Same reasoning as the S key on the keyboard builds: the question is
    // whether a file can be saved, not whether a card slot exists. These boards
    // had no way to reach Save at all — the Wio Tracker L2 has an SD card and
    // still could not use it, because the only route in was a keyboard it does
    // not have.
    makeDiscoveryBtn(actionRow, "Save", [](lv_event_t *e) {
        LV_UNUSED(e);
        char msg[72];
        (void)discoverySaveJson(msg, sizeof(msg));   // msg says which way it went
        discoverySetStatus(msg);
        refreshDiscoveryModal(true);
    });
#endif
#endif  // UI_TOUCH_ONLY_PROFILE

    s_discoveryStatusLabel = lv_label_create(s_discoveryModal);
    lv_obj_set_width(s_discoveryStatusLabel, lv_pct(100));
    lv_obj_set_style_text_font(s_discoveryStatusLabel, &lv_font_montserrat_10, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(s_discoveryStatusLabel, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(s_discoveryStatusLabel, lv_color_hex(0xB8D4FF), 0);
#endif
    lv_obj_set_style_text_align(s_discoveryStatusLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_discoveryStatusLabel, s_discoveryStatus[0] ? s_discoveryStatus : "-");

    s_discoveryList = lv_obj_create(s_discoveryModal);
    lv_obj_set_width(s_discoveryList, lv_pct(100));
    lv_obj_set_flex_grow(s_discoveryList, 1);
    lv_obj_add_flag(s_discoveryList, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_discoveryList);
    lv_obj_set_scrollbar_mode(s_discoveryList, LV_SCROLLBAR_MODE_AUTO);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_color(s_discoveryList, lv_color_make(255, 255, 255), 0);
    lv_obj_set_style_bg_opa(s_discoveryList, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_discoveryList, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(s_discoveryList, lv_color_hex(0x123266), 0);
    lv_obj_set_style_bg_opa(s_discoveryList, LV_OPA_40, 0);
    lv_obj_set_style_border_color(s_discoveryList, lv_color_hex(0x335D9D), 0);
#endif
    lv_obj_set_style_border_width(s_discoveryList, 1, 0);
    lv_obj_set_style_pad_all(s_discoveryList, 3, 0);
    lv_obj_set_style_width(s_discoveryList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_discoveryList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_discoveryList, LV_OPA_70, LV_PART_SCROLLBAR);

    // Columns are laid out by a flex row inside the scroller, so the tallest
    // one sets the scroll height and short ones simply end early.
    lv_obj_t *colRow = s_discoveryList;
    if (kDiscoveryCols > 1) {
        colRow = lv_obj_create(s_discoveryList);
        lv_obj_set_width(colRow, lv_pct(100));
        lv_obj_set_height(colRow, LV_SIZE_CONTENT);
        lv_obj_clear_flag(colRow, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(colRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(colRow, 0, 0);
        lv_obj_set_style_pad_all(colRow, 0, 0);
        lv_obj_set_style_pad_column(colRow, 6, 0);
        lv_obj_set_flex_flow(colRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(colRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);
    }

    // Each column is a container, not a label: a group's heading and its rows
    // are separate labels so they can carry different fonts, and only a
    // container can stack them. refreshDiscoveryModal() empties and refills
    // these, so nothing outside holds a pointer to what is inside them.
    for (int i = 0; i < kDiscoveryCols; i++) {
        lv_obj_t *box = lv_obj_create(colRow);
        s_discoveryColBoxes[i] = box;
        if (kDiscoveryCols > 1) {
            lv_obj_set_flex_grow(box, 1);   // equal shares of the row
        } else {
            lv_obj_set_width(box, lv_pct(100));
        }
        lv_obj_set_height(box, LV_SIZE_CONTENT);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(box, 0, 0);
        lv_obj_set_style_pad_all(box, 0, 0);
        lv_obj_set_style_pad_row(box, 0, 0);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);
    }

#if !UI_TOUCH_ONLY_PROFILE
    lv_obj_t *hint = lv_label_create(s_discoveryModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(hint, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
#endif
    s_discoveryHintLabel = hint;
    discoverySetHintText();
#endif

    // The way out of this screen on every build: it has no X. Tools lit, as
    // the screen it was opened from. Last, so its spacer is the final child of
    // the column. A no-op where the bar is switched off.
    appendHeltecBottomNav(s_discoveryModal, HELTEC_NAV_TOOLS);

    refreshDiscoveryModal(true);
}

#endif  // FEATURE_DISCOVERY

