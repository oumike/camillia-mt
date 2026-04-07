#include "web_config.h"
#include "node_db.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <nvs_flash.h>
#include <SD.h>
#include <math.h>

static const uint32_t kConnectTimeout  = 10000;  // ms

static const char    *kUser            = "admin";
static const char    *kPassword        = "admin";

static WebServer      server(80);
static bool           running          = false;
static bool           gOnboarding      = false;
static char           ipBuf[16]        = "";
static char           sessionToken[17] = "";   // hex token; empty = no session
static RhinoConfig   *gCfg             = nullptr;
static WebCfgSaveCb   gOnSave          = nullptr;
static volatile bool  gAnnounceReq     = false;
static char           gWifiSsid[64]    = "";
static char           gWifiPass[64]    = "";

// ── Helpers ───────────────────────────────────────────────────

static bool isLoggedIn() {
    // TODO: re-enable session auth before production
    return true;
    // if (!sessionToken[0]) return false;
    // String cookie = server.header("Cookie");
    // String needle = String("sess=") + sessionToken;
    // return cookie.indexOf(needle) >= 0;
}

static void redirect(const char *path) {
    server.sendHeader("Location", path);
    server.send(303);
}

// ── HTML helpers ──────────────────────────────────────────────

static const char kHead[] =
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Camillia MT</title>"
    "<style>"
    "body{font-family:sans-serif;max-width:540px;margin:2em auto;padding:0 1em}"
    "h2{color:#2a9d8f;margin-bottom:.2em}"
    "h3{color:#555;margin:1.2em 0 .4em;border-bottom:1px solid #ddd;padding-bottom:.2em}"
    "label{display:block;margin:.6em 0 .1em;font-size:.9em;color:#333}"
    "input[type=text],input[type=number],input[type=password],select"
      "{width:100%;padding:.35em;box-sizing:border-box;border:1px solid #ccc;border-radius:3px}"
    "input[readonly]{background:#f5f5f5;color:#666}"
    "button{margin-top:1.2em;padding:.5em 1.8em;background:#2a9d8f;color:#fff;"
           "border:none;border-radius:3px;cursor:pointer;font-size:1em}"
    ".msg{color:#2a9;margin:.5em 0}"
    ".err{color:#e63;margin:.5em 0}"
    ".logout{float:right;font-size:.9em;color:#2a9d8f;text-decoration:none}"
    ".row2{display:grid;grid-template-columns:1fr 1fr;gap:.5em}"
    ".gps-note{margin:.4em 0;font-size:.9em}"
    ".gps-note button{padding:.2em .8em;font-size:.9em;margin-left:.4em}"
    ".gps-hint{font-size:.8em;color:#888;margin:.2em 0 .6em}"
    "details{border:1px solid #ddd;border-radius:4px;margin:.8em 0;padding:0 .8em}"
    "details[open]{padding-bottom:.8em}"
    "summary{font-size:1em;font-weight:600;color:#2a9d8f;cursor:pointer;"
             "padding:.5em 0;list-style:none}"
    "summary::-webkit-details-marker{display:none}"
    "summary::before{content:'\\25B6\\00A0';font-size:.8em}"
    "details[open] summary::before{content:'\\25BC\\00A0';font-size:.8em}"
    ".ch-row{display:grid;grid-template-columns:1fr 2fr auto;gap:.4em;align-items:end;margin:.4em 0}"
    ".ch-row label{margin:0;font-size:.85em}"
    "</style></head><body>";

// ── Timezone table ────────────────────────────────────────────

struct TzOption { const char *label; const char *posix; };
static const TzOption kTzOptions[] = {
    { "UTC",                                "UTC0"                                   },
    { "US — Hawaii (UTC-10)",               "HST10"                                  },
    { "US — Alaska (UTC-9/-8)",             "AKST9AKDT,M3.2.0,M11.1.0"              },
    { "US — Pacific (UTC-8/-7)",            "PST8PDT,M3.2.0,M11.1.0"                },
    { "US — Mountain (UTC-7/-6)",           "MST7MDT,M3.2.0,M11.1.0"                },
    { "US — Arizona, no DST (UTC-7)",       "MST7"                                   },
    { "US — Central (UTC-6/-5)",            "CST6CDT,M3.2.0,M11.1.0"                },
    { "US — Eastern (UTC-5/-4)",            "EST5EDT,M3.2.0,M11.1.0"                },
    { "Canada — Atlantic (UTC-4/-3)",       "AST4ADT,M3.2.0/0,M11.1.0/0"            },
    { "Brazil — Brasilia (UTC-3)",          "BRT3BRST,M10.3.0/0,M2.3.0/0"           },
    { "Argentina (UTC-3)",                  "ART3"                                   },
    { "UK (UTC+0/+1)",                      "GMT0BST,M3.5.0/1,M10.5.0"              },
    { "Western Europe — CET (UTC+1/+2)",    "CET-1CEST,M3.5.0,M10.5.0/3"           },
    { "Eastern Europe — EET (UTC+2/+3)",    "EET-2EEST,M3.5.0/3,M10.5.0/4"         },
    { "Russia — Moscow (UTC+3)",            "MSK-3"                                  },
    { "India (UTC+5:30)",                   "IST-5:30"                               },
    { "China / Singapore (UTC+8)",          "CST-8"                                  },
    { "Japan / Korea (UTC+9)",              "JST-9"                                  },
    { "Australia — Perth (UTC+8)",          "AWST-8"                                 },
    { "Australia — Eastern (UTC+10/+11)",   "AEST-10AEDT,M10.1.0,M4.1.0/3"         },
    { "New Zealand (UTC+12/+13)",           "NZST-12NZDT,M9.5.0,M4.1.0/3"          },
};
static const int kTzCount = (int)(sizeof(kTzOptions) / sizeof(kTzOptions[0]));

// ── Login page ────────────────────────────────────────────────

static void sendLoginPage(const char *err = "") {
    String html = kHead;
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
    server.send(200, "text/html", html);
}

