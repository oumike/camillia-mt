// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.h"

#include "base64_util.h"
#include "config.h"
#include "mesh_channel_plan.h"
#include "mesh_proto.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace {

const char kDefaultFile[] =
    "# Camillia-MT CardputerZero settings. Restart the service after editing:\n"
    "#   sudo systemctl restart camillia-czero\n"
    "\n"
    "# LoRa region (US, EU_868, ANZ, ...) and modem preset (LongFast, MediumFast, ...).\n"
    "region = US\n"
    "preset = LongFast\n"
    "\n"
    "# Hops our own packets may take (0-7), and transmit power in dBm (capped by\n"
    "# the region and the radio).\n"
    "hop_limit = 3\n"
    "tx_power = 22\n"
    "\n"
    "# CLIENT relays other nodes' packets; CLIENT_MUTE does not.\n"
    "role = CLIENT\n"
    "\n"
    "# Listen only: never transmit.\n"
    "rx_only = false\n"
    "\n"
    "# Broadcast our GPS position on the primary channel. position_precision is\n"
    "# 32 for exact, down to 10 (about 23 km); a public channel caps it at 15.\n"
    "gps = true\n"
    "gps_device = /dev/serial0\n"
    "gps_baud = 115200\n"
    "position_interval = 1800\n"
    "position_precision = 32\n"
    "\n"
    "# Seconds between NodeInfo broadcasts.\n"
    "nodeinfo_interval = 900\n"
    "\n"
    "# Channels, primary first: \"channel = NAME BASE64KEY\". With none, the\n"
    "# preset's public channel (e.g. LongFast, key AQ==) is used.\n"
    "# channel = LongFast AQ==\n"
    "# channel = MyGroup 0vxp7sCkt6wJRsF1tmfSyBpIW3UzqSy+N0cQuKpXzu4=\n";

std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) a++;
    while (b > a && isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

// Compares preset names ignoring case, spaces and underscores, so "LongFast",
// "Long Fast" and "LONG_FAST" are the same preset.
std::string squash(const char *s) {
    std::string o;
    for (; *s; s++)
        if (*s != ' ' && *s != '_' && *s != '-') o += (char)tolower((unsigned char)*s);
    return o;
}

int findPreset(const std::string &name) {
    std::string want = squash(name.c_str());
    for (int i = 0; i < PRESET_COUNT; i++)
        if (squash(kPresets[i].name) == want || squash(kPresets[i].channelName) == want) return i;
    return -1;
}

bool parseBool(const std::string &v, bool &out) {
    if (!strcasecmp(v.c_str(), "true") || v == "1" || !strcasecmp(v.c_str(), "yes") || !strcasecmp(v.c_str(), "on"))
        return out = true, true;
    if (!strcasecmp(v.c_str(), "false") || v == "0" || !strcasecmp(v.c_str(), "no") || !strcasecmp(v.c_str(), "off"))
        return out = false, true;
    return false;
}

bool parseInt(const std::string &v, long lo, long hi, long &out) {
    char *end;
    long n = strtol(v.c_str(), &end, 10);
    if (end == v.c_str() || *end || n < lo || n > hi) return false;
    out = n;
    return true;
}

}  // namespace

const char *roleName(uint8_t role) { return role == 1 ? "CLIENT_MUTE" : "CLIENT"; }

