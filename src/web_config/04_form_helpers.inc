// SPDX-License-Identifier: GPL-3.0-or-later
// Part of web_config.cpp: HTML/JS escaping and form section helpers.
// Included in order from web_config.cpp as one translation unit; not built on its own.

// Appends text into a single-quoted HTML attribute. Channel names are free text
// typed on the device or imported from YAML, and one apostrophe in a name would
// otherwise close the value= attribute and mangle the rest of the form — the
// name field would come back truncated on the next save.
static void appendAttr(String &html, const char *s) {
    if (!s) return;
    for (; *s; ++s) {
        switch (*s) {
            case '&':  html += "&amp;";  break;
            case '<':  html += "&lt;";   break;
            case '>':  html += "&gt;";   break;
            case '\'': html += "&#39;";  break;
            case '"':  html += "&quot;"; break;
            default:   html += *s;       break;
        }
    }
}

// Appends text into a quoted JS string literal inside an inline <script>.
// '<' and '>' are hex-escaped so the value can never close the script element.
static void appendJsStr(String &out, const char *s) {
    if (!s) return;
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        unsigned char c = *p;
        if (c == '\\' || c == '\'' || c == '"') { out += '\\'; out += (char)c; }
        else if (c == '<') out += "\\x3C";
        else if (c == '>') out += "\\x3E";
        else if (c < 0x20) {
            char u[7];
            snprintf(u, sizeof(u), "\\u%04X", (unsigned)c);
            out += u;
        } else {
            out += (char)c;
        }
    }
}

// The Community Preset picker in the LoRa Radio section. It sits inside the
// /save form but is not part of it: the select has no name, and Apply posts
// its own one-field form to /community-preset (forms cannot nest), after a
// confirm that names what changes. Presets this device cannot take are listed
// disabled with the reason, rather than missing.
static void appendCommunityPresetPicker(String &html) {
    html += "<label>Community Preset<select id='sel-cp' onchange='cpShow()'>"
            "<option value=''>Choose a local mesh&hellip;</option>";
    const char *area = nullptr;
    for (int i = 0; i < kCommunityPresetCount; i++) {
        const CommunityPreset &cp = kCommunityPresets[i];
        if (!area || strcmp(area, cp.area) != 0) {
            if (area) html += "</optgroup>";
            area = cp.area;
            html += "<optgroup label='"; appendAttr(html, area); html += "'>";
        }
        const char *why = communityPresetProblem(i);
        html += "<option value='"; appendAttr(html, cp.id); html += "'";
        if (why) html += " disabled";
        html += ">"; appendAttr(html, cp.label);
        if (why) { html += " ("; appendAttr(html, why); html += ")"; }
        html += "</option>";
    }
    if (area) html += "</optgroup>";
    html += "</select></label>"
            "<p class='gps-hint' id='cp-info'>Sets the region, radio preset, every channel and the MQTT broker "
            "the way a local mesh publishes them, clears messages and non-favorite nodes, "
            "then reboots. Your current channels are replaced.</p>"
            "<button type='button' id='cp-apply' disabled onclick='cpApply()'>"
            "Apply Community Preset</button>";
    // One entry per preset: label, region, radio, channel names, notes, url.
    html += "<script>var CP={";
    char tmp[16];
    for (int i = 0; i < kCommunityPresetCount; i++) {
        const CommunityPreset &cp = kCommunityPresets[i];
        if (i) html += ",";
        html += "'"; appendJsStr(html, cp.id); html += "':{l:'";
        appendJsStr(html, cp.label); html += "',r:'";
        appendJsStr(html, cp.region); html += "',p:'";
        int radio = -1;
        for (uint8_t k = 0; cp.preset[0] && k < PRESET_COUNT; k++) {
            if (!strcasecmp(cp.preset, kPresets[k].channelName)) { radio = k; break; }
        }
        appendJsStr(html, radio >= 0 ? kPresets[radio].name : "unchanged");
        if (cp.slot) {
            snprintf(tmp, sizeof(tmp), ", slot %u", (unsigned)cp.slot);
            html += tmp;
        }
        html += "',c:[";
        for (int c = 0; c < cp.channelCount; c++) {
            const CommunityPresetChannel &ch = kCommunityPresetChannels[cp.firstChannel + c];
            if (c) html += ",";
            html += "'"; appendJsStr(html, ch.name[0] ? ch.name : "(primary)"); html += "'";
        }
        html += "],m:'";
        if (cp.mqtt.host[0]) {
            appendJsStr(html, cp.mqtt.host); html += " (";
            appendJsStr(html, cp.mqtt.root); html += ")";
        }
        html += "',n:'"; appendJsStr(html, cp.notes);
        html += "',u:'"; appendJsStr(html, cp.url); html += "'}";
        if ((i & 7) == 7) sendChunk(html);
    }
    html += "};"
            "function cpEsc(t){var d=document.createElement('div');d.textContent=t;return d.innerHTML;}"
            "function cpShow(){var id=document.getElementById('sel-cp').value,e=document.getElementById('cp-info'),"
            "b=document.getElementById('cp-apply'),p=CP[id];b.disabled=!p;"
            "if(!p){e.textContent='Sets the region, radio preset, every channel and the MQTT broker the way a local mesh "
            "publishes them, clears messages and non-favorite nodes, then reboots. Your current "
            "channels are replaced.';return;}"
            "e.innerHTML='Region <b>'+cpEsc(p.r)+'</b>, radio <b>'+cpEsc(p.p)+'</b>, channels: '"
            "+cpEsc(p.c.join(', '))+(p.m?', MQTT <b>'+cpEsc(p.m)+'</b> (turned on)':'')+(p.n?'<br>'+cpEsc(p.n):'')"
            "+(p.u?'<br><a href=\"'+cpEsc(p.u)+'\" target=\"_blank\" rel=\"noopener\">Community page</a>':'');}"
            "function cpApply(){var id=document.getElementById('sel-cp').value,p=CP[id];if(!p)return;"
            "if(!confirm('Apply '+p.l+'?\\n\\nRegion: '+p.r+'\\nRadio: '+p.p+'\\nChannels: replaced with '"
            "+p.c.length+(p.m?'\\nMQTT: '+p.m+', turned on':'')+'\\n\\nYour current channels are overwritten, all messages and non-favorite "
            "nodes are cleared, and the device reboots. Continue?'))return;"
            "var f=document.createElement('form');f.method='POST';f.action='/community-preset';"
            "var i=document.createElement('input');i.type='hidden';i.name='id';i.value=id;"
            "f.appendChild(i);document.body.appendChild(f);f.submit();}"
            "</script>";
}