static const char kB64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void b64Encode(const uint8_t *in, size_t len, char *out) {
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t b = (uint32_t)in[i] << 16;
        if (i + 1 < len) b |= (uint32_t)in[i+1] << 8;
        if (i + 2 < len) b |= (uint32_t)in[i+2];
        out[o++] = kB64Chars[(b >> 18) & 0x3F];
        out[o++] = kB64Chars[(b >> 12) & 0x3F];
        out[o++] = (i + 1 < len) ? kB64Chars[(b >>  6) & 0x3F] : '=';
        out[o++] = (i + 2 < len) ? kB64Chars[ b        & 0x3F] : '=';
    }
    out[o] = '\0';
}

static int b64Decode(const char *in, uint8_t *out, int maxLen) {
    uint32_t acc = 0; int bits = 0, o = 0;
    for (const char *p = in; *p && *p != '='; p++) {
        int v;
        char c = *p;
        if      (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else continue;
        acc = (acc << 6) | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; if (o < maxLen) out[o++] = (uint8_t)((acc >> bits) & 0xFF); }
    }
    return o;
}

// ── Config page ───────────────────────────────────────────────

// Helper: flush a chunk of HTML and reset the buffer
static void sendChunk(String &html) {
    server.sendContent(html);
    html = "";
}

