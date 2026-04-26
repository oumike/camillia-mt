#include "config_io.h"
#include "base64_util.h"
#include <SD.h>
#include <SPI.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char *kPath = "/camillia/config.yaml";
static bool sdReady = false;

// ── Role / rebroadcast name tables ───────────────────────────
static const char *kRoleNames[] = {
    "CLIENT", "CLIENT_MUTE", "ROUTER", "ROUTER_CLIENT", "REPEATER",
    "TRACKER", "SENSOR", "TAK", "CLIENT_HIDDEN", "LOST_AND_FOUND", "TAK_TRACKER"
};
static const int kNumRoles = 11;

static const char *kRebroadNames[] = {
    "ALL", "ALL_SKIP_DECODING", "LOCAL_ONLY", "KNOWN_ONLY", "CORE_PORTNUMS_ONLY"
};
static const int kNumRebroadModes = 5;

static const char *kThemeNames[] = {
    "CAMELLIA", "EVERGREEN", "EARTHEN"
};
static const int kNumThemes = 3;

static const char *kThemeModeNames[] = {
    "DARK", "LIGHT"
};
static const int kNumThemeModes = 2;

static uint8_t findName(const char *val, const char **table, int n) {
    for (int i = 0; i < n; i++)
        if (!strcmp(val, table[i])) return (uint8_t)i;
    return 0;
}