// Config- and Utilities-pane section wrappers. The full page gives each one a
// panel with an id (sec-<slug>) that the rail, the phone's section sheet and
// the URL (#config/<slug>) all point at; web config lite renders a plain
// heading — no styling, so nothing depends on CSS being there.
static void section(String &html, bool lite, const char *title, const char *slug,
                    const char *desc = nullptr) {
    const char *safeTitle = title ? title : "?";
    if (lite) {
        // Progress marker: if a render stalls, the last line names the section.
        Serial.printf("[web] lite: %s\n", safeTitle);
#if defined(DEVICE_CARDPUTER_LORA_HAT)
        char heapTag[80];
        snprintf(heapTag, sizeof(heapTag), "lite: %s", safeTitle);
        logWifiHeapDiag(heapTag);
#endif
        html += "<h3>";
        html += safeTitle;
        html += "</h3>";
        return;
    }
    html += "<section class='cfg-sec' id='sec-";
    html += slug;
    html += "'><h2>";
    html += safeTitle;
    html += "</h2>";
    if (desc) {
        html += "<p class='sec-desc'>";
        html += desc;
        html += "</p>";
    }
}

static void sectionEnd(String &html, bool lite) {
    if (!lite) html += "</section>";
}

// ── Login page ────────────────────────────────────────────────

static void sendLoginPage(const char *err = "") {
    logWifiHeapDiag("serving login page");
    gSendAborted = false;

    // The head is a multi-KB flash constant; building it into a String needs a
    // contiguous DRAM block a fragmented heap often can't provide (the "broken
    // login" symptom). Always stream it via chunked transfer so no large
    // contiguous allocation is needed; only the small form body is built in
    // RAM. AP mode gets the lite head — the full page's stylesheet is more than
    // the heap left after SoftAP comes up.
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    if (webCfgUseLite()) sendFlash(kLiteHead); else sendFullHead();
    String html;
    html +=
        "<h2>Camillia MT</h2>"
        "<form method='POST' action='/login'>"
        "<label>Username<input name='u' type='text' autofocus autocomplete='username'></label>"
        "<label>Password<input name='p' type='password' autocomplete='current-password'></label>"
        "<button type='submit'>Login</button>";
    if (err[0]) {
        html += "<p class='err'>";
        html += err;
        html += "</p>";
    }
    html += "</form></body></html>";
    sendChunk(html);
    server.sendContent("");   // terminate chunked response
}

// ── Config page ───────────────────────────────────────────────