static void sendConfigPage(const char *msg = "") {
    if (!gCfg) { server.send(500, "text/plain", "No config"); return; }

    // Use chunked transfer to avoid building one giant String
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");

    char tmp[96];
    String html = kHead;
    html += "<h2>Camillia MT <a class='logout' href='/logout'>Logout</a></h2>";

    if (msg[0]) { html += "<p class='msg'>"; html += msg; html += "</p>"; }

    html += "<form method='POST' action='/save'>";

    // ── Node Identity ─────────────────────────────────────────
    html += "<details open><summary>Node Identity</summary>";
    html += "<label>Long Name (max 39 chars)"
            "<input name='long' type='text' maxlength='39' value='";
    html += gCfg->nodeLong; html += "'></label>";
    html += "<label>Short Name (max 4 chars)"
            "<input name='short' type='text' maxlength='4' value='";
    html += gCfg->nodeShort; html += "'></label>";
    // Node ID override (developer option — hidden for end users)
    // snprintf(tmp, sizeof(tmp), "%08lx", (unsigned long)gCfg->nodeIdOverride);
    // html += "<label>Node ID Override ...";
    html += "</details>";
    sendChunk(html);

    // ── Device ────────────────────────────────────────────────
    html += "<details open><summary>Device</summary>";
    html += "<div class='row2'>";
    html += "<label>Role<select name='role'>";
    static const struct { uint8_t v; const char *l; } kRoles[] = {
        {0,"CLIENT"},{1,"CLIENT_MUTE"},{2,"ROUTER"},{3,"ROUTER_CLIENT"},
        {4,"REPEATER"},{5,"TRACKER"},{6,"SENSOR"},{7,"TAK"},
        {8,"CLIENT_HIDDEN"},{9,"LOST_AND_FOUND"},{10,"TAK_TRACKER"}
    };
    for (int i = 0; i < 11; i++) {
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
    html += "<div class='row2'>";
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)gCfg->nodeInfoIntervalS);
    html += "<label>NodeInfo Interval (s)<input name='nodeinfo_intv' type='number' min='60' value='";
    html += tmp; html += "'></label>";
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)gCfg->posIntervalS);
    html += "<label>Position Interval (s)<input name='pos_intv' type='number' min='60' value='";
    html += tmp; html += "'></label></div>";
    // Timezone dropdown
    {
        bool tzMatched = false;
        for (int i = 0; i < kTzCount && !tzMatched; i++)
            if (strcmp(gCfg->tzDef, kTzOptions[i].posix) == 0) tzMatched = true;
        html += "<label>Timezone<select name='tzdef'>";
        if (!tzMatched && gCfg->tzDef[0]) {
            html += "<option value='"; html += gCfg->tzDef;
            html += "' selected>Custom: "; html += gCfg->tzDef; html += "</option>";
        }
        for (int i = 0; i < kTzCount; i++) {
            html += "<option value='"; html += kTzOptions[i].posix; html += "'";
            if (strcmp(gCfg->tzDef, kTzOptions[i].posix) == 0) html += " selected";
            html += ">"; html += kTzOptions[i].label; html += "</option>";
        }
        html += "</select></label>";
    }
    html += "</details>";
    sendChunk(html);

    // ── Position ──────────────────────────────────────────────
    html += "<details open><summary>Position</summary>";
    html += "<label style='display:flex;align-items:center;gap:.5em'>"
            "<input type='checkbox' name='gpsEnabled' value='1'";
    if (gCfg->gpsEnabled) html += " checked";
    html += "> GPS Enabled (L76K hardware GPS)</label>";
    html += "<p class='gps-hint'>When GPS is enabled, position is sourced from the GPS module. "
            "The manual coordinates below are used as fallback until a fix is acquired.</p>";
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
    html += "</details>";
    sendChunk(html);

    // ── Channels ──────────────────────────────────────────────
    html += "<details><summary>Channels</summary>";
    html += "<p class='gps-hint'>Key: base64 (e.g. \"AQ==\" or \"MA==\"). "
            "Hash is recomputed automatically on save.</p>";
    char b64buf[48];
    for (int i = 0; i < MESH_CHANNELS; i++) {
        const ChannelKey &ch = CHANNEL_KEYS[i];
        b64Encode(ch.key, ch.keyLen, b64buf);
        html += "<div class='ch-row'>";
        // Name
        snprintf(tmp, sizeof(tmp), "ch%d_name", i);
        html += "<label>"; snprintf(tmp+20, 20, "%d", i); html += "Ch "; html += (tmp+20);
        snprintf(tmp, sizeof(tmp), "ch%d_name", i);
        html += "<input name='"; html += tmp; html += "' type='text' maxlength='11' value='";
        html += ch.name; html += "'></label>";
        // Key
        snprintf(tmp, sizeof(tmp), "ch%d_key", i);
        html += "<label>Key<input name='"; html += tmp;
        html += "' type='text' value='"; html += b64buf; html += "'></label>";
        // Role
        snprintf(tmp, sizeof(tmp), "ch%d_role", i);
        html += "<label>Role<select name='"; html += tmp; html += "'>";
        const char *roles[] = {"PRIMARY","SECONDARY","DISABLED"};
        for (int r = 0; r < 3; r++) {
            snprintf(tmp, sizeof(tmp), "%d", r);
            html += "<option value='"; html += tmp; html += "'";
            if (ch.role == r) html += " selected";
            html += ">"; html += roles[r]; html += "</option>";
        }
        html += "</select></label>";
        html += "</div>";
    }
    html += "</details>";
    sendChunk(html);

    // ── LoRa Radio ────────────────────────────────────────────
    html += "<details open><summary>LoRa Radio</summary>";
    html += "<div class='row2'>"
            "<label>Region<select name='region' id='sel-rgn'>"
            "<option value='US'>US (902&ndash;928 MHz)</option>"
            "<option value='EU_433'>EU 433 (433&ndash;434 MHz)</option>"
            "<option value='EU_868'>EU 868 (869.4&ndash;869.65 MHz)</option>"
            "<option value='CN'>CN (470&ndash;510 MHz)</option>"
            "<option value='JP'>JP (920.5&ndash;923.5 MHz)</option>"
            "<option value='ANZ'>ANZ (915&ndash;928 MHz)</option>"
            "<option value='ANZ_433'>ANZ 433 (433&ndash;434.8 MHz)</option>"
            "<option value='RU'>RU (868.7&ndash;869.2 MHz)</option>"
            "<option value='KR'>KR (920&ndash;923 MHz)</option>"
            "<option value='TW'>TW (920&ndash;925 MHz)</option>"
            "<option value='IN'>IN (865&ndash;867 MHz)</option>"
            "<option value='NZ_865'>NZ 865 (864&ndash;868 MHz)</option>"
            "<option value='TH'>TH (920&ndash;925 MHz)</option>"
            "<option value='UA_433'>UA 433 (433&ndash;434.7 MHz)</option>"
            "<option value='UA_868'>UA 868 (868&ndash;868.6 MHz)</option>"
            "<option value='MY_433'>MY 433 (433&ndash;435 MHz)</option>"
            "<option value='MY_919'>MY 919 (919&ndash;924 MHz)</option>"
            "<option value='SG_923'>SG 923 (917&ndash;925 MHz)</option>"
            "<option value='PH_433'>PH 433 (433&ndash;434.7 MHz)</option>"
            "<option value='PH_868'>PH 868 (868&ndash;869.4 MHz)</option>"
            "<option value='PH_915'>PH 915 (915&ndash;918 MHz)</option>"
            "<option value='KZ_433'>KZ 433 (433&ndash;434.8 MHz)</option>"
            "<option value='KZ_863'>KZ 863 (863&ndash;868 MHz)</option>"
            "<option value='NP_865'>NP 865 (865&ndash;868 MHz)</option>"
            "<option value='BR_902'>BR 902 (902&ndash;907.5 MHz)</option>"
            "<option value='LORA_24'>LoRa 2.4 GHz (2400&ndash;2483.5 MHz)</option>"
            "</select></label>"
            "<label>Modem Preset<select id='sel-pst'>"
            "<option value='Long Fast'>Long Fast (default)</option>"
            "<option value='Long Moderate'>Long Moderate</option>"
            "<option value='Long Slow'>Long Slow</option>"
            "<option value='Long Turbo'>Long Turbo</option>"
            "<option value='Medium Fast'>Medium Fast</option>"
            "<option value='Medium Slow'>Medium Slow</option>"
            "<option value='Short Fast'>Short Fast</option>"
            "<option value='Short Slow'>Short Slow</option>"
            "<option value='Short Turbo'>Short Turbo</option>"
            "</select></label></div>";
    html += "<script>document.getElementById('sel-rgn').value='";
    html += gCfg->region; html += "';</script>";
    html += "<button type='button' onclick='applyPreset()'"
            " style='margin-top:.5em;background:#555'>Apply Preset to fields below</button>"
            "<p class='gps-hint'>Fills frequency, BW, SF, CR and TX power from preset.</p>"
            "<script>"
            "var R={'US':{f:906.875,p:22},'EU_433':{f:433.5,p:10},'EU_868':{f:869.525,p:22},"
            "'CN':{f:490.0,p:19},'JP':{f:922.0,p:13},'ANZ':{f:921.5,p:22},'ANZ_433':{f:433.92,p:14},"
            "'RU':{f:868.95,p:20},'KR':{f:921.5,p:22},'TW':{f:922.5,p:22},'IN':{f:866.0,p:22},"
            "'NZ_865':{f:866.0,p:22},'TH':{f:922.5,p:16},'UA_433':{f:433.85,p:10},'UA_868':{f:868.3,p:14},"
            "'MY_433':{f:434.0,p:20},'MY_919':{f:921.5,p:22},'SG_923':{f:921.0,p:20},'PH_433':{f:433.85,p:10},"
            "'PH_868':{f:868.7,p:14},'PH_915':{f:916.5,p:22},'KZ_433':{f:433.925,p:10},'KZ_863':{f:865.5,p:22},"
            "'NP_865':{f:866.5,p:22},'BR_902':{f:904.75,p:22},'LORA_24':{f:2441.75,p:10}};"
            "var P={'Long Fast':{bw:250,sf:11,cr:5},'Long Moderate':{bw:125,sf:11,cr:8},"
            "'Long Slow':{bw:125,sf:12,cr:8},'Long Turbo':{bw:500,sf:11,cr:8},"
            "'Medium Fast':{bw:250,sf:9,cr:5},'Medium Slow':{bw:250,sf:10,cr:5},"
            "'Short Fast':{bw:250,sf:7,cr:5},'Short Slow':{bw:250,sf:8,cr:5},'Short Turbo':{bw:500,sf:7,cr:5}};"
            "function applyPreset(){"
              "var r=R[document.getElementById('sel-rgn').value];"
              "var p=P[document.getElementById('sel-pst').value];"
              "if(!r||!p)return;"
              "document.querySelector('[name=freq]').value=r.f.toFixed(3);"
              "document.querySelector('[name=bw]').value=p.bw;"
              "document.querySelector('[name=sf]').value=p.sf;"
              "document.querySelector('[name=cr]').value=p.cr;"
              "document.querySelector('[name=pwr]').value=r.p;"
            "}"
            "</script>";
    html += "<div class='row2'>";
    snprintf(tmp, sizeof(tmp), "%.3f", gCfg->loraFreq);
    html += "<label>Frequency (MHz)<input name='freq' type='number' step='0.001' min='150' max='2500' value='";
    html += tmp; html += "'></label>";
    html += "<label>Bandwidth (kHz)<select name='bw'>";
    const float bwOpts[] = {125.0f,250.0f,500.0f};
    const char *bwLabels[] = {"125 kHz","250 kHz","500 kHz"};
    for (int i = 0; i < 3; i++) {
        snprintf(tmp, sizeof(tmp), "%.0f", bwOpts[i]);
        html += "<option value='"; html += tmp; html += "'";
        if (fabsf(gCfg->loraBw - bwOpts[i]) < 0.1f) html += " selected";
        html += ">"; html += bwLabels[i]; html += "</option>";
    }
    html += "</select></label></div><div class='row2'>";
    html += "<label>Spreading Factor<select name='sf'>";
    for (int sf = 7; sf <= 12; sf++) {
        snprintf(tmp, sizeof(tmp), "%d", sf);
        html += "<option value='"; html += tmp; html += "'";
        if (gCfg->loraSf == sf) html += " selected";
        html += ">SF"; html += tmp; html += "</option>";
    }
    html += "</select></label>";
    html += "<label>Coding Rate<select name='cr'>";
    for (int cr = 5; cr <= 8; cr++) {
        snprintf(tmp, sizeof(tmp), "%d", cr);
        html += "<option value='"; html += tmp; html += "'";
        if (gCfg->loraCr == cr) html += " selected";
        html += ">4/"; html += tmp; html += "</option>";
    }
    html += "</select></label></div><div class='row2'>";
    snprintf(tmp, sizeof(tmp), "%d", gCfg->loraPower);
    html += "<label>TX Power (dBm, 1&ndash;22)<input name='pwr' type='number' min='1' max='22' value='";
    html += tmp; html += "'></label>";
    snprintf(tmp, sizeof(tmp), "%d", gCfg->loraHopLimit);
    html += "<label>Hop Limit (1&ndash;7)<input name='hop' type='number' min='1' max='7' value='";
    html += tmp; html += "'></label></div>";
    html += "<label style='display:flex;align-items:center;gap:.5em'>"
            "<input type='checkbox' name='ok_to_mqtt' value='1'";
    if (gCfg->okToMqtt) html += " checked";
    html += "> OK to MQTT &mdash; allow MQTT-connected nodes to forward your packets upstream</label>";
    html += "<label style='display:flex;align-items:center;gap:.5em'>"
            "<input type='checkbox' name='ignore_mqtt' value='1'";
    if (gCfg->ignoreMqtt) html += " checked";
    html += "> Ignore MQTT &mdash; drop received packets that arrived via MQTT</label>";
    html += "</details>";
    sendChunk(html);

    // ── Bluetooth ─────────────────────────────────────────────
    html += "<details><summary>Bluetooth</summary>";
    html += "<div class='row2'>";
    html += "<label>Enabled<select name='bt_enabled'>"
            "<option value='1'"; if ( gCfg->btEnabled) html += " selected"; html += ">Yes</option>"
            "<option value='0'"; if (!gCfg->btEnabled) html += " selected"; html += ">No</option>"
            "</select></label>";
    html += "<label>Mode<select name='bt_mode'>";
    static const char *btModes[] = {"RANDOM_PIN","FIXED_PIN","NO_PIN"};
    for (int i = 0; i < 3; i++) {
        snprintf(tmp, sizeof(tmp), "%d", i);
        html += "<option value='"; html += tmp; html += "'";
        if (gCfg->btMode == i) html += " selected";
        html += ">"; html += btModes[i]; html += "</option>";
    }
    html += "</select></label></div>";
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)gCfg->btFixedPin);
    html += "<label>Fixed PIN (when mode = FIXED_PIN)"
            "<input name='bt_pin' type='number' min='0' max='999999' value='";
    html += tmp; html += "' style='max-width:150px'></label>";
    html += "</details>";
    sendChunk(html);

    // Network section removed (MQTT not used; strictly LoRa)

    // ── Display ───────────────────────────────────────────────
    html += "<details><summary>Display</summary>";
    html += "<div class='row2'>";
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)gCfg->screenOnSecs);
    html += "<label>Screen Timeout (s)<input name='screen_on' type='number' min='0' value='";
    html += tmp; html += "'></label>";
    html += "<label>Units<select name='disp_units'>"
            "<option value='0'"; if (!gCfg->displayUnits) html += " selected"; html += ">Metric</option>"
            "<option value='1'"; if ( gCfg->displayUnits) html += " selected"; html += ">Imperial</option>"
            "</select></label></div>";
    html += "<div class='row2'>";
    html += "<label>Compass North Top<select name='compass_north'>"
            "<option value='1'"; if ( gCfg->compassNorthTop) html += " selected"; html += ">Yes</option>"
            "<option value='0'"; if (!gCfg->compassNorthTop) html += " selected"; html += ">No</option>"
            "</select></label>";
    html += "<label>Flip Screen<select name='flip_screen'>"
            "<option value='1'"; if ( gCfg->flipScreen) html += " selected"; html += ">Yes</option>"
            "<option value='0'"; if (!gCfg->flipScreen) html += " selected"; html += ">No</option>"
            "</select></label></div>";
    html += "<label>Chat Line Spacing (reboot required)<select name='chat_space'>"
            "<option value='0'"; if (gCfg->chatSpacing == 0) html += " selected"; html += ">Tight</option>"
            "<option value='1'"; if (gCfg->chatSpacing == 1) html += " selected"; html += ">Normal</option>"
            "<option value='2'"; if (gCfg->chatSpacing == 2) html += " selected"; html += ">Loose</option>"
            "</select></label>";
    html += "</details>";
    sendChunk(html);

    // ── Power ─────────────────────────────────────────────────
    html += "<details><summary>Power</summary>";
    html += "<label>Power Saving<select name='pwr_saving'>"
            "<option value='1'"; if ( gCfg->isPowerSaving) html += " selected"; html += ">Enabled</option>"
            "<option value='0'"; if (!gCfg->isPowerSaving) html += " selected"; html += ">Disabled</option>"
            "</select></label>";
    html += "<div class='row2'>";
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)gCfg->lsSecs);
    html += "<label>Light Sleep After (s)<input name='ls_secs' type='number' min='0' value='";
    html += tmp; html += "'></label>";
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)gCfg->minWakeSecs);
    html += "<label>Min Wake (s)<input name='min_wake' type='number' min='0' value='";
    html += tmp; html += "'></label></div>";
    html += "</details>";
    sendChunk(html);

    // ── Modules ───────────────────────────────────────────────
    html += "<details><summary>Modules</summary>";
    // Telemetry
    html += "<h3 style='font-size:.95em;margin:.8em 0 .3em'>Telemetry</h3>";
    html += "<div class='row2'>";
    html += "<label>Device Telemetry<select name='tel_dev_en'>"
            "<option value='1'"; if ( gCfg->telDeviceEnabled) html += " selected"; html += ">Enabled</option>"
            "<option value='0'"; if (!gCfg->telDeviceEnabled) html += " selected"; html += ">Disabled</option>"
            "</select></label>";
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)gCfg->telDeviceIntervalS);
    html += "<label>Device Interval (s)<input name='tel_dev_intv' type='number' min='60' value='";
    html += tmp; html += "'></label></div>";
    html += "<div class='row2'>";
    html += "<label>Environment Telemetry<select name='tel_env_en'>"
            "<option value='1'"; if ( gCfg->telEnvEnabled) html += " selected"; html += ">Enabled</option>"
            "<option value='0'"; if (!gCfg->telEnvEnabled) html += " selected"; html += ">Disabled</option>"
            "</select></label>";
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)gCfg->telEnvIntervalS);
    html += "<label>Env Interval (s)<input name='tel_env_intv' type='number' min='60' value='";
    html += tmp; html += "'></label></div>";
    // Canned Messages
    html += "<h3 style='font-size:.95em;margin:.8em 0 .3em'>Canned Messages</h3>";
    html += "<label>Enabled<select name='canned_en'>"
            "<option value='1'"; if ( gCfg->cannedEnabled) html += " selected"; html += ">Yes</option>"
            "<option value='0'"; if (!gCfg->cannedEnabled) html += " selected"; html += ">No</option>"
            "</select></label>";
    html += "<label>Messages (pipe-separated, e.g. Hi|Bye|Yes|No)"
            "<input name='canned_msgs' type='text' maxlength='199' value='";
    html += gCfg->cannedMessages; html += "'></label>";
    html += "</details>";
    sendChunk(html);

    // WiFi
    html += "<h3 style='font-size:.95em;margin:.8em 0 .3em'>WiFi</h3>"
            "<label>SSID<input name='wifi_ssid' type='text' maxlength='63' value='";
    html += gWifiSsid;
    html += "'></label>"
            "<label>Password<input name='wifi_pass' type='password' maxlength='63' value='";
    html += gWifiPass;
    html += "'></label>";

    html += "<button type='submit' style='width:100%;margin-top:1.5em'>Save All</button></form>";
    sendChunk(html);

    // ── Backup & Restore ──────────────────────────────────────
    html +=
        "<h3 style='margin-top:1.5em'>Diagnostics</h3>"
        "<form method='POST' action='/announce'>"
        "<button type='submit' style='background:#e07b00'>"
        "&#128225; Send NODEINFO Broadcast</button>"
        "</form>"
        "<p style='font-size:.82em;color:#888;margin:.3em 0 1em'>"
        "Forces immediate re-announcement to the mesh (NODEINFO + position).</p>"
        "<h3 style='margin-top:.5em'>Backup &amp; Restore</h3>"
        "<p><a href='/export' download='config.yaml'"
        " style='display:inline-block;padding:.4em 1.2em;background:#2a9d8f;"
        "color:#fff;border-radius:3px;text-decoration:none;font-size:.95em'>"
        "&#11015; Export config.yaml</a></p>"
        "<form method='POST' action='/import' enctype='multipart/form-data'"
        " style='margin-top:.6em'>"
        "<label>Import a YAML config file.</label>  "
        "<input type='file' name='f' accept='.yaml,.yml'"
        " style='margin-top:.3em'><br />"
        "<button type='submit'>&#11014; Upload &amp; Apply</button>"
        "</form>"
        "<h3 style='margin-top:1.5em;color:#c0392b'>Danger Zone</h3>"
        "<form method='POST' action='/clear-nodes'"
        " onsubmit=\"return confirm('This will clear all discovered nodes and reboot. Continue?')\">"
        "<button type='submit' style='background:#c0392b'>"
        "Clear Nodes</button>"
        "</form>"
        "<p style='font-size:.82em;color:#888;margin:.3em 0 .6em'>"
        "Clears the persisted node database and reboots.</p>"
        "<form method='POST' action='/factory-reset'"
        " onsubmit=\"return confirm('This will erase ALL settings and reboot the device. Continue?')\">"
        "<button type='submit' style='background:#c0392b'>"
        "Factory Reset</button>"
        "</form>"
        "<p style='font-size:.82em;color:#888;margin:.3em 0 1em'>"
        "Erases all NVS configuration (node identity, channels, keys) and reboots."
        " The device will behave as if freshly flashed.</p>";

    html += "</body></html>";
    server.sendContent(html);
    server.sendContent("");   // empty chunk signals end of response
}

