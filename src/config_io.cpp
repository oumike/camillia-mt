#include "config_io.h"
#include <SD.h>
#include <SPI.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *kPath = "/camillia/config.yaml";
static bool sdReady = false;

// ── Hex helpers ──────────────────────────────────────────────
static void bytesToHex(const uint8_t *b, int len, char *out) {
    for (int i = 0; i < len; i++) snprintf(out + 2*i, 3, "%02x", b[i]);
    out[2*len] = '\0';
}

static int hexToBytes(const char *hex, uint8_t *out, int maxLen) {
    int n = strlen(hex) / 2;
    if (n > maxLen) n = maxLen;
    for (int i = 0; i < n; i++) {
        char tmp[3] = { hex[2*i], hex[2*i+1], '\0' };
        out[i] = (uint8_t)strtol(tmp, nullptr, 16);
    }
    return n;
}

// ── Defaults ─────────────────────────────────────────────────
void cfgInitDefaults(RhinoConfig &cfg) {
    strncpy(cfg.nodeLong,  MY_LONG_NAME,  sizeof(cfg.nodeLong)  - 1);
    strncpy(cfg.nodeShort, MY_SHORT_NAME, sizeof(cfg.nodeShort) - 1);
    cfg.nodeLong[sizeof(cfg.nodeLong)   - 1] = '\0';
    cfg.nodeShort[sizeof(cfg.nodeShort) - 1] = '\0';
    cfg.latI         = MY_LAT_I;
    cfg.lonI         = MY_LON_I;
    cfg.alt          = MY_ALT;
    cfg.loraFreq     = MESH_FREQ;
    cfg.loraBw       = MESH_BW;
    cfg.loraSf       = MESH_SF;
    cfg.loraCr       = MESH_CR;
    cfg.loraPower    = MESH_POWER;
    cfg.loraHopLimit = MESH_HOP_LIMIT;
}

// ── SD init ──────────────────────────────────────────────────
bool sdBegin() {
    sdReady = SD.begin(SD_CS, SPI, 4000000);
    Serial.printf("[sd] %s\n", sdReady ? "mounted" : "not found");
    return sdReady;
}

// ── YAML serialise (Meshtastic CLI-compatible format) ─────────
void cfgToYaml(const RhinoConfig &cfg, String &out) {
    char tmp[64];
    out  = "# start of Meshtastic configure yaml\n";
    out += "config:\n";
    out += "  lora:\n";
    snprintf(tmp, sizeof(tmp), "    bandwidth: %.0f\n",    cfg.loraBw);       out += tmp;
    snprintf(tmp, sizeof(tmp), "    codingRate: %d\n",     cfg.loraCr);       out += tmp;
    snprintf(tmp, sizeof(tmp), "    freq_mhz: %.3f\n",     cfg.loraFreq);     out += tmp;
    snprintf(tmp, sizeof(tmp), "    hopLimit: %d\n",       cfg.loraHopLimit); out += tmp;
    snprintf(tmp, sizeof(tmp), "    spreadFactor: %d\n",   cfg.loraSf);       out += tmp;
    snprintf(tmp, sizeof(tmp), "    txPower: %d\n",        cfg.loraPower);    out += tmp;
    out += "location:\n";
    snprintf(tmp, sizeof(tmp), "  alt: %d\n",              (int)cfg.alt);     out += tmp;
    snprintf(tmp, sizeof(tmp), "  lat: %.7f\n",            cfg.latI * 1e-7f); out += tmp;
    snprintf(tmp, sizeof(tmp), "  lon: %.7f\n",            cfg.lonI * 1e-7f); out += tmp;
    out += "owner: ";       out += cfg.nodeLong;  out += "\n";
    out += "owner_short: "; out += cfg.nodeShort; out += "\n";
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

        // Legacy channel list item: "  - name: foo"
        if (indent == 2 && t[0] == '-' && t[1] == ' ') {
            chanIdx++;
            if (chanIdx >= MAX_CHANNELS) break;
            const char *after = t + 2;
            char *c2 = strchr((char *)after, ':');
            if (c2) {
                *c2 = '\0';
                const char *v2 = c2 + 1;
                while (*v2 == ' ') v2++;
                if (!strcmp(after, "name")) {
                    strncpy(CHANNEL_KEYS[chanIdx].name_buf, v2,
                            sizeof(CHANNEL_KEYS[0].name_buf) - 1);
                    CHANNEL_KEYS[chanIdx].name = CHANNEL_KEYS[chanIdx].name_buf;
                }
            }
            continue;
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
                    strncpy(CHANNEL_KEYS[chanIdx].name_buf, val,
                            sizeof(CHANNEL_KEYS[0].name_buf) - 1);
                    CHANNEL_KEYS[chanIdx].name = CHANNEL_KEYS[chanIdx].name_buf;
                } else if (!strcmp(key, "key")) {
                    CHANNEL_KEYS[chanIdx].keyLen =
                        (uint8_t)hexToBytes(val, CHANNEL_KEYS[chanIdx].key, 32);
                } else if (!strcmp(key, "keylen")) {
                    CHANNEL_KEYS[chanIdx].keyLen = (uint8_t)atoi(val);
                } else if (!strcmp(key, "hash")) {
                    CHANNEL_KEYS[chanIdx].hash = (uint8_t)strtol(val, nullptr, 16);
                }
            } else if (!strcmp(section, "config") && !strcmp(subsection, "lora")) {
                // Meshtastic CLI format
                if      (!strcmp(key, "bandwidth"))    cfg.loraBw       = atof(val);
                else if (!strcmp(key, "codingRate"))   cfg.loraCr       = (uint8_t)atoi(val);
                else if (!strcmp(key, "hopLimit"))     cfg.loraHopLimit = (uint8_t)atoi(val);
                else if (!strcmp(key, "spreadFactor")) cfg.loraSf       = (uint8_t)atoi(val);
                else if (!strcmp(key, "txPower"))      cfg.loraPower    = (uint8_t)constrain(atoi(val), 1, 22);
                else if (!strcmp(key, "freq_mhz"))     cfg.loraFreq     = atof(val);
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
