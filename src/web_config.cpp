#include "web_config.h"
#include <WiFi.h>
#include <WebServer.h>
#include <math.h>

static const char    *kSSID            = "rhinohome";
static const char    *kPass            = "fishfood is smelly";
static const uint32_t kConnectTimeout  = 10000;  // ms

static const char    *kUser            = "admin";
static const char    *kPassword        = "admin";

static WebServer      server(80);
static bool           running          = false;
static char           ipBuf[16]        = "";
static char           sessionToken[17] = "";   // hex token; empty = no session
static RhinoConfig   *gCfg             = nullptr;
static WebCfgSaveCb   gOnSave          = nullptr;

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
    "</style></head><body>";

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

// ── Config page ───────────────────────────────────────────────

static void sendConfigPage(const char *msg = "") {
    if (!gCfg) { server.send(500, "text/plain", "No config"); return; }

    char tmp[32];
    String html = kHead;

    html += "<h2>Camillia MT <a class='logout' href='/logout'>Logout</a></h2>";

    if (msg[0]) {
        html += "<p class='msg'>";
        html += msg;
        html += "</p>";
    }

    html += "<form method='POST' action='/save'>";

    // ── Node Identity ─────────────────────────────────────────
    html += "<h3>Node Identity</h3>";
    html += "<label>Long Name (max 39 chars)"
            "<input name='long' type='text' maxlength='39' value='";
    html += gCfg->nodeLong;
    html += "'></label>";
    html += "<label>Short Name (max 4 chars)"
            "<input name='short' type='text' maxlength='4' value='";
    html += gCfg->nodeShort;
    html += "'></label>";

    // ── Position ──────────────────────────────────────────────
    html += "<h3>Position</h3>";

#if HAS_GPS
    html += "<p class='gps-note'>&#128satellites; Coordinates sourced from GPS hardware.</p>";
#else
    html +=
        "<p class='gps-note'>No GPS hardware &mdash; "
        "<button type='button' onclick='fillLocation()'>Use my location</button>"
        " <span id='geo-msg'></span></p>"
        "<p class='gps-hint'>If the button is blocked (browsers require HTTPS for geolocation), "
        "find your coordinates at "
        "<a href='https://www.latlong.net' target='_blank'>latlong.net</a> "
        "and paste them below.</p>"
        "<script>"
        "function fillLocation(){"
          "var msg=document.getElementById('geo-msg');"
          "if(!navigator.geolocation){msg.textContent='Geolocation not available.';return;}"
          "msg.textContent='Locating\u2026';"
          "navigator.geolocation.getCurrentPosition("
            "function(p){"
              "document.querySelector('[name=lat]').value=p.coords.latitude.toFixed(7);"
              "document.querySelector('[name=lon]').value=p.coords.longitude.toFixed(7);"
              "if(p.coords.altitude!==null)"
                "document.querySelector('[name=alt]').value=Math.round(p.coords.altitude);"
              "msg.textContent='\u2713 Done';"
            "},"
            "function(e){msg.textContent='Error: '+e.message;}"
          ");"
        "}"
        "</script>";
#endif

    html += "<div class='row2'>";

    snprintf(tmp, sizeof(tmp), "%.7f", gCfg->latI * 1e-7);
    html += "<label>Latitude&deg;<input name='lat' type='number' step='0.0000001' value='";
    html += tmp;
    html += "'></label>";

    snprintf(tmp, sizeof(tmp), "%.7f", gCfg->lonI * 1e-7);
    html += "<label>Longitude&deg;<input name='lon' type='number' step='0.0000001' value='";
    html += tmp;
    html += "'></label>";

    html += "</div>";

    snprintf(tmp, sizeof(tmp), "%d", (int)gCfg->alt);
    html += "<label>Altitude (m)"
            "<input name='alt' type='number' value='";
    html += tmp;
    html += "' style='max-width:120px'></label>";

    // ── Meshtastic Preset ─────────────────────────────────────
    html += "<h3>Meshtastic Preset</h3>"
            "<div class='row2'>"
            "<label>Region<select id='sel-rgn'>"
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
            "</select></label>"
            "</div>"
            "<button type='button' onclick='applyPreset()'"
            " style='margin-top:.5em;background:#555'>Apply Preset</button>"
            "<p class='gps-hint'>Fills frequency, bandwidth, SF, CR and TX power. "
            "Frequency shown is the band midpoint &mdash; Meshtastic picks the exact slot "
            "by hashing the channel name, but this gets you in the right region. "
            "Fine-tune manually if needed.</p>"
            "<script>"
            "var R={"
              "'US':{f:906.875,p:22},"
              "'EU_433':{f:433.5,p:10},"
              "'EU_868':{f:869.525,p:22},"
              "'CN':{f:490.0,p:19},"
              "'JP':{f:922.0,p:13},"
              "'ANZ':{f:921.5,p:22},"
              "'ANZ_433':{f:433.92,p:14},"
              "'RU':{f:868.95,p:20},"
              "'KR':{f:921.5,p:22},"
              "'TW':{f:922.5,p:22},"
              "'IN':{f:866.0,p:22},"
              "'NZ_865':{f:866.0,p:22},"
              "'TH':{f:922.5,p:16},"
              "'UA_433':{f:433.85,p:10},"
              "'UA_868':{f:868.3,p:14},"
              "'MY_433':{f:434.0,p:20},"
              "'MY_919':{f:921.5,p:22},"
              "'SG_923':{f:921.0,p:20},"
              "'PH_433':{f:433.85,p:10},"
              "'PH_868':{f:868.7,p:14},"
              "'PH_915':{f:916.5,p:22},"
              "'KZ_433':{f:433.925,p:10},"
              "'KZ_863':{f:865.5,p:22},"
              "'NP_865':{f:866.5,p:22},"
              "'BR_902':{f:904.75,p:22},"
              "'LORA_24':{f:2441.75,p:10}"
            "};"
            "var P={"
              "'Long Fast':{bw:250,sf:11,cr:5},"
              "'Long Moderate':{bw:125,sf:11,cr:8},"
              "'Long Slow':{bw:125,sf:12,cr:8},"
              "'Long Turbo':{bw:500,sf:11,cr:8},"
              "'Medium Fast':{bw:250,sf:9,cr:5},"
              "'Medium Slow':{bw:250,sf:10,cr:5},"
              "'Short Fast':{bw:250,sf:7,cr:5},"
              "'Short Slow':{bw:250,sf:8,cr:5},"
              "'Short Turbo':{bw:500,sf:7,cr:5}"
            "};"
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

    // ── LoRa Radio ────────────────────────────────────────────
    html += "<h3>LoRa Radio</h3>";
    html += "<div class='row2'>";

    snprintf(tmp, sizeof(tmp), "%.3f", gCfg->loraFreq);
    html += "<label>Frequency (MHz)"
            "<input name='freq' type='number' step='0.001' min='150' max='2500' value='";
    html += tmp;
    html += "'></label>";

    // Bandwidth — dropdown (LoRa only supports specific values)
    html += "<label>Bandwidth (kHz)<select name='bw'>";
    const float bwOpts[]      = { 125.0f, 250.0f, 500.0f };
    const char *bwLabels[]    = { "125 kHz", "250 kHz", "500 kHz" };
    for (int i = 0; i < 3; i++) {
        snprintf(tmp, sizeof(tmp), "%.0f", bwOpts[i]);
        html += "<option value='";
        html += tmp;
        html += "'";
        if (fabsf(gCfg->loraBw - bwOpts[i]) < 0.1f) html += " selected";
        html += ">";
        html += bwLabels[i];
        html += "</option>";
    }
    html += "</select></label></div>";

    html += "<div class='row2'>";

    // Spreading Factor — dropdown SF7–SF12
    html += "<label>Spreading Factor<select name='sf'>";
    for (int sf = 7; sf <= 12; sf++) {
        snprintf(tmp, sizeof(tmp), "%d", sf);
        html += "<option value='";
        html += tmp;
        html += "'";
        if (gCfg->loraSf == sf) html += " selected";
        html += ">SF";
        html += tmp;
        html += "</option>";
    }
    html += "</select></label>";

    // Coding Rate — 4/5 … 4/8
    html += "<label>Coding Rate<select name='cr'>";
    for (int cr = 5; cr <= 8; cr++) {
        snprintf(tmp, sizeof(tmp), "%d", cr);
        html += "<option value='";
        html += tmp;
        html += "'";
        if (gCfg->loraCr == cr) html += " selected";
        html += ">4/";
        html += tmp;
        html += "</option>";
    }
    html += "</select></label></div>";

    html += "<div class='row2'>";

    snprintf(tmp, sizeof(tmp), "%d", gCfg->loraPower);
    html += "<label>TX Power (dBm, 1&ndash;22)"
            "<input name='pwr' type='number' min='1' max='22' value='";
    html += tmp;
    html += "'></label>";

    snprintf(tmp, sizeof(tmp), "%d", gCfg->loraHopLimit);
    html += "<label>Hop Limit (1&ndash;7)"
            "<input name='hop' type='number' min='1' max='7' value='";
    html += tmp;
    html += "'></label></div>";

    html += "<button type='submit'>Save</button></form>";

    // ── Backup & Restore ──────────────────────────────────────
    html +=
        "<h3>Backup &amp; Restore</h3>"
        "<p><a href='/export' download='config.yaml'"
        " style='display:inline-block;padding:.4em 1.2em;background:#2a9d8f;"
        "color:#fff;border-radius:3px;text-decoration:none;font-size:.95em'>"
        "&#11015; Export config.yaml</a></p>"
        "<form method='POST' action='/import' enctype='multipart/form-data'"
        " style='margin-top:.6em'>"
        "<label>Import config.yaml"
        "<input type='file' name='f' accept='.yaml,.yml'"
        " style='margin-top:.3em'></label>"
        "<button type='submit'>&#11014; Upload &amp; Apply</button>"
        "</form>";

    html += "</body></html>";

    server.send(200, "text/html", html);
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

    // Position
    gCfg->latI = (int32_t)(server.arg("lat").toFloat() * 1e7f);
    gCfg->lonI = (int32_t)(server.arg("lon").toFloat() * 1e7f);
    gCfg->alt  = (int32_t)server.arg("alt").toInt();

    // LoRa
    gCfg->loraFreq     = server.arg("freq").toFloat();
    gCfg->loraBw       = server.arg("bw").toFloat();
    gCfg->loraSf       = (uint8_t)constrain(server.arg("sf").toInt(),  7, 12);
    gCfg->loraCr       = (uint8_t)constrain(server.arg("cr").toInt(),  5,  8);
    gCfg->loraPower    = (uint8_t)constrain(server.arg("pwr").toInt(), 1, 22);
    gCfg->loraHopLimit = (uint8_t)constrain(server.arg("hop").toInt(), 1,  7);

    if (gOnSave) gOnSave();
    sendConfigPage("Saved.");
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

    WiFi.mode(WIFI_STA);
    WiFi.begin(kSSID, kPass);
    Serial.printf("[web] connecting to \"%s\" ...\n", kSSID);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start >= kConnectTimeout) {
            Serial.println("[web] connect timeout");
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            return false;
        }
        delay(100);
    }

    WiFi.localIP().toString().toCharArray(ipBuf, sizeof(ipBuf));

    const char *headers[] = {"Cookie"};
    server.collectHeaders(headers, 1);

    server.on("/",       HTTP_GET,  handleGetRoot);
    server.on("/login",  HTTP_GET,  handleGetLogin);
    server.on("/login",  HTTP_POST, handlePostLogin);
    server.on("/save",   HTTP_POST, handlePostSave);
    server.on("/logout", HTTP_GET,  handleGetLogout);
    server.on("/export", HTTP_GET,  handleGetExport);
    server.on("/import", HTTP_POST, handleImportDone, handleImportUpload);
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
    ipBuf[0] = '\0';
    gCfg     = nullptr;
    gOnSave  = nullptr;
    running  = false;
    Serial.println("[web] stopped");
}

void webCfgLoop() {
    if (running) server.handleClient();
}

bool webCfgRunning() { return running; }
const char *webCfgIP() { return ipBuf; }