// ── Onboarding (WiFi setup) ───────────────────────────────────

static void sendOnboardingPage(const char *err = "") {
    String html = kHead;
    html +=
        "<h2>Camillia-MT Setup</h2>"
        "<p style='color:#555;font-size:.95em'>"
        "Enter your WiFi network name and password. The device will connect "
        "and display its IP address on screen.</p>"
        "<form method='POST' action='/onboard'>"
        "<label>WiFi Name (SSID)"
        "<input name='ssid' type='text' autofocus autocomplete='off' "
        "placeholder='MyNetwork'></label>"
        "<label>WiFi Password"
        "<input name='pass' type='password' autocomplete='off'></label>"
        "<button type='submit'>Connect</button>";
    if (err[0]) {
        html += "<p class='err'>";
        html += err;
        html += "</p>";
    }
    html += "</form></body></html>";
    server.send(200, "text/html", html);
}

static void handleGetOnboard() {
    sendOnboardingPage();
}

static void handlePostOnboard() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    ssid.trim();
    if (ssid.isEmpty()) {
        sendOnboardingPage("WiFi name cannot be empty.");
        return;
    }
    Preferences prefs;
    prefs.begin("camillia", false);
    prefs.putString("wifiSsid", ssid);
    prefs.putString("wifiPass", pass);
    prefs.end();

    String html = kHead;
    html +=
        "<h2>WiFi Saved</h2>"
        "<p>The device is rebooting and will connect to <b>";
    html += ssid;
    html +=
        "</b>.</p>"
        "<p>Once connected, the IP address will appear on the device screen. "
        "Open that address in your browser to complete setup.</p>"
        "</body></html>";
    server.send(200, "text/html", html);
    server.stop();
    delay(500);
    ESP.restart();
}

