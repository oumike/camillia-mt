#include "config_io.h"
#include <SD.h>
#include <SPI.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *kPath = "/camillia/config.ini";
static bool sdReady = false;

// ── Hex helpers ──────────────────────────────────────────────
static void bytesToHex(const uint8_t *b, int len, char *out) {
    for (int i = 0; i < len; i++) {
        snprintf(out + 2*i, 3, "%02x", b[i]);
    }
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
    cfg.latI       = MY_LAT_I;
    cfg.lonI       = MY_LON_I;
    cfg.alt        = MY_ALT;
    cfg.loraFreq   = MESH_FREQ;
    cfg.loraBw     = MESH_BW;
    cfg.loraSf     = MESH_SF;
    cfg.loraCr     = MESH_CR;
    cfg.loraPower  = MESH_POWER;
    cfg.loraHopLimit = MESH_HOP_LIMIT;
}

// ── SD init ──────────────────────────────────────────────────
bool sdBegin() {
    sdReady = SD.begin(SD_CS, SPI, 4000000);
    Serial.printf("[sd] %s\n", sdReady ? "mounted" : "not found");
    return sdReady;
}

// ── Export ────────────────────────────────────────────────────
bool cfgExport(const RhinoConfig &cfg) {
    if (!sdReady) return false;
    SD.mkdir("/camillia");
    File f = SD.open(kPath, FILE_WRITE);
    if (!f) return false;

    f.println("# camillia config v1");
    f.printf("node_long=%s\n",      cfg.nodeLong);
    f.printf("node_short=%s\n",     cfg.nodeShort);
    f.printf("lat=%d\n",            (int)cfg.latI);
    f.printf("lon=%d\n",            (int)cfg.lonI);
    f.printf("alt=%d\n",            (int)cfg.alt);
    f.printf("lora_freq=%.3f\n",    cfg.loraFreq);
    f.printf("lora_bw=%.1f\n",      cfg.loraBw);
    f.printf("lora_sf=%d\n",        cfg.loraSf);
    f.printf("lora_cr=%d\n",        cfg.loraCr);
    f.printf("lora_power=%d\n",     cfg.loraPower);
    f.printf("lora_hop_limit=%d\n", cfg.loraHopLimit);

    char hexbuf[65];
    for (int i = 0; i < MAX_CHANNELS; i++) {
        const ChannelKey &ch = CHANNEL_KEYS[i];
        bytesToHex(ch.key, ch.keyLen, hexbuf);
        f.printf("ch%d_name=%s\n",   i, ch.name);
        f.printf("ch%d_key=%s\n",    i, hexbuf);
        f.printf("ch%d_keylen=%d\n", i, ch.keyLen);
        f.printf("ch%d_hash=%02x\n", i, ch.hash);
    }
    f.close();
    Serial.printf("[cfg] exported to %s\n", kPath);
    return true;
}

// ── Import ────────────────────────────────────────────────────
bool cfgImport(RhinoConfig &cfg) {
    if (!sdReady) return false;
    File f = SD.open(kPath, FILE_READ);
    if (!f) return false;

    char line[128];
    while (f.available()) {
        int n = f.readBytesUntil('\n', line, sizeof(line) - 1);
        if (n <= 0) continue;
        // strip trailing \r
        if (line[n-1] == '\r') n--;
        line[n] = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        // Node identity
        if (!strcmp(key, "node_long")) {
            strncpy(cfg.nodeLong, val, sizeof(cfg.nodeLong) - 1);
        } else if (!strcmp(key, "node_short")) {
            strncpy(cfg.nodeShort, val, sizeof(cfg.nodeShort) - 1);
        }
        // Position
        else if (!strcmp(key, "lat"))  cfg.latI = (int32_t)atol(val);
        else if (!strcmp(key, "lon"))  cfg.lonI = (int32_t)atol(val);
        else if (!strcmp(key, "alt"))  cfg.alt  = (int32_t)atol(val);
        // LoRa
        else if (!strcmp(key, "lora_freq"))      cfg.loraFreq     = atof(val);
        else if (!strcmp(key, "lora_bw"))        cfg.loraBw       = atof(val);
        else if (!strcmp(key, "lora_sf"))        cfg.loraSf       = (uint8_t)atoi(val);
        else if (!strcmp(key, "lora_cr"))        cfg.loraCr       = (uint8_t)atoi(val);
        else if (!strcmp(key, "lora_power"))     cfg.loraPower    = (uint8_t)atoi(val);
        else if (!strcmp(key, "lora_hop_limit")) cfg.loraHopLimit = (uint8_t)atoi(val);
        // Channels: ch0_name, ch0_key, ch0_keylen, ch0_hash
        else if (key[0] == 'c' && key[1] == 'h' && key[2] >= '0' && key[2] <= '7') {
            int idx = key[2] - '0';
            const char *field = key + 3;  // "_name", "_key", etc.
            if (!strcmp(field, "_name")) {
                strncpy(CHANNEL_KEYS[idx].name_buf, val, sizeof(CHANNEL_KEYS[idx].name_buf) - 1);
                CHANNEL_KEYS[idx].name = CHANNEL_KEYS[idx].name_buf;
            } else if (!strcmp(field, "_key")) {
                CHANNEL_KEYS[idx].keyLen = (uint8_t)hexToBytes(val, CHANNEL_KEYS[idx].key, 32);
            } else if (!strcmp(field, "_keylen")) {
                CHANNEL_KEYS[idx].keyLen = (uint8_t)atoi(val);
            } else if (!strcmp(field, "_hash")) {
                CHANNEL_KEYS[idx].hash = (uint8_t)strtol(val, nullptr, 16);
            }
        }
    }
    f.close();
    Serial.printf("[cfg] imported from %s\n", kPath);
    return true;
}