static void copyTrimmed(char *dst, size_t dstSize, const char *src) {
    if (!dst || dstSize == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (*src && isspace((unsigned char)*src)) src++;
    size_t len = strlen(src);
    while (len > 0 && isspace((unsigned char)src[len - 1])) len--;

    size_t n = (len < (dstSize - 1)) ? len : (dstSize - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool parseBoolValue(const char *val) {
    if (!val || !val[0]) return false;
    if (!strcmp(val, "true") || !strcmp(val, "TRUE") || !strcmp(val, "1")) return true;
    if (!strcmp(val, "false") || !strcmp(val, "FALSE") || !strcmp(val, "0")) return false;
    return atoi(val) != 0;
}

// ── Defaults ─────────────────────────────────────────────────
void cfgInitDefaults(RhinoConfig &cfg) {
    strncpy(cfg.nodeLong,  MY_LONG_NAME,  sizeof(cfg.nodeLong)  - 1);
    strncpy(cfg.nodeShort, MY_SHORT_NAME, sizeof(cfg.nodeShort) - 1);
    cfg.nodeLong[sizeof(cfg.nodeLong)   - 1] = '\0';
    cfg.nodeShort[sizeof(cfg.nodeShort) - 1] = '\0';
    cfg.gpsEnabled   = (bool)MY_GPS_ENABLED;
    cfg.latI         = MY_LAT_I;
    cfg.lonI         = MY_LON_I;
    cfg.alt          = MY_ALT;
    cfg.loraFreq     = MESH_FREQ;
    cfg.loraBw       = MESH_BW;
    cfg.loraSf       = MESH_SF;
    cfg.loraCr       = MESH_CR;
    cfg.loraPower    = MESH_POWER;
    cfg.loraHopLimit = MESH_HOP_LIMIT;
    cfg.deviceRole        = MY_DEVICE_ROLE;
    cfg.rebroadcastMode   = MY_REBROADCAST;
    cfg.okToMqtt          = true;   // allow MQTT nodes to forward our packets by default
    cfg.ignoreMqtt        = false;  // process all packets regardless of via_mqtt flag
    cfg.nodeInfoIntervalS = MY_NODEINFO_INTV;
    cfg.posIntervalS      = MY_POS_INTV;
    strncpy(cfg.region, MY_REGION, sizeof(cfg.region) - 1);
    cfg.region[sizeof(cfg.region) - 1] = '\0';
    strncpy(cfg.tzDef, MY_TZ_DEF, sizeof(cfg.tzDef) - 1);
    cfg.tzDef[sizeof(cfg.tzDef) - 1] = '\0';
    cfg.wifiSsid[0]        = '\0';
    cfg.wifiPass[0]        = '\0';
    cfg.screenOnSecs       = MY_SCREEN_ON_SECS;
    cfg.displayUnits       = MY_DISPLAY_UNITS;
    cfg.compassNorthTop    = MY_COMPASS_NORTH;
    cfg.flipScreen         = MY_FLIP_SCREEN;
    cfg.uiTheme            = MY_UI_THEME;
    cfg.uiMode             = MY_UI_MODE;
    cfg.btEnabled          = MY_BT_ENABLED;
    cfg.btMode             = MY_BT_MODE;
    cfg.btFixedPin         = MY_BT_PIN;
    strncpy(cfg.ntpServer,   MY_NTP_SERVER,  sizeof(cfg.ntpServer)  - 1);
    cfg.ntpServer[sizeof(cfg.ntpServer) - 1] = '\0';
    cfg.mqttEnabled        = MY_MQTT_ENABLED;
    strncpy(cfg.mqttServer,  MY_MQTT_SERVER, sizeof(cfg.mqttServer) - 1);
    cfg.mqttServer[sizeof(cfg.mqttServer) - 1] = '\0';
    strncpy(cfg.mqttUser,    MY_MQTT_USER,   sizeof(cfg.mqttUser)   - 1);
    cfg.mqttUser[sizeof(cfg.mqttUser) - 1] = '\0';
    strncpy(cfg.mqttPass,    MY_MQTT_PASS,   sizeof(cfg.mqttPass)   - 1);
    cfg.mqttPass[sizeof(cfg.mqttPass) - 1] = '\0';
    strncpy(cfg.mqttRoot,    MY_MQTT_ROOT,   sizeof(cfg.mqttRoot)   - 1);
    cfg.mqttRoot[sizeof(cfg.mqttRoot) - 1] = '\0';
    cfg.mqttEncryption     = MY_MQTT_ENCRYPT;
    cfg.mqttMapReport      = MY_MQTT_MAP_RPT;
    cfg.isPowerSaving      = MY_POWER_SAVING;
    cfg.lsSecs             = MY_LS_SECS;
    cfg.minWakeSecs        = MY_MIN_WAKE_SECS;
    cfg.telDeviceEnabled   = MY_TEL_DEV_EN;
    cfg.telDeviceIntervalS = MY_TEL_DEV_INTV;
    cfg.telEnvEnabled      = MY_TEL_ENV_EN;
    cfg.telEnvIntervalS    = MY_TEL_ENV_INTV;
    cfg.cannedEnabled      = MY_CANNED_EN;
    strncpy(cfg.cannedMessages, MY_CANNED_MSGS, sizeof(cfg.cannedMessages) - 1);
    cfg.cannedMessages[sizeof(cfg.cannedMessages) - 1] = '\0';
    cfg.chatSpacing        = MY_CHAT_SPACING;
    cfg.debugAcks          = MY_DBG_ACKS;
    cfg.debugMessages      = MY_DBG_MESSAGES;
    cfg.debugGps           = MY_DBG_GPS;
}

// ── SD init ──────────────────────────────────────────────────
bool sdBegin() {
    sdReady = SD.begin(SD_CS, SPI, 4000000);
    Serial.printf("[sd] %s\n", sdReady ? "mounted" : "not found");
    return sdReady;
}

// ── YAML serialise (Meshtastic CLI-compatible format) ─────────
void cfgToYaml(const RhinoConfig &cfg, String &out) {
    char tmp[96];
    out  = "# start of Meshtastic configure yaml\n";
    // canned_messages top-level
    out += "canned_messages: "; out += cfg.cannedMessages; out += "\n";
    // WiFi credentials (for web config export/import portability)
    out += "wifi_ssid: "; out += cfg.wifiSsid; out += "\n";
    out += "wifi_pass: "; out += cfg.wifiPass; out += "\n";
    snprintf(tmp, sizeof(tmp), "debug_acks: %s\n", cfg.debugAcks ? "true" : "false"); out += tmp;
    snprintf(tmp, sizeof(tmp), "debug_messages: %s\n", cfg.debugMessages ? "true" : "false"); out += tmp;
    snprintf(tmp, sizeof(tmp), "debug_gps: %s\n", cfg.debugGps ? "true" : "false"); out += tmp;
    out += "config:\n";
    // bluetooth
    out += "  bluetooth:\n";
    snprintf(tmp, sizeof(tmp), "    enabled: %s\n",    cfg.btEnabled ? "true" : "false"); out += tmp;
    snprintf(tmp, sizeof(tmp), "    fixedPin: %lu\n",  (unsigned long)cfg.btFixedPin);    out += tmp;
    out += "    mode: ";
    out += (cfg.btMode == 1 ? "FIXED_PIN" : cfg.btMode == 2 ? "NO_PIN" : "RANDOM_PIN");
    out += "\n";
    // device
    out += "  device:\n";
    snprintf(tmp, sizeof(tmp), "    nodeInfoBroadcastSecs: %lu\n", (unsigned long)cfg.nodeInfoIntervalS); out += tmp;
    out += "    rebroadcastMode: ";
    out += (cfg.rebroadcastMode < kNumRebroadModes) ? kRebroadNames[cfg.rebroadcastMode] : "ALL";
    out += "\n";
    out += "    role: ";
    out += (cfg.deviceRole < kNumRoles) ? kRoleNames[cfg.deviceRole] : "CLIENT";
    out += "\n";
    if (cfg.tzDef[0]) { out += "    tzdef: "; out += cfg.tzDef; out += "\n"; }
    // display
    out += "  display:\n";
    snprintf(tmp, sizeof(tmp), "    screenOnSecs: %lu\n", (unsigned long)cfg.screenOnSecs); out += tmp;
    out += "    units: "; out += (cfg.displayUnits ? "IMPERIAL" : "METRIC"); out += "\n";
    snprintf(tmp, sizeof(tmp), "    compassNorthTop: %s\n", cfg.compassNorthTop ? "true" : "false"); out += tmp;
    snprintf(tmp, sizeof(tmp), "    flipScreen: %s\n",      cfg.flipScreen      ? "true" : "false"); out += tmp;
    out += "    theme: ";
    out += (cfg.uiTheme < kNumThemes) ? kThemeNames[cfg.uiTheme] : kThemeNames[0];
    out += "\n";
    out += "    themeMode: ";
    out += (cfg.uiMode < kNumThemeModes) ? kThemeModeNames[cfg.uiMode] : kThemeModeNames[0];
    out += "\n";
    // lora
    out += "  lora:\n";
    snprintf(tmp, sizeof(tmp), "    bandwidth: %.0f\n",    cfg.loraBw);       out += tmp;
    snprintf(tmp, sizeof(tmp), "    codingRate: %d\n",     cfg.loraCr);       out += tmp;
    snprintf(tmp, sizeof(tmp), "    freq_mhz: %.3f\n",     cfg.loraFreq);     out += tmp;
    snprintf(tmp, sizeof(tmp), "    hopLimit: %d\n",       cfg.loraHopLimit); out += tmp;
    out += "    region: "; out += cfg.region; out += "\n";
    snprintf(tmp, sizeof(tmp), "    spreadFactor: %d\n",   cfg.loraSf);       out += tmp;
    snprintf(tmp, sizeof(tmp), "    txPower: %d\n",        cfg.loraPower);    out += tmp;
    // network
    out += "  network:\n";
    out += "    ntpServer: "; out += cfg.ntpServer; out += "\n";
    // position
    out += "  position:\n";
    snprintf(tmp, sizeof(tmp), "    fixedPosition: %s\n", cfg.gpsEnabled ? "false" : "true"); out += tmp;
    out += "    gpsMode: "; out += (cfg.gpsEnabled ? "ENABLED" : "DISABLED"); out += "\n";
    snprintf(tmp, sizeof(tmp), "    positionBroadcastSecs: %lu\n", (unsigned long)cfg.posIntervalS); out += tmp;
    // power
    out += "  power:\n";
    snprintf(tmp, sizeof(tmp), "    isPowerSaving: %s\n", cfg.isPowerSaving ? "true" : "false"); out += tmp;
    snprintf(tmp, sizeof(tmp), "    lsSecs: %lu\n",       (unsigned long)cfg.lsSecs);            out += tmp;
    snprintf(tmp, sizeof(tmp), "    minWakeSecs: %lu\n",  (unsigned long)cfg.minWakeSecs);        out += tmp;
    // location
    out += "location:\n";
    snprintf(tmp, sizeof(tmp), "  alt: %d\n",    (int)cfg.alt);        out += tmp;
    snprintf(tmp, sizeof(tmp), "  lat: %.7f\n",  cfg.latI * 1e-7f);    out += tmp;
    snprintf(tmp, sizeof(tmp), "  lon: %.7f\n",  cfg.lonI * 1e-7f);    out += tmp;
    // module_config
    out += "module_config:\n";
    out += "  cannedMessage:\n";
    snprintf(tmp, sizeof(tmp), "    enabled: %s\n", cfg.cannedEnabled ? "true" : "false"); out += tmp;
    out += "  mqtt:\n";
    out += "    address: "; out += cfg.mqttServer; out += "\n";
    snprintf(tmp, sizeof(tmp), "    enabled: %s\n",           cfg.mqttEnabled   ? "true" : "false"); out += tmp;
    snprintf(tmp, sizeof(tmp), "    encryptionEnabled: %s\n", cfg.mqttEncryption? "true" : "false"); out += tmp;
    snprintf(tmp, sizeof(tmp), "    mapReportingEnabled: %s\n",cfg.mqttMapReport? "true" : "false"); out += tmp;
    out += "    password: "; out += cfg.mqttPass; out += "\n";
    out += "    root: ";     out += cfg.mqttRoot; out += "\n";
    out += "    username: "; out += cfg.mqttUser; out += "\n";
    out += "  telemetry:\n";
    snprintf(tmp, sizeof(tmp), "    deviceTelemetryEnabled: %s\n", cfg.telDeviceEnabled ? "true" : "false"); out += tmp;
    snprintf(tmp, sizeof(tmp), "    deviceUpdateInterval: %lu\n",  (unsigned long)cfg.telDeviceIntervalS);    out += tmp;
    snprintf(tmp, sizeof(tmp), "    environmentMeasurementEnabled: %s\n", cfg.telEnvEnabled ? "true" : "false"); out += tmp;
    snprintf(tmp, sizeof(tmp), "    environmentUpdateInterval: %lu\n",    (unsigned long)cfg.telEnvIntervalS);    out += tmp;
    // owner
    out += "owner: ";       out += cfg.nodeLong;  out += "\n";
    out += "owner_short: "; out += cfg.nodeShort; out += "\n";
    // channels (our format — key stored as base64, e.g. "MA==" for 1-byte PSK 0x30)
    out += "channels:\n";
    char b64buf[48];
    for (int i = 0; i < MESH_CHANNELS; i++) {
        const ChannelKey &ch = CHANNEL_KEYS[i];
        const char *nm = ch.name_buf[0] ? ch.name_buf : ch.name;
        base64Encode(ch.key, ch.keyLen, b64buf);
        const char *roleStr = (ch.role == 1) ? "SECONDARY" : (ch.role == 2) ? "DISABLED" : "PRIMARY";
        snprintf(tmp, sizeof(tmp),
                 "  - name: %s\n    role: %s\n    key: %s\n    hash: %02x\n",
                 nm, roleStr, b64buf, ch.hash);
        out += tmp;
    }
}

// ── YAML parse (from memory buffer) ──────────────────────────
// Handles both:
//   - Meshtastic CLI format (owner/owner_short top-level, config.lora at indent 4, location)
//   - Legacy Camillia format (node/position/lora/channels sections at indent 2)
bool cfgImportFromBuf(const char *buf, size_t len, RhinoConfig &cfg) {
    char        section[20]    = "";   // indent-0 section key
    char        subsection[20] = "";   // indent-2 subsection key (e.g. "lora" under "config")
    int         chanIdx        = -1;
    const char *p              = buf;
    const char *end            = buf + len;

    while (p < end) {
        char   line[128];
        size_t n = 0;
        while (p < end && *p != '\n' && n < sizeof(line) - 1)
            line[n++] = *p++;
        if (p < end) p++;
        if (n > 0 && line[n-1] == '\r') n--;
        line[n] = '\0';
        if (!n) continue;

        int indent = 0;
        while (line[indent] == ' ') indent++;
        const char *t = line + indent;
        if (t[0] == '#' || t[0] == '\0') continue;

        // Channel list item. Accept both normal "- name:" and malformed lines
        // with accidental leading junk before "- name:".
        if (!strcmp(section, "channels")) {
            const char *nameItem = strstr(t, "- name:");
            if (nameItem) {
                chanIdx++;
                if (chanIdx >= MAX_CHANNELS) break;
                const char *v2 = nameItem + 7;
                while (*v2 == ' ') v2++;
                if (*v2) {
                    copyTrimmed(CHANNEL_KEYS[chanIdx].name_buf,
                                sizeof(CHANNEL_KEYS[0].name_buf), v2);
                    CHANNEL_KEYS[chanIdx].name = CHANNEL_KEYS[chanIdx].name_buf;
                }
                continue;
            }
        }

        char *colon = strchr((char *)t, ':');
        if (!colon) continue;
        *colon = '\0';
        const char *key = t;
        const char *val = colon + 1;
        while (*val == ' ') val++;
        bool hasVal = (val[0] != '\0');

        if (indent == 0) {
            if (!hasVal) {
                strncpy(section, key, sizeof(section) - 1);
                section[sizeof(section) - 1] = '\0';
                subsection[0] = '\0';
                chanIdx = -1;
            } else {
                // Top-level key-value (Meshtastic CLI format)
                if      (!strcmp(key, "owner"))
                    strncpy(cfg.nodeLong,  val, sizeof(cfg.nodeLong)  - 1);
                else if (!strcmp(key, "owner_short"))
                    strncpy(cfg.nodeShort, val, sizeof(cfg.nodeShort) - 1);
                else if (!strcmp(key, "canned_messages"))
                    strncpy(cfg.cannedMessages, val, sizeof(cfg.cannedMessages) - 1);
                else if (!strcmp(key, "wifi_ssid")) {
                    strncpy(cfg.wifiSsid, val, sizeof(cfg.wifiSsid) - 1);
                    cfg.wifiSsid[sizeof(cfg.wifiSsid) - 1] = '\0';
                }
                else if (!strcmp(key, "wifi_pass")) {
                    strncpy(cfg.wifiPass, val, sizeof(cfg.wifiPass) - 1);
                    cfg.wifiPass[sizeof(cfg.wifiPass) - 1] = '\0';
                }
                else if (!strcmp(key, "debug_acks"))
                    cfg.debugAcks = parseBoolValue(val);
                else if (!strcmp(key, "debug_messages"))
                    cfg.debugMessages = parseBoolValue(val);
                else if (!strcmp(key, "debug_gps"))
                    cfg.debugGps = parseBoolValue(val);
            }
        } else if (indent == 2) {
            if (!hasVal) {
                // Subsection header (e.g. "lora:" under "config:")
                strncpy(subsection, key, sizeof(subsection) - 1);
                subsection[sizeof(subsection) - 1] = '\0';
                chanIdx = -1;
            } else {
                if (!strcmp(section, "location")) {
                    // Meshtastic CLI: float degrees
                    if (!strcmp(key, "lat"))
                        cfg.latI = strchr(val, '.') ? (int32_t)(atof(val) * 1e7f)
                                                    : (int32_t)atol(val);
                    else if (!strcmp(key, "lon"))
                        cfg.lonI = strchr(val, '.') ? (int32_t)(atof(val) * 1e7f)
                                                    : (int32_t)atol(val);
                    else if (!strcmp(key, "alt"))
                        cfg.alt = (int32_t)atol(val);
                } else if (!strcmp(section, "node")) {
                    // Legacy format
                    if      (!strcmp(key, "long"))  strncpy(cfg.nodeLong,  val, sizeof(cfg.nodeLong)  - 1);
                    else if (!strcmp(key, "short")) strncpy(cfg.nodeShort, val, sizeof(cfg.nodeShort) - 1);
                } else if (!strcmp(section, "position")) {
                    // Legacy format: stored as scaled int32 * 1e7
                    if      (!strcmp(key, "lat")) cfg.latI = (int32_t)atol(val);
                    else if (!strcmp(key, "lon")) cfg.lonI = (int32_t)atol(val);
                    else if (!strcmp(key, "alt")) cfg.alt  = (int32_t)atol(val);
                } else if (!strcmp(section, "lora")) {
                    // Legacy format
                    if      (!strcmp(key, "freq"))      cfg.loraFreq     = atof(val);
                    else if (!strcmp(key, "bw"))        cfg.loraBw       = atof(val);
                    else if (!strcmp(key, "sf"))        cfg.loraSf       = (uint8_t)atoi(val);
                    else if (!strcmp(key, "cr"))        cfg.loraCr       = (uint8_t)atoi(val);
                    else if (!strcmp(key, "power"))     cfg.loraPower    = (uint8_t)atoi(val);
                    else if (!strcmp(key, "hop_limit")) cfg.loraHopLimit = (uint8_t)atoi(val);
                }
            }
        } else if (indent == 4) {
            if (!hasVal) {
                // Deeper subsection header — ignore
            } else if (chanIdx >= 0 && chanIdx < MAX_CHANNELS) {
                // Legacy channel properties
                if (!strcmp(key, "name")) {
                    copyTrimmed(CHANNEL_KEYS[chanIdx].name_buf,
                                sizeof(CHANNEL_KEYS[0].name_buf), val);
                    CHANNEL_KEYS[chanIdx].name = CHANNEL_KEYS[chanIdx].name_buf;
                } else if (!strcmp(key, "key")) {
                    int kl = base64Decode(val, CHANNEL_KEYS[chanIdx].key, 32);
                    if (kl > 0) CHANNEL_KEYS[chanIdx].keyLen = (uint8_t)kl;
                } else if (!strcmp(key, "hash")) {
                    CHANNEL_KEYS[chanIdx].hash = (uint8_t)strtol(val, nullptr, 16);
                } else if (!strcmp(key, "role")) {
                    CHANNEL_KEYS[chanIdx].role = !strcmp(val,"SECONDARY") ? 1 : !strcmp(val,"DISABLED") ? 2 : 0;
                }
            } else if (!strcmp(section, "config") && !strcmp(subsection, "lora")) {
                // Meshtastic CLI format
                if      (!strcmp(key, "bandwidth"))    cfg.loraBw       = atof(val);
                else if (!strcmp(key, "codingRate"))   cfg.loraCr       = (uint8_t)atoi(val);
                else if (!strcmp(key, "hopLimit"))     cfg.loraHopLimit = (uint8_t)atoi(val);
                else if (!strcmp(key, "spreadFactor")) cfg.loraSf       = (uint8_t)atoi(val);
                else if (!strcmp(key, "txPower"))      cfg.loraPower    = (uint8_t)constrain(atoi(val), 1, 22);
                else if (!strcmp(key, "freq_mhz"))     cfg.loraFreq     = atof(val);
                else if (!strcmp(key, "region"))       strncpy(cfg.region, val, sizeof(cfg.region) - 1);
            } else if (!strcmp(section, "config") && !strcmp(subsection, "device")) {
                if      (!strcmp(key, "role"))
                    cfg.deviceRole = findName(val, kRoleNames, kNumRoles);
                else if (!strcmp(key, "rebroadcastMode"))
                    cfg.rebroadcastMode = findName(val, kRebroadNames, kNumRebroadModes);
                else if (!strcmp(key, "nodeInfoBroadcastSecs"))
                    cfg.nodeInfoIntervalS = (uint32_t)atol(val);
                else if (!strcmp(key, "tzdef")) strncpy(cfg.tzDef, val, sizeof(cfg.tzDef) - 1);
            } else if (!strcmp(section, "config") && !strcmp(subsection, "position")) {
                if (!strcmp(key, "positionBroadcastSecs"))
                    cfg.posIntervalS = (uint32_t)atol(val);
                else if (!strcmp(key, "gpsMode"))
                    cfg.gpsEnabled = (!strcmp(val,"ENABLED"));
            } else if (!strcmp(section, "config") && !strcmp(subsection, "bluetooth")) {
                if      (!strcmp(key, "enabled"))  cfg.btEnabled  = (!strcmp(val,"true"));
                else if (!strcmp(key, "fixedPin")) cfg.btFixedPin = (uint32_t)atol(val);
                else if (!strcmp(key, "mode"))     cfg.btMode = !strcmp(val,"FIXED_PIN") ? 1 : !strcmp(val,"NO_PIN") ? 2 : 0;
            } else if (!strcmp(section, "config") && !strcmp(subsection, "display")) {
                if      (!strcmp(key, "screenOnSecs"))    cfg.screenOnSecs    = (uint32_t)atol(val);
                else if (!strcmp(key, "units"))           cfg.displayUnits    = !strcmp(val,"IMPERIAL") ? 1 : 0;
                else if (!strcmp(key, "compassNorthTop")) cfg.compassNorthTop = (!strcmp(val,"true"));
                else if (!strcmp(key, "flipScreen"))      cfg.flipScreen      = (!strcmp(val,"true"));
                else if (!strcmp(key, "theme")) {
                    if (isdigit((unsigned char)val[0]))
                        cfg.uiTheme = (uint8_t)constrain(atoi(val), 0, UI_THEME_COUNT - 1);
                    else
                        cfg.uiTheme = findName(val, kThemeNames, kNumThemes);
                }
                else if (!strcmp(key, "themeMode")) {
                    if (isdigit((unsigned char)val[0]))
                        cfg.uiMode = (uint8_t)constrain(atoi(val), 0, 1);
                    else
                        cfg.uiMode = findName(val, kThemeModeNames, kNumThemeModes);
                }
            } else if (!strcmp(section, "config") && !strcmp(subsection, "network")) {
                if (!strcmp(key, "ntpServer")) strncpy(cfg.ntpServer, val, sizeof(cfg.ntpServer) - 1);
            } else if (!strcmp(section, "config") && !strcmp(subsection, "power")) {
                if      (!strcmp(key, "isPowerSaving")) cfg.isPowerSaving = (!strcmp(val,"true"));
                else if (!strcmp(key, "lsSecs"))        cfg.lsSecs        = (uint32_t)atol(val);
                else if (!strcmp(key, "minWakeSecs"))   cfg.minWakeSecs   = (uint32_t)atol(val);
            } else if (!strcmp(section, "module_config") && !strcmp(subsection, "mqtt")) {
                if      (!strcmp(key, "address"))            strncpy(cfg.mqttServer,  val, sizeof(cfg.mqttServer)  - 1);
                else if (!strcmp(key, "enabled"))            cfg.mqttEnabled    = (!strcmp(val,"true"));
                else if (!strcmp(key, "encryptionEnabled"))  cfg.mqttEncryption = (!strcmp(val,"true"));
                else if (!strcmp(key, "mapReportingEnabled"))cfg.mqttMapReport  = (!strcmp(val,"true"));
                else if (!strcmp(key, "password"))           strncpy(cfg.mqttPass,    val, sizeof(cfg.mqttPass)    - 1);
                else if (!strcmp(key, "root"))               strncpy(cfg.mqttRoot,    val, sizeof(cfg.mqttRoot)    - 1);
                else if (!strcmp(key, "username"))           strncpy(cfg.mqttUser,    val, sizeof(cfg.mqttUser)    - 1);
            } else if (!strcmp(section, "module_config") && !strcmp(subsection, "telemetry")) {
                if      (!strcmp(key, "deviceTelemetryEnabled"))        cfg.telDeviceEnabled   = (!strcmp(val,"true"));
                else if (!strcmp(key, "deviceUpdateInterval"))          cfg.telDeviceIntervalS = (uint32_t)atol(val);
                else if (!strcmp(key, "environmentMeasurementEnabled")) cfg.telEnvEnabled      = (!strcmp(val,"true"));
                else if (!strcmp(key, "environmentUpdateInterval"))     cfg.telEnvIntervalS    = (uint32_t)atol(val);
            } else if (!strcmp(section, "module_config") && !strcmp(subsection, "cannedMessage")) {
                if (!strcmp(key, "enabled")) cfg.cannedEnabled = (!strcmp(val,"true"));
            } else if (!strcmp(section, "channels") && chanIdx >= 0 && chanIdx < MESH_CHANNELS) {
                if (!strcmp(key, "role"))
                    CHANNEL_KEYS[chanIdx].role = !strcmp(val,"SECONDARY") ? 1 : !strcmp(val,"DISABLED") ? 2 : 0;
            }
        }
    }
    return true;
}

// ── Export to SD ──────────────────────────────────────────────
bool cfgExport(const RhinoConfig &cfg) {
    if (!sdReady) return false;
    SD.mkdir("/camillia");
    File f = SD.open(kPath, FILE_WRITE);
    if (!f) return false;
    String yaml;
    cfgToYaml(cfg, yaml);
    f.print(yaml);
    f.close();
    Serial.printf("[cfg] exported to %s\n---\n%s---\n", kPath, yaml.c_str());
    return true;
}

// ── Import from SD ────────────────────────────────────────────
bool cfgImport(RhinoConfig &cfg) {
    if (!sdReady) return false;
    File f = SD.open(kPath, FILE_READ);
    if (!f) return false;
    String content = f.readString();
    f.close();
    bool ok = cfgImportFromBuf(content.c_str(), content.length(), cfg);
    if (ok) Serial.printf("[cfg] imported from %s\n", kPath);
    return ok;
}