// ── Route handlers ────────────────────────────────────────────

static void handleGetRoot() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    sendConfigPage();
}

static void handleGetLogin() {
    if (isLoggedIn()) { redirect("/"); return; }
    sendLoginPage();
}

static void handlePostLogin() {
    String u = server.arg("u");
    String p = server.arg("p");
    if (u == kUser && p == kPassword) {
        uint32_t r1 = esp_random(), r2 = esp_random();
        snprintf(sessionToken, sizeof(sessionToken), "%08x%08x", r1, r2);
        String cookie = String("sess=") + sessionToken + "; Path=/; HttpOnly";
        server.sendHeader("Set-Cookie", cookie);
        redirect("/");
    } else {
        sendLoginPage("Invalid username or password.");
    }
}

static void handlePostSave() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    if (!gCfg) { redirect("/"); return; }

    // Node identity
    String lng  = server.arg("long");
    String shrt = server.arg("short");
    strncpy(gCfg->nodeLong,  lng.c_str(),  sizeof(gCfg->nodeLong)  - 1);
    strncpy(gCfg->nodeShort, shrt.c_str(), sizeof(gCfg->nodeShort) - 1);
    gCfg->nodeLong[sizeof(gCfg->nodeLong)   - 1] = '\0';
    gCfg->nodeShort[sizeof(gCfg->nodeShort) - 1] = '\0';
    // Node ID override hidden for end users; preserve existing value
    // String ovr = server.arg("node_id_ovr");
    // gCfg->nodeIdOverride = (ovr.length() > 0) ? (uint32_t)strtoul(ovr.c_str(), nullptr, 16) : 0;

    // Device
    gCfg->deviceRole        = (uint8_t)constrain(server.arg("role").toInt(),        0, 10);
    gCfg->rebroadcastMode   = (uint8_t)constrain(server.arg("rebroadcast").toInt(), 0,  4);
    gCfg->nodeInfoIntervalS = (uint32_t)max((long)60, server.arg("nodeinfo_intv").toInt());
    gCfg->posIntervalS      = (uint32_t)max((long)60, server.arg("pos_intv").toInt());
    strncpy(gCfg->tzDef, server.arg("tzdef").c_str(), sizeof(gCfg->tzDef) - 1);

    // Position
    gCfg->gpsEnabled = (server.arg("gpsEnabled") == "1");
    gCfg->latI = (int32_t)(server.arg("lat").toFloat() * 1e7f);
    gCfg->lonI = (int32_t)(server.arg("lon").toFloat() * 1e7f);
    gCfg->alt  = (int32_t)server.arg("alt").toInt();

    // Channels
    for (int i = 0; i < MESH_CHANNELS; i++) {
        char field[16];
        snprintf(field, sizeof(field), "ch%d_name", i);
        String nm = server.arg(field);
        Serial.printf("[cfg] ch%d: name='%s' key='%s' role='%s'\n", i,
                      nm.c_str(), server.arg(String("ch") + i + "_key").c_str(),
                      server.arg(String("ch") + i + "_role").c_str());
        if (nm.length() > 0 && nm.length() < sizeof(CHANNEL_KEYS[i].name_buf)) {
            strncpy(CHANNEL_KEYS[i].name_buf, nm.c_str(), sizeof(CHANNEL_KEYS[i].name_buf) - 1);
            CHANNEL_KEYS[i].name_buf[sizeof(CHANNEL_KEYS[i].name_buf) - 1] = '\0';
            CHANNEL_KEYS[i].name = CHANNEL_KEYS[i].name_buf;
        }
        snprintf(field, sizeof(field), "ch%d_key", i);
        String kh = server.arg(field);
        kh.trim();
        if (kh.length() >= 2) {
            uint8_t kbuf[32];
            int klen = b64Decode(kh.c_str(), kbuf, 32);
            if (klen > 0) { memcpy(CHANNEL_KEYS[i].key, kbuf, klen); CHANNEL_KEYS[i].keyLen = (uint8_t)klen; }
        }
        snprintf(field, sizeof(field), "ch%d_role", i);
        CHANNEL_KEYS[i].role = (uint8_t)constrain(server.arg(field).toInt(), 0, 2);
        // Recompute on-air hash from current name + key
        const char *nm2 = CHANNEL_KEYS[i].name_buf[0] ? CHANNEL_KEYS[i].name_buf : CHANNEL_KEYS[i].name;
        CHANNEL_KEYS[i].hash = computeChannelHash(nm2, CHANNEL_KEYS[i].key, CHANNEL_KEYS[i].keyLen);
    }

    // Region
    String rgn = server.arg("region");
    if (rgn.length() > 0 && rgn.length() < sizeof(gCfg->region))
        strncpy(gCfg->region, rgn.c_str(), sizeof(gCfg->region) - 1);

    // LoRa
    gCfg->okToMqtt    = (server.arg("ok_to_mqtt")   == "1");
    gCfg->ignoreMqtt  = (server.arg("ignore_mqtt")  == "1");
    gCfg->loraFreq     = server.arg("freq").toFloat();
    gCfg->loraBw       = server.arg("bw").toFloat();
    gCfg->loraSf       = (uint8_t)constrain(server.arg("sf").toInt(),  7, 12);
    gCfg->loraCr       = (uint8_t)constrain(server.arg("cr").toInt(),  5,  8);
    gCfg->loraPower    = (uint8_t)constrain(server.arg("pwr").toInt(), 1, 22);
    gCfg->loraHopLimit = (uint8_t)constrain(server.arg("hop").toInt(), 1,  7);

    // Bluetooth
    gCfg->btEnabled  = server.arg("bt_enabled").toInt() != 0;
    gCfg->btMode     = (uint8_t)constrain(server.arg("bt_mode").toInt(), 0, 2);
    gCfg->btFixedPin = (uint32_t)server.arg("bt_pin").toInt();

    // Network
    // NTP server removed from UI; preserve existing value
    // strncpy(gCfg->ntpServer, server.arg("ntp_server").c_str(), sizeof(gCfg->ntpServer) - 1);
    // MQTT removed; preserve existing values in config struct

    // Display
    gCfg->screenOnSecs    = (uint32_t)server.arg("screen_on").toInt();
    gCfg->displayUnits    = server.arg("disp_units").toInt() != 0 ? 1 : 0;
    gCfg->compassNorthTop = server.arg("compass_north").toInt() != 0;
    gCfg->flipScreen      = server.arg("flip_screen").toInt() != 0;
    gCfg->chatSpacing     = (uint8_t)constrain(server.arg("chat_space").toInt(), 0, 2);

    // Power
    gCfg->isPowerSaving = server.arg("pwr_saving").toInt() != 0;
    gCfg->lsSecs        = (uint32_t)server.arg("ls_secs").toInt();
    gCfg->minWakeSecs   = (uint32_t)server.arg("min_wake").toInt();

    // Modules
    gCfg->telDeviceEnabled   = server.arg("tel_dev_en").toInt() != 0;
    gCfg->telDeviceIntervalS = (uint32_t)max((long)60, server.arg("tel_dev_intv").toInt());
    gCfg->telEnvEnabled      = server.arg("tel_env_en").toInt() != 0;
    gCfg->telEnvIntervalS    = (uint32_t)max((long)60, server.arg("tel_env_intv").toInt());
    gCfg->cannedEnabled      = server.arg("canned_en").toInt() != 0;
    strncpy(gCfg->cannedMessages, server.arg("canned_msgs").c_str(), sizeof(gCfg->cannedMessages) - 1);

    // WiFi credentials — save directly to NVS (not part of gCfg)
    {
        String ssid = server.arg("wifi_ssid");
        String pass = server.arg("wifi_pass");
        ssid.trim(); pass.trim();
        if (ssid.length() > 0) {
            strncpy(gWifiSsid, ssid.c_str(), sizeof(gWifiSsid) - 1);
            strncpy(gWifiPass, pass.c_str(), sizeof(gWifiPass) - 1);
        }
    }

    if (gOnSave) gOnSave();

    server.send(200, "text/html",
        "<!DOCTYPE html><html><body style='font-family:sans-serif;padding:2em'>"
        "<h2>Saved.</h2>"
        "<p>The device is rebooting. Reconnect in a few seconds.</p>"
        "</body></html>");
    server.stop();
    delay(500);
    ESP.restart();
}