Settings loadSettings(const std::string &path) {
    Settings s;
    s.region = MY_REGION;
    s.preset = PRESET_LONG_FAST;
    s.txPower = MESH_POWER;
    s.positionIntervalS = MY_POS_INTV;
    s.positionPrecision = MY_POSITION_PRECISION;
    s.nodeInfoIntervalS = MY_NODEINFO_INTV;

    FILE *f = fopen(path.c_str(), "r");
    if (!f) {
        if ((f = fopen(path.c_str(), "w"))) {
            fputs(kDefaultFile, f);
            fclose(f);
            fprintf(stderr, "[cfg] wrote default settings to %s\n", path.c_str());
        }
        return s;
    }

    char buf[512];
    int lineNo = 0;
    bool presetSet = false;
    while (fgets(buf, sizeof buf, f)) {
        lineNo++;
        std::string line = trim(buf);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        auto bad = [&](const char *why) {
            fprintf(stderr, "[cfg] %s:%d: %s: %s\n", path.c_str(), lineNo, why, line.c_str());
        };
        if (eq == std::string::npos) { bad("expected key = value"); continue; }
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        long n;
        bool b;
        if (k == "region") {
            for (auto &c : v) c = (char)toupper((unsigned char)c);
            if (!regionPlan(v.c_str()) || !regionSelectable(v.c_str(), false)) bad("unknown region");
            else s.region = v;
        } else if (k == "preset") {
            int p = findPreset(v);
            if (p < 0 || !presetUsableOnThisRadio((uint8_t)p)) bad("unknown preset");
            else s.preset = (uint8_t)p, presetSet = true;
        } else if (k == "hop_limit") {
            if (parseInt(v, 0, 7, n)) s.hopLimit = (int)n; else bad("want 0-7");
        } else if (k == "tx_power") {
            if (parseInt(v, -9, MESH_POWER, n)) s.txPower = (int)n; else bad("out of range");
        } else if (k == "role") {
            if (!strcasecmp(v.c_str(), "CLIENT")) s.role = 0;
            else if (!strcasecmp(v.c_str(), "CLIENT_MUTE")) s.role = 1;
            else bad("want CLIENT or CLIENT_MUTE");
        } else if (k == "rx_only") {
            if (parseBool(v, b)) s.rxOnly = b; else bad("want true or false");
        } else if (k == "gps") {
            if (parseBool(v, b)) s.gps = b; else bad("want true or false");
        } else if (k == "gps_device") {
            s.gpsDevice = v;
        } else if (k == "gps_baud") {
            if (parseInt(v, 1200, 921600, n)) s.gpsBaud = (int)n; else bad("bad baud rate");
        } else if (k == "position_interval") {
            if (parseInt(v, 0, 86400 * 7, n)) s.positionIntervalS = (uint32_t)n; else bad("bad interval");
        } else if (k == "position_precision") {
            if (parseInt(v, 0, 32, n)) s.positionPrecision = (uint8_t)n; else bad("want 0-32");
        } else if (k == "nodeinfo_interval") {
            if (parseInt(v, 60, 86400 * 7, n)) s.nodeInfoIntervalS = (uint32_t)n; else bad("want 60 or more");
        } else if (k == "channel") {
            size_t sp = v.find_first_of(" \t");
            ChannelSetting c;
            c.name = trim(v.substr(0, sp));
            std::string key = sp == std::string::npos ? "AQ==" : trim(v.substr(sp + 1));
            int len = base64Decode(key.c_str(), c.key, sizeof c.key);
            if (c.name.empty() || c.name.size() > 11) bad("channel name must be 1-11 characters");
            else if (len != 0 && len != 1 && len != 16 && len != 32) bad("key must decode to 0, 1, 16 or 32 bytes");
            else if ((int)s.channels.size() >= MESH_CHANNELS) bad("too many channels");
            else c.keyLen = (uint8_t)len, s.channels.push_back(c);
        } else {
            bad("unknown setting");
        }
    }
    fclose(f);

    if (!presetSet) s.preset = regionDefaultPreset(s.region.c_str());
    if (!presetAllowedInRegion(s.region.c_str(), s.preset)) {
        fprintf(stderr, "[cfg] preset %s is not allowed in %s; using %s\n", kPresets[s.preset].name,
                s.region.c_str(), kPresets[regionDefaultPreset(s.region.c_str())].name);
        s.preset = regionDefaultPreset(s.region.c_str());
    }
    int cap = regionPower(s.region.c_str());
    if (cap > 0 && s.txPower > cap) s.txPower = cap;
    return s;
}

void applyChannels(const Settings &s) {
    std::vector<ChannelSetting> list = s.channels;
    if (list.empty()) {
        ChannelSetting c;
        c.name = kPresets[s.preset].channelName;
        c.key[0] = 0x01;
        c.keyLen = 1;
        list.push_back(c);
    }
    for (int i = 0; i < MAX_CHANNELS; i++) {
        ChannelKey &ck = CHANNEL_KEYS[i];
        if (i >= (int)list.size()) {
            // Keyless as well as disabled, so the table's default keys
            // cannot decrypt traffic on channels that were not configured.
            memset(ck.name_buf, 0, sizeof ck.name_buf);
            ck.name = ck.name_buf;
            memset(ck.key, 0, sizeof ck.key);
            ck.keyLen = 0;
            ck.role = 2;
            ck.shareLocation = false;
            continue;
        }
        const ChannelSetting &c = list[i];
        memset(ck.name_buf, 0, sizeof ck.name_buf);
        strncpy(ck.name_buf, c.name.c_str(), sizeof ck.name_buf - 1);
        ck.name = ck.name_buf;
        memset(ck.key, 0, sizeof ck.key);
        memcpy(ck.key, c.key, c.keyLen);
        ck.keyLen = c.keyLen;
        ck.role = i == 0 ? 0 : 1;
        ck.shareLocation = i == 0;
        ck.useAead = false;
        ck.hopLimitPlus1 = 0;
        ck.hash = channelKeyHash(ck);
    }
}