// Renders the config page in one of two modes. `lite` is the AP-mode variant
// ("web config lite"): same Config pane, same form, same /save handler, but the
// small CSS head and none of the other tabs — no node list, live feed, chat or
// map. Both modes stream every large constant straight from flash and flush the
// working String often, which is what lets the pane render inside AP mode's
// ~13 KB largest free block.
// ── Saved WiFi networks ───────────────────────────────────────
// Part of the Config tab's WiFi section, which sits inside the /save form. A
// form cannot nest, so the row buttons and the Add fields name their own forms
// with the form= attribute, and those forms are emitted empty after the config
// form closes (appendSavedNetworkForms()). Use posts only to /wifi-use, and
// Save All never carries the Add fields -- nor is it blocked by their
// `required`, which only applies to the form a field belongs to.
//
// Full page only. The AP page is deliberately the Config pane alone, and its
// heap has nothing spare for a per-row list.
//
// The SSID/Password fields above it still set the active network; this is for
// keeping the others around and swapping between them.
//
// None of it on the CardputerZero, where the OS owns the network (issue #162):
// there the WiFi section only names the system's connection.
#if !defined(PLATFORM_LINUX)
static void appendSavedNetworks(String &html) {
    char nSsid[64], nPass[64];
    const int savedCount = cfgSavedWifiCount();

    html += "<h4 style='margin:1.2em 0 .3em'>Saved Networks</h4>"
            "<p style='font-size:.82em;color:#888;margin:.1em 0 .6em'>"
            "Networks this device remembers. <b>Use</b> switches to one and keeps "
            "the current network in the list, so you can swap back. Switching "
            "reconnects the radio &mdash; if you are reading this page over WiFi, "
            "it will drop and you will need to reach the device on the new "
            "network.</p>";

    html += "<table style='width:100%;border-collapse:collapse;font-size:.9em'>";
    // SSIDs are free text chosen by whoever owns the access point, so every
    // one of them goes through appendAttr() — an apostrophe would otherwise
    // close the value= attribute and mangle the rest of the form, and a '<'
    // would inject markup from a network name we merely scanned.
    if (gCfg->wifiSsid[0]) {
        html += "<tr><td style='padding:.3em 0'><b>";
        appendAttr(html, gCfg->wifiSsid);
        html += "</b> <span style='color:#5a9;font-size:.85em'>&#9679; active</span></td>"
                "<td style='text-align:right;color:#888;font-size:.85em'>in use</td></tr>";
    }
    char id[16];
    for (int i = 0; i < savedCount; i++) {
        if (!cfgSavedWifiAt(i, nSsid, sizeof(nSsid), nPass, sizeof(nPass))) continue;
        if (!nSsid[0]) continue;
        html += "<tr><td style='padding:.3em 0'>";
        appendAttr(html, nSsid);
        html += "</td><td style='text-align:right;white-space:nowrap'>";
        snprintf(id, sizeof(id), "wfu%d", i);
        html += "<input type='hidden' name='ssid' form='"; html += id; html += "' value='";
        appendAttr(html, nSsid);
        html += "'><button type='submit' form='"; html += id;
        html += "' style='padding:.15em .7em'>Use</button> ";
        snprintf(id, sizeof(id), "wff%d", i);
        html += "<input type='hidden' name='ssid' form='"; html += id; html += "' value='";
        appendAttr(html, nSsid);
        html += "'><button type='submit' form='"; html += id;
        html += "' style='padding:.15em .7em;background:#c0392b'>Forget</button></td></tr>";
    }
    html += "</table>";

    if (savedCount == 0) {
        html += "<p style='font-size:.82em;color:#888;margin:.3em 0'>"
                "No other networks remembered yet.</p>";
    }

    char slots[64];
    snprintf(slots, sizeof(slots), "%d of %d slots used", savedCount, CFG_SAVED_WIFI_MAX);
    html += "<p style='font-size:.82em;color:#888;margin:.3em 0 .6em'>";
    html += slots;
    html += ". When full, the least recently added one drops off.</p>";

    html += "<label>Add network &mdash; SSID"
            "<input name='ssid' form='wfadd' type='text' maxlength='63' required></label>"
            "<label>Password (blank for an open network)"
            "<input name='pass' form='wfadd' type='password' maxlength='63'></label>"
            "<button type='submit' form='wfadd' style='margin-top:.4em'>Remember Network</button>"
            "<p style='font-size:.82em;color:#888;margin:.3em 0 1em'>"
            "Adds to the list without switching to it. The credentials are not "
            "tested until the device connects.</p>";
}

// The empty forms appendSavedNetworks() points at. Same indices, so a row's
// buttons and its form agree even when a slot is skipped as empty.
static void appendSavedNetworkForms(String &html) {
    const int savedCount = cfgSavedWifiCount();
    // Room to spare: at 160 this was a few bytes short, cut the Forget form's
    // </form> down to a lone "<" (shown at the bottom of the page) and left
    // that form open around every form after it.
    char tag[256];
    for (int i = 0; i < savedCount; i++) {
        snprintf(tag, sizeof(tag),
                 "<form id='wfu%d' method='POST' action='/wifi-use'></form>"
                 "<form id='wff%d' method='POST' action='/wifi-forget'"
                 " onsubmit=\"return confirm('Forget this network?')\"></form>",
                 i, i);
        html += tag;
    }
    html += "<form id='wfadd' method='POST' action='/wifi-add'></form>";
}
#endif  // !PLATFORM_LINUX