// ── Announce ─────────────────────────────────────────────────

static void handlePostAnnounce() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    gAnnounceReq = true;
    sendConfigPage("NODEINFO broadcast queued.");
}

// ── Clear Nodes ───────────────────────────────────────────────

static void handlePostClearNodes() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    Nodes.clearPersisted();
    server.send(200, "text/html",
        "<!DOCTYPE html><html><body style='font-family:sans-serif;padding:2em'>"
        "<h2>Node database cleared.</h2>"
        "<p>All discovered nodes erased. The device is rebooting now.</p>"
        "</body></html>");
    server.stop();
    delay(500);
    ESP.restart();
}

// ── Factory Reset ─────────────────────────────────────────────

static void handlePostFactoryReset() {
    if (!isLoggedIn()) { redirect("/login"); return; }

    // Erase the entire NVS partition
    nvs_flash_erase();
    nvs_flash_init();

    // Delete saved DM conversations from SD card
    {
        File dir = SD.open("/camillia/dms");
        if (dir && dir.isDirectory()) {
            File f = dir.openNextFile();
            while (f) {
                String fp = String("/camillia/dms/") + f.name();
                f.close();
                SD.remove(fp.c_str());
                f = dir.openNextFile();
            }
            dir.close();
            SD.rmdir("/camillia/dms");
        }
    }

    server.send(200, "text/html",
        "<!DOCTYPE html><html><body style='font-family:sans-serif;padding:2em'>"
        "<h2>Factory reset complete.</h2>"
        "<p>All settings erased. The device is rebooting now.</p>"
        "</body></html>");
    server.stop();
    delay(500);
    ESP.restart();
}

// ── Export / Import ───────────────────────────────────────────

static void handleGetExport() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    if (!gCfg) { server.send(500, "text/plain", "No config"); return; }
    String yaml;
    cfgToYaml(*gCfg, yaml);
    server.sendHeader("Content-Disposition", "attachment; filename=\"config.yaml\"");
    server.send(200, "text/x-yaml", yaml);
}

static char   importBuf[8192];
static size_t importLen = 0;
static bool   importOk  = false;

static void handleImportUpload() {
    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        importLen = 0;
        importOk  = false;
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        size_t space = sizeof(importBuf) - importLen - 1;
        size_t chunk = upload.currentSize < space ? upload.currentSize : space;
        memcpy(importBuf + importLen, upload.buf, chunk);
        importLen += chunk;
    } else if (upload.status == UPLOAD_FILE_END) {
        importBuf[importLen] = '\0';
        importOk = true;
    }
}

static void handleImportDone() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    if (!gCfg) { redirect("/"); return; }
    if (!importOk || importLen == 0) {
        sendConfigPage("Import failed: no data received.");
        return;
    }
    if (!cfgImportFromBuf(importBuf, importLen, *gCfg)) {
        sendConfigPage("Import failed: parse error.");
        return;
    }
    if (gOnSave) gOnSave();
    sendConfigPage("Config imported and saved.");
}

static void handleGetLogout() {
    sessionToken[0] = '\0';
    server.sendHeader("Set-Cookie", "sess=; Path=/; Max-Age=0");
    redirect("/login");
}

// ── Public API ────────────────────────────────────────────────

bool webCfgBegin(RhinoConfig *cfg, WebCfgSaveCb onSave) {
    if (running) return true;

    gCfg    = cfg;
    gOnSave = onSave;

    // Load saved WiFi credentials
    Preferences prefs;
    prefs.begin("camillia", true);
    String savedSsid = prefs.getString("wifiSsid", "");
    String savedPass = prefs.getString("wifiPass", "");
    prefs.end();
    strncpy(gWifiSsid, savedSsid.c_str(), sizeof(gWifiSsid) - 1);
    strncpy(gWifiPass, savedPass.c_str(), sizeof(gWifiPass) - 1);

    const char *headers[] = {"Cookie"};
    server.collectHeaders(headers, 1);

    if (savedSsid.isEmpty()) {
        // ── Onboarding mode: create an AP with full config ────
        gOnboarding = true;
        WiFi.mode(WIFI_AP);
        WiFi.softAP("camillia-mt");
        delay(100);
        WiFi.softAPIP().toString().toCharArray(ipBuf, sizeof(ipBuf));

        // Serve both the onboarding WiFi page and full config
        server.on("/setup",   HTTP_GET,  handleGetOnboard);
        server.on("/onboard", HTTP_POST, handlePostOnboard);
        server.on("/",        HTTP_GET,  handleGetRoot);
        server.on("/login",   HTTP_GET,  handleGetLogin);
        server.on("/login",   HTTP_POST, handlePostLogin);
        server.on("/save",    HTTP_POST, handlePostSave);
        server.on("/logout",  HTTP_GET,  handleGetLogout);
        server.on("/export",  HTTP_GET,  handleGetExport);
        server.on("/announce",HTTP_POST, handlePostAnnounce);
        server.on("/import",        HTTP_POST, handleImportDone, handleImportUpload);
        server.on("/clear-nodes",   HTTP_POST, handlePostClearNodes);
        server.on("/factory-reset", HTTP_POST, handlePostFactoryReset);
        server.begin();
        running = true;
        Serial.printf("[web] onboarding AP at http://%s/\n", ipBuf);
        return true;
    }

    // ── Normal mode: connect to saved WiFi ────────────────────
    gOnboarding = false;
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSsid.c_str(), savedPass.c_str());
    Serial.printf("[web] connecting to \"%s\" ...\n", savedSsid.c_str());

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start >= kConnectTimeout) {
            Serial.println("[web] STA connect timeout — falling back to AP mode");
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            delay(100);
            WiFi.mode(WIFI_AP);
            delay(100);
            WiFi.softAP("camillia-mt");
            delay(500);
            WiFi.softAPIP().toString().toCharArray(ipBuf, sizeof(ipBuf));

            server.on("/",        HTTP_GET,  handleGetRoot);
            server.on("/login",   HTTP_GET,  handleGetLogin);
            server.on("/login",   HTTP_POST, handlePostLogin);
            server.on("/save",    HTTP_POST, handlePostSave);
            server.on("/logout",  HTTP_GET,  handleGetLogout);
            server.on("/export",  HTTP_GET,  handleGetExport);
            server.on("/announce",HTTP_POST, handlePostAnnounce);
            server.on("/import",        HTTP_POST, handleImportDone, handleImportUpload);
            server.on("/clear-nodes",   HTTP_POST, handlePostClearNodes);
            server.on("/factory-reset", HTTP_POST, handlePostFactoryReset);
            server.begin();
            running = true;
            Serial.printf("[web] AP fallback at http://%s/\n", ipBuf);
            return true;
        }
        delay(100);
    }

    WiFi.localIP().toString().toCharArray(ipBuf, sizeof(ipBuf));

    server.on("/",        HTTP_GET,  handleGetRoot);
    server.on("/login",   HTTP_GET,  handleGetLogin);
    server.on("/login",   HTTP_POST, handlePostLogin);
    server.on("/save",    HTTP_POST, handlePostSave);
    server.on("/logout",  HTTP_GET,  handleGetLogout);
    server.on("/export",  HTTP_GET,  handleGetExport);
    server.on("/announce",HTTP_POST, handlePostAnnounce);
    server.on("/import",        HTTP_POST, handleImportDone, handleImportUpload);
    server.on("/clear-nodes",   HTTP_POST, handlePostClearNodes);
    server.on("/factory-reset", HTTP_POST, handlePostFactoryReset);
    server.begin();
    running = true;
    Serial.printf("[web] ready at http://%s/\n", ipBuf);
    return true;
}

void webCfgEnd() {
    if (!running) return;
    sessionToken[0] = '\0';
    server.stop();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    ipBuf[0]    = '\0';
    gCfg        = nullptr;
    gOnSave     = nullptr;
    running     = false;
    gOnboarding = false;
    Serial.println("[web] stopped");
}

bool webCfgIsOnboarding() { return gOnboarding; }
const char *webCfgWifiSsid() { return gWifiSsid; }
const char *webCfgWifiPass() { return gWifiPass; }

void webCfgLoop() {
    if (running) server.handleClient();
}

bool webCfgRunning() { return running; }
const char *webCfgIP() { return ipBuf; }

bool webCfgAnnounceRequested() {
    if (!gAnnounceReq) return false;
    gAnnounceReq = false;
    return true;
}
