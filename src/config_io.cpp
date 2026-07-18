#include "config_io.h"
#include "mesh_channel_plan.h"
#include "base64_util.h"
#include "utf8_utils.h"
#include "ignore_list.h"
#include <SD.h>
#include <Preferences.h>
#include <SPI.h>
#include <Wire.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ── Channel plan tables ──────────────────────────────────────────────────────
const PresetParams kPresets[PRESET_COUNT] = {
    //  human label        channel name   bw      sf  cr
    {"Long Fast",     "LongFast",    250.0f, 11, 5},
    {"Long Moderate", "LongMod",     125.0f, 11, 8},
    {"Long Slow",     "LongSlow",    125.0f, 12, 8},
    {"Long Turbo",    "LongTurbo",   500.0f, 11, 8},
    {"Medium Fast",   "MediumFast",  250.0f,  9, 5},
    {"Medium Slow",   "MediumSlow",  250.0f, 10, 5},
    {"Short Fast",    "ShortFast",   250.0f,  7, 5},
    {"Short Slow",    "ShortSlow",   250.0f,  8, 5},
    {"Short Turbo",   "ShortTurbo",  500.0f,  7, 5},
};

// Band edges (freqStart, freqEnd in MHz) mirror Meshtastic's RegionInfo table;
// power is the SX1262 hardware-capped TX ceiling (dBm), <= the regulatory limit.
const RegionPlan kRegions[] = {
    //  code       freqStart  freqEnd   power
    {"US",      902.0f,   928.0f,   22},
    {"EU_433",  433.0f,   434.0f,   10},
    {"EU_868",  869.4f,   869.65f,  22},
    {"CN",      470.0f,   510.0f,   19},
    {"JP",      920.5f,   923.5f,   13},
    {"ANZ",     915.0f,   928.0f,   22},
    {"ANZ_433", 433.05f,  434.79f,  14},
    {"RU",      868.7f,   869.2f,   20},
    {"KR",      920.0f,   923.0f,   22},
    {"TW",      920.0f,   925.0f,   22},
    {"IN",      865.0f,   867.0f,   22},
    {"NZ_865",  864.0f,   868.0f,   22},
    {"TH",      920.0f,   925.0f,   16},
    {"UA_433",  433.0f,   434.7f,   10},
    {"UA_868",  868.0f,   868.6f,   14},
    {"MY_433",  433.0f,   435.0f,   20},
    {"MY_919",  919.0f,   924.0f,   22},
    {"SG_923",  917.0f,   925.0f,   20},
    {"PH_433",  433.0f,   434.7f,   10},
    {"PH_868",  868.0f,   869.4f,   14},
    {"PH_915",  915.0f,   918.0f,   22},
    {"KZ_433",  433.075f, 434.775f, 10},
    {"KZ_863",  863.0f,   868.0f,   22},
    {"NP_865",  865.0f,   868.0f,   22},
    {"BR_902",  902.0f,   907.5f,   22},
    {"LORA_24", 2400.0f,  2483.5f,  10},
};
const uint8_t kRegionCount = (uint8_t)(sizeof(kRegions) / sizeof(kRegions[0]));

uint8_t presetFromName(const char *name) {
    if (!name) return PRESET_LONG_FAST;
    for (uint8_t i = 0; i < PRESET_COUNT; i++) {
        if (strcmp(kPresets[i].name, name) == 0) return i;
    }
    return PRESET_LONG_FAST;
}

// djb2 string hash — the function Meshtastic uses to pick a frequency slot.
static uint32_t meshNameHash(const char *s) {
    uint32_t h = 5381;
    for (; s && *s; s++) h = (h * 33u) + (uint8_t)*s;
    return h;
}

static const RegionPlan *regionLookup(const char *code) {
    if (code) {
        for (uint8_t i = 0; i < kRegionCount; i++) {
            if (strcmp(kRegions[i].code, code) == 0) return &kRegions[i];
        }
    }
    return nullptr;
}

float regionSlotFreq(const char *code, float bwKhz, const char *channelName) {
    const RegionPlan *r = regionLookup(code);
    if (!r || bwKhz <= 0.0f) return MESH_FREQ;
    float bwMhz = bwKhz / 1000.0f;
    uint32_t numChannels = (uint32_t)((r->freqEnd - r->freqStart) / bwMhz);
    if (numChannels == 0) numChannels = 1;
    uint32_t slot = meshNameHash(channelName) % numChannels;
    return r->freqStart + (bwMhz / 2.0f) + (slot * bwMhz);
}

uint8_t regionPower(const char *code) {
    const RegionPlan *r = regionLookup(code);
    return r ? r->power : MESH_POWER;
}

void applyPresetParams(RhinoConfig &cfg) {
    if (cfg.modemPreset >= PRESET_COUNT) cfg.modemPreset = PRESET_LONG_FAST;
    const PresetParams &p = kPresets[cfg.modemPreset];
    cfg.loraBw = p.bw;
    cfg.loraSf = p.sf;
    cfg.loraCr = p.cr;
    // Operating frequency is the name-hashed channel slot, like Meshtastic.
    cfg.loraFreq = regionSlotFreq(cfg.region, p.bw, p.channelName);
}

// ── Storage paths ─────────────────────────────────────────────────────────────
static const char *kPath = "/camillia/config.yaml";
static const char *kWebCfgUser = "admin";
static bool sdReady = false;

#if defined(DEVICE_TLORA_PAGER_TFT)
namespace {
constexpr uint8_t kXl9555RegOut0 = 0x02;
constexpr uint8_t kXl9555RegOut1 = 0x03;
constexpr uint8_t kXl9555RegCfg0 = 0x06;
constexpr uint8_t kXl9555RegCfg1 = 0x07;

constexpr uint8_t kExpDrvEn    = 0;
constexpr uint8_t kExpAmpEn    = 1;
constexpr uint8_t kExpLoraEn   = 3;
constexpr uint8_t kExpGpsEn    = 4;
constexpr uint8_t kExpKbEn     = 8;
constexpr uint8_t kExpGpioEn   = 9;
constexpr uint8_t kExpSdDet    = 10;
constexpr uint8_t kExpSdPullen = 11;
constexpr uint8_t kExpSdEn     = 12;

uint8_t sPagerExpAddr = 0xFF;
// Runtime-discovered working profile for this boot.
int sPagerGoodProfile = -1;
int sPagerGoodSpeedIdx = -1;
bool sPagerPrefsLoaded = false;

static bool xl9555WriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static bool xl9555ReadReg(uint8_t addr, uint8_t reg, uint8_t &val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)addr, 1) != 1) return false;
    val = Wire.read();
    return true;
}

static void xl9555SetOutput(uint8_t pin, bool level, bool invertDirSense,
                            uint8_t &out0, uint8_t &out1,
                            uint8_t &cfg0, uint8_t &cfg1) {
    uint8_t bit = (uint8_t)(1U << (pin & 0x07));
    if (pin < 8) {
        if (invertDirSense) cfg0 |= bit;
        else                cfg0 &= (uint8_t)~bit;
        if (level) out0 |= bit;
        else       out0 &= (uint8_t)~bit;
    } else {
        if (invertDirSense) cfg1 |= bit;
        else                cfg1 &= (uint8_t)~bit;
        if (level) out1 |= bit;
        else       out1 &= (uint8_t)~bit;
    }
}

static void xl9555SetInput(uint8_t pin, bool invertDirSense,
                           uint8_t &cfg0, uint8_t &cfg1) {
    uint8_t bit = (uint8_t)(1U << (pin & 0x07));
    if (pin < 8) {
        if (invertDirSense) cfg0 &= (uint8_t)~bit;
        else                cfg0 |= bit;
    } else {
        if (invertDirSense) cfg1 &= (uint8_t)~bit;
        else                cfg1 |= bit;
    }
}

static bool pagerFindExpander() {
    if (sPagerExpAddr != 0xFF) return true;
    for (uint8_t a = 0x20; a <= 0x27; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            sPagerExpAddr = a;
            return true;
        }
    }
    return false;
}

static bool pagerApplyExpanderProfile(int profile) {
    if (!pagerFindExpander()) {
        Serial.println("[sd] pager expander not found (0x20-0x27)");
        return false;
    }

    bool invertDirSense = false;
    bool sdEnHigh = true;
    enum SdPullenMode : uint8_t { PULLEN_INPUT = 0, PULLEN_LOW = 1, PULLEN_HIGH = 2 };
    SdPullenMode pullenMode = PULLEN_INPUT;

    switch (profile) {
        case 0: invertDirSense = false; sdEnHigh = true;  pullenMode = PULLEN_INPUT; break;
        case 1: invertDirSense = false; sdEnHigh = false; pullenMode = PULLEN_INPUT; break;
        case 2: invertDirSense = false; sdEnHigh = true;  pullenMode = PULLEN_LOW;   break;
        case 3: invertDirSense = true;  sdEnHigh = true;  pullenMode = PULLEN_INPUT; break;
        case 4: invertDirSense = true;  sdEnHigh = false; pullenMode = PULLEN_INPUT; break;
        default: return false;
    }

    uint8_t out0 = 0xFF, out1 = 0xFF, cfg0 = 0xFF, cfg1 = 0xFF;
    (void)xl9555ReadReg(sPagerExpAddr, kXl9555RegOut0, out0);
    (void)xl9555ReadReg(sPagerExpAddr, kXl9555RegOut1, out1);
    (void)xl9555ReadReg(sPagerExpAddr, kXl9555RegCfg0, cfg0);
    (void)xl9555ReadReg(sPagerExpAddr, kXl9555RegCfg1, cfg1);

    // SD probing must only touch SD-specific lines. Reprogramming shared rails
    // here can blank the display after radio bring-up on some pager units.
    xl9555SetOutput(kExpSdEn, sdEnHigh, invertDirSense, out0, out1, cfg0, cfg1);
    xl9555SetInput(kExpSdDet, invertDirSense, cfg0, cfg1);
    if (pullenMode == PULLEN_INPUT) {
        xl9555SetInput(kExpSdPullen, invertDirSense, cfg0, cfg1);
    } else {
        xl9555SetOutput(kExpSdPullen, pullenMode == PULLEN_HIGH, invertDirSense,
                        out0, out1, cfg0, cfg1);
    }

    bool ok = xl9555WriteReg(sPagerExpAddr, kXl9555RegOut0, out0)
           && xl9555WriteReg(sPagerExpAddr, kXl9555RegOut1, out1)
           && xl9555WriteReg(sPagerExpAddr, kXl9555RegCfg0, cfg0)
           && xl9555WriteReg(sPagerExpAddr, kXl9555RegCfg1, cfg1);
    if (!ok) {
        Serial.printf("[sd] pager expander write failed addr=0x%02X profile=%d\n",
                      sPagerExpAddr, profile);
        return false;
    }

    Serial.printf("[sd] pager expander ready addr=0x%02X profile=%d\n",
                  sPagerExpAddr, profile);
    return true;
}
} // namespace
#endif

static bool ensureSdMounted() {
    if (sdReady) return true;
    return sdBegin();
}

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
    "CAMELLIA", "EVERGREEN", "EARTHEN", "SOLARIZED", "CRIMSON", "SCARLET_POP",
    "INK_WASH", "LAVENDAR_FIELDS", "WILD_FLOWERS", "QUIET_LUXURY", "MORNING_DEW", "WINTER_CHILL"
};
static const int kNumThemes = 12;

static const char *kThemeModeNames[] = {
    "DARK", "LIGHT"
};
static const int kNumThemeModes = 2;

static const char *kMsgAlertSoundNames[] = {
    "DEFAULT", "CHIRPY", "BASS", "OFF"
};
static const int kNumMsgAlertSounds = 4;

static uint8_t findName(const char *val, const char **table, int n) {
    for (int i = 0; i < n; i++)
        if (!strcmp(val, table[i])) return (uint8_t)i;
    return 0;
}

static uint8_t parseMsgAlertSound(const char *val) {
    if (!val || !val[0]) return MSG_ALERT_SOUND_DEFAULT;
    if (isdigit((unsigned char)val[0])) {
        return (uint8_t)constrain(atoi(val), 0, kNumMsgAlertSounds - 1);
    }
    return findName(val, kMsgAlertSoundNames, kNumMsgAlertSounds);
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
    while (n > 0 && src[n] != '\0' && utf8util::isContinuationByte((uint8_t)src[n])) {
        n--;
    }
    if (n > 0) memcpy(dst, src, n);
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
    utf8util::copyTruncate(cfg.nodeLong, sizeof(cfg.nodeLong), MY_LONG_NAME);
    utf8util::copyTruncate(cfg.nodeShort, sizeof(cfg.nodeShort), MY_SHORT_NAME);
    cfg.gpsEnabled   = (bool)MY_GPS_ENABLED;
    cfg.latI         = MY_LAT_I;
    cfg.lonI         = MY_LON_I;
    cfg.alt          = MY_ALT;
    cfg.modemPreset  = PRESET_LONG_FAST;
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
    cfg.gpsPollIntervalS  = MY_GPS_POLL_S;
    strncpy(cfg.region, MY_REGION, sizeof(cfg.region) - 1);
    cfg.region[sizeof(cfg.region) - 1] = '\0';
    strncpy(cfg.tzDef, MY_TZ_DEF, sizeof(cfg.tzDef) - 1);
    cfg.tzDef[sizeof(cfg.tzDef) - 1] = '\0';
    cfg.wifiEnabled        = MY_WIFI_ENABLED;
    cfg.wifiSsid[0]        = '\0';
    cfg.wifiPass[0]        = '\0';
    cfg.webCfgAuthEnabled  = false;  // authentication disabled by default
    strncpy(cfg.webCfgPass, "admin", sizeof(cfg.webCfgPass) - 1);
    cfg.webCfgPass[sizeof(cfg.webCfgPass) - 1] = '\0';
    cfg.screenOnSecs       = MY_SCREEN_ON_SECS;
    cfg.displayUnits       = MY_DISPLAY_UNITS;
    cfg.compassNorthTop    = MY_COMPASS_NORTH;
    cfg.flipScreen         = MY_FLIP_SCREEN;
    cfg.splashMelodyEnabled = MY_SPLASH_MELODY_ENABLED;
    cfg.msgAlertSound      = MY_MSG_ALERT_SOUND;
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
    cfg.mqttPort           = MY_MQTT_PORT;
    cfg.mqttTls            = MY_MQTT_TLS;
    cfg.isPowerSaving      = MY_POWER_SAVING;
    cfg.lsSecs             = MY_LS_SECS;
    cfg.minWakeSecs        = MY_MIN_WAKE_SECS;
    cfg.telDeviceEnabled   = MY_TEL_DEV_EN;
    cfg.telDeviceIntervalS = MY_TEL_DEV_INTV;
    cfg.telEnvEnabled      = HAS_ENV_SENSOR_TELEMETRY ? (bool)MY_TEL_ENV_EN : false;
    cfg.telEnvIntervalS    = MY_TEL_ENV_INTV;
    cfg.neighborInfoEnabled = MY_NEIGHBORINFO_EN;
    cfg.neighborInfoIntervalS = MY_NEIGHBORINFO_INTV;
    cfg.neighborInfoOverLora = MY_NEIGHBORINFO_LORA;
    cfg.cannedEnabled      = MY_CANNED_EN;
    strncpy(cfg.cannedMessages, MY_CANNED_MSGS, sizeof(cfg.cannedMessages) - 1);
    cfg.cannedMessages[sizeof(cfg.cannedMessages) - 1] = '\0';
    cfg.snfClientEnabled   = MY_SNF_CLIENT_EN;
    cfg.chatSpacing        = MY_CHAT_SPACING;
    cfg.debugAcks          = MY_DBG_ACKS;
    cfg.debugMessages      = MY_DBG_MESSAGES;
    cfg.debugGps           = MY_DBG_GPS;
}

// ── SD init ──────────────────────────────────────────────────
bool sdBegin() {
#if (SD_CS < 0)
    sdReady = false;
    Serial.println("[sd] disabled");
    return sdReady;
#else
    if (sdReady) return true;

    // Cardputer Cap shares SPI between LoRa and the SD slot, so keep both
    // chip selects deasserted before attempting to mount the card.
    SPI.begin(LORA_SPI_SCK, LORA_SPI_MISO, LORA_SPI_MOSI);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
#if (LORA_CS >= 0)
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
#endif
#if (TFT_CS >= 0)
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
#endif
    delay(8);

#if defined(DEVICE_TLORA_PAGER_TFT)
    // Prefer profile 3 first (validated on this pager), then fall back.
    static const int kProfiles[] = { 3, 2, 0, 1, 4 };
    static const uint32_t kSpeeds[] = { 4000000UL, 1000000UL, 400000UL };
    const int kSpeedCount = (int)(sizeof(kSpeeds) / sizeof(kSpeeds[0]));

    sdReady = false;

    auto loadPagerPrefs = [&]() {
        if (sPagerPrefsLoaded) return;
        sPagerPrefsLoaded = true;
        Preferences p;
        if (!p.begin("camillia", true)) return;
        int storedProfile = (int)p.getChar("sdProfile", -1);
        int storedSpeedIdx = (int)p.getUChar("sdSpeedIdx", 0xFF);
        p.end();

        if (storedProfile >= 0 && storedProfile <= 4) sPagerGoodProfile = storedProfile;
        if (storedSpeedIdx >= 0 && storedSpeedIdx < kSpeedCount) sPagerGoodSpeedIdx = storedSpeedIdx;
    };

    auto savePagerPrefs = [&](int profile, int speedIdx) {
        Preferences p;
        if (!p.begin("camillia", false)) return;
        p.putChar("sdProfile", (int8_t)profile);
        p.putUChar("sdSpeedIdx", (uint8_t)speedIdx);
        p.end();
    };

    auto tryMountWithProfile = [&](int profile, int preferredSpeedIdx) -> bool {
        if (!pagerApplyExpanderProfile(profile)) return false;
        delay(12);

        auto trySpeedIdx = [&](int si) -> bool {
            if (si < 0 || si >= kSpeedCount) return false;
            if (SD.begin(SD_CS, SPI, kSpeeds[si])) {
                sdReady = true;
                sPagerGoodProfile = profile;
                sPagerGoodSpeedIdx = si;
                savePagerPrefs(profile, si);
                Serial.printf("[sd] mounted using profile=%d speed=%lu\n",
                              profile, (unsigned long)kSpeeds[si]);
                return true;
            }
            return false;
        };

        if (trySpeedIdx(preferredSpeedIdx)) return true;
        for (int si = 0; si < kSpeedCount; si++) {
            if (si == preferredSpeedIdx) continue;
            if (trySpeedIdx(si)) return true;
        }
        return false;
    };

    loadPagerPrefs();

    bool triedProfile[5] = {false, false, false, false, false};
    auto markTried = [&](int profile) {
        if (profile >= 0 && profile <= 4) triedProfile[profile] = true;
    };
    auto wasTried = [&](int profile) -> bool {
        return (profile >= 0 && profile <= 4) ? triedProfile[profile] : false;
    };

    // First pass: try last known-good profile/speed from NVS (or in-memory cache).
    if (sPagerGoodProfile >= 0) {
        (void)tryMountWithProfile(sPagerGoodProfile, sPagerGoodSpeedIdx);
        if (sdReady) markTried(sPagerGoodProfile);
    }

    // Fallback pass: try known profile order, skipping the already-tried one.
    for (size_t pi = 0; pi < (sizeof(kProfiles) / sizeof(kProfiles[0])) && !sdReady; pi++) {
        int profile = kProfiles[pi];
        if (wasTried(profile)) continue;
        markTried(profile);
        (void)tryMountWithProfile(profile, -1);
    }
#else
    sdReady = SD.begin(SD_CS, SPI, 4000000);
    if (!sdReady) sdReady = SD.begin(SD_CS, SPI, 1000000);
#endif

    Serial.printf("[sd] %s cs=%d sck=%d miso=%d mosi=%d\n",
                  sdReady ? "mounted" : "not found",
                  SD_CS, LORA_SPI_SCK, LORA_SPI_MISO, LORA_SPI_MOSI);
    return sdReady;
#endif
}

// ── YAML serialise (Meshtastic CLI-compatible format) ─────────
void cfgToYaml(const RhinoConfig &cfg, String &out) {
    char tmp[96];
    out  = "# start of Meshtastic configure yaml\n";
    // WiFi credentials (for web config export/import portability)
    out += "wifi_ssid: "; out += cfg.wifiSsid; out += "\n";
    out += "wifi_pass: "; out += cfg.wifiPass; out += "\n";
    out += "webcfg_user: "; out += kWebCfgUser; out += "\n";
    snprintf(tmp, sizeof(tmp), "webcfg_auth_enabled: %s\n", cfg.webCfgAuthEnabled ? "true" : "false"); out += tmp;
    out += "webcfg_pass: "; out += cfg.webCfgPass; out += "\n";
    bool debugMonitor = cfg.debugAcks || cfg.debugMessages || cfg.debugGps;
    snprintf(tmp, sizeof(tmp), "debug_monitor: %s\n", debugMonitor ? "true" : "false"); out += tmp;
    snprintf(tmp, sizeof(tmp), "message_alert_sound: %s\n",
             kMsgAlertSoundNames[constrain((int)cfg.msgAlertSound, 0, kNumMsgAlertSounds - 1)]);
    out += tmp;
    snprintf(tmp, sizeof(tmp), "splash_melody_enabled: %s\n", cfg.splashMelodyEnabled ? "true" : "false");
    out += tmp;
    out += "config:\n";
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
    snprintf(tmp, sizeof(tmp), "    splashMelodyEnabled: %s\n", cfg.splashMelodyEnabled ? "true" : "false"); out += tmp;
    snprintf(tmp, sizeof(tmp), "    messageAlertSound: %s\n",
             kMsgAlertSoundNames[constrain((int)cfg.msgAlertSound, 0, kNumMsgAlertSounds - 1)]);
    out += tmp;
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
    out += "    modemPreset: ";
    out += kPresets[cfg.modemPreset < PRESET_COUNT ? cfg.modemPreset : 0].name;
    out += "\n";
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
    snprintf(tmp, sizeof(tmp), "    gpsPollIntervalSecs: %lu\n", (unsigned long)cfg.gpsPollIntervalS); out += tmp;
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
    out += "  storeForward:\n";
    snprintf(tmp, sizeof(tmp), "    client_enabled: %s\n", cfg.snfClientEnabled ? "true" : "false"); out += tmp;
    // MQTT subsection: include full bridge settings so export/import round-trips
    // all MQTT behavior and connectivity fields.
    out += "  mqtt:\n";
    snprintf(tmp, sizeof(tmp), "    enabled: %s\n", cfg.mqttEnabled ? "true" : "false"); out += tmp;
    out += "    address: "; out += cfg.mqttServer; out += "\n";
    snprintf(tmp, sizeof(tmp), "    port: %u\n", (unsigned)cfg.mqttPort); out += tmp;
    snprintf(tmp, sizeof(tmp), "    tlsEnabled: %s\n", cfg.mqttTls ? "true" : "false"); out += tmp;
    out += "    username: "; out += cfg.mqttUser; out += "\n";
    out += "    password: "; out += cfg.mqttPass; out += "\n";
    out += "    root: "; out += cfg.mqttRoot; out += "\n";
    snprintf(tmp, sizeof(tmp), "    encryptionEnabled: %s\n", cfg.mqttEncryption ? "true" : "false"); out += tmp;
    snprintf(tmp, sizeof(tmp), "    mapReportingEnabled: %s\n", cfg.mqttMapReport ? "true" : "false"); out += tmp;
    snprintf(tmp, sizeof(tmp), "    ok_to_mqtt: %s\n", cfg.okToMqtt ? "true" : "false"); out += tmp;
    snprintf(tmp, sizeof(tmp), "    ignore_mqtt: %s\n", cfg.ignoreMqtt ? "true" : "false"); out += tmp;
    out += "  telemetry:\n";
    snprintf(tmp, sizeof(tmp), "    deviceTelemetryEnabled: %s\n", cfg.telDeviceEnabled ? "true" : "false"); out += tmp;
    snprintf(tmp, sizeof(tmp), "    deviceUpdateInterval: %lu\n",  (unsigned long)cfg.telDeviceIntervalS);    out += tmp;
    snprintf(tmp, sizeof(tmp), "    environmentMeasurementEnabled: %s\n", cfg.telEnvEnabled ? "true" : "false"); out += tmp;
    snprintf(tmp, sizeof(tmp), "    environmentUpdateInterval: %lu\n",    (unsigned long)cfg.telEnvIntervalS);    out += tmp;
    out += "  neighbor_info:\n";
    snprintf(tmp, sizeof(tmp), "    enabled: %s\n", cfg.neighborInfoEnabled ? "true" : "false"); out += tmp;
    snprintf(tmp, sizeof(tmp), "    update_interval: %lu\n", (unsigned long)cfg.neighborInfoIntervalS); out += tmp;
    snprintf(tmp, sizeof(tmp), "    transmit_over_lora: %s\n", cfg.neighborInfoOverLora ? "true" : "false"); out += tmp;
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
        out += "  - name: "; out += nm; out += "\n";
        out += "    role: "; out += roleStr; out += "\n";
        out += "    key: "; out += b64buf; out += "\n";
        snprintf(tmp, sizeof(tmp), "    uplink: %s\n", ch.uplinkEnabled ? "true" : "false");
        out += tmp;
        snprintf(tmp, sizeof(tmp), "    downlink: %s\n", ch.downlinkEnabled ? "true" : "false");
        out += tmp;
        snprintf(tmp, sizeof(tmp), "    mute: %s\n", ch.muted ? "true" : "false");
        out += tmp;
        snprintf(tmp, sizeof(tmp), "    hash: %02x\n", ch.hash);
        out += tmp;
    }

    // User-managed list of ignored node IDs. Emitted as a comma-separated
    // hex list on one line so the parser can pick it up as a plain scalar.
    // Format: "ignored_nodes: aabbccdd,11223344" (empty when list is empty).
    out += "ignored_nodes: ";
    for (int i = 0; i < Ignored.count(); i++) {
        if (i > 0) out += ",";
        snprintf(tmp, sizeof(tmp), "%08lx", (unsigned long)Ignored.at(i));
        out += tmp;
    }
    out += "\n";
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
                    utf8util::copyTruncate(cfg.nodeLong, sizeof(cfg.nodeLong), val);
                else if (!strcmp(key, "owner_short"))
                    utf8util::copyTruncate(cfg.nodeShort, sizeof(cfg.nodeShort), val);
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
                else if (!strcmp(key, "webcfg_user")) {
                    // Username is currently fixed to admin; accept key for compatibility.
                }
                else if (!strcmp(key, "webcfg_auth_enabled")) {
                    cfg.webCfgAuthEnabled = parseBoolValue(val);
                }
                else if (!strcmp(key, "webcfg_pass")) {
                    strncpy(cfg.webCfgPass, val, sizeof(cfg.webCfgPass) - 1);
                    cfg.webCfgPass[sizeof(cfg.webCfgPass) - 1] = '\0';
                }
                else if (!strcmp(key, "debug_monitor")) {
                    bool en = parseBoolValue(val);
                    cfg.debugAcks = en;
                    cfg.debugMessages = en;
                    cfg.debugGps = en;
                }
                else if (!strcmp(key, "debug_acks"))
                    cfg.debugAcks = parseBoolValue(val);
                else if (!strcmp(key, "debug_messages"))
                    cfg.debugMessages = parseBoolValue(val);
                else if (!strcmp(key, "debug_gps"))
                    cfg.debugGps = parseBoolValue(val);
                else if (!strcmp(key, "message_alert_sound"))
                    cfg.msgAlertSound = parseMsgAlertSound(val);
                else if (!strcmp(key, "message_alert_beep")) {
                    // Backward-compatible legacy bool key.
                    cfg.msgAlertSound = parseBoolValue(val)
                        ? MSG_ALERT_SOUND_DEFAULT
                        : MSG_ALERT_SOUND_OFF;
                } else if (!strcmp(key, "splash_melody_enabled")) {
                    cfg.splashMelodyEnabled = parseBoolValue(val);
                }
                else if (!strcmp(key, "ignored_nodes")) {
                    // Comma-separated 8-hex-digit node IDs (empty allowed).
                    // Tokens may optionally include a leading '!' or "0x".
                    uint32_t ids[IgnoreList::kMax];
                    int n = 0;
                    const char *cur = val;
                    while (*cur && n < IgnoreList::kMax) {
                        while (*cur == ' ' || *cur == ',') cur++;
                        if (!*cur) break;
                        if (*cur == '!') cur++;
                        if (cur[0] == '0' && (cur[1] == 'x' || cur[1] == 'X')) cur += 2;
                        char *endp = nullptr;
                        unsigned long v = strtoul(cur, &endp, 16);
                        if (endp == cur) break;
                        if (v != 0 && v != 0xFFFFFFFFUL) {
                            ids[n++] = (uint32_t)v;
                        }
                        cur = endp;
                    }
                    Ignored.replace(ids, n);
                }
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
                    if      (!strcmp(key, "long"))  utf8util::copyTruncate(cfg.nodeLong, sizeof(cfg.nodeLong), val);
                    else if (!strcmp(key, "short")) utf8util::copyTruncate(cfg.nodeShort, sizeof(cfg.nodeShort), val);
                } else if (!strcmp(section, "position")) {
                    // Legacy format: stored as scaled int32 * 1e7
                    if      (!strcmp(key, "lat")) cfg.latI = (int32_t)atol(val);
                    else if (!strcmp(key, "lon")) cfg.lonI = (int32_t)atol(val);
                    else if (!strcmp(key, "alt")) cfg.alt  = (int32_t)atol(val);
                    else if (!strcmp(key, "gpsPollIntervalSecs")) cfg.gpsPollIntervalS = (uint32_t)atol(val);
                    else if (!strcmp(key, "gpsPollIntervalMs")) {
                        uint32_t ms = (uint32_t)atol(val);
                        cfg.gpsPollIntervalS = (ms == 0) ? 0 : ((ms + 999UL) / 1000UL);
                    }
                } else if (!strcmp(section, "lora")) {
                    // Legacy format
                    if      (!strcmp(key, "freq"))         cfg.loraFreq     = atof(val);
                    else if (!strcmp(key, "bw"))           cfg.loraBw       = atof(val);
                    else if (!strcmp(key, "sf"))           cfg.loraSf       = (uint8_t)atoi(val);
                    else if (!strcmp(key, "cr"))           cfg.loraCr       = (uint8_t)atoi(val);
                    else if (!strcmp(key, "power"))        cfg.loraPower    = (uint8_t)atoi(val);
                    else if (!strcmp(key, "hop_limit"))    cfg.loraHopLimit = (uint8_t)atoi(val);
                    else if (!strcmp(key, "modem_preset")) cfg.modemPreset  = presetFromName(val);
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
                } else if (!strcmp(key, "uplink")) {
                    CHANNEL_KEYS[chanIdx].uplinkEnabled = parseBoolValue(val);
                } else if (!strcmp(key, "downlink")) {
                    CHANNEL_KEYS[chanIdx].downlinkEnabled = parseBoolValue(val);
                } else if (!strcmp(key, "mute")) {
                    CHANNEL_KEYS[chanIdx].muted = parseBoolValue(val);
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
                else if (!strcmp(key, "modemPreset"))  cfg.modemPreset  = presetFromName(val);
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
                else if (!strcmp(key, "gpsPollIntervalSecs"))
                    cfg.gpsPollIntervalS = (uint32_t)atol(val);
                else if (!strcmp(key, "gpsPollIntervalMs")) {
                    uint32_t ms = (uint32_t)atol(val);
                    cfg.gpsPollIntervalS = (ms == 0) ? 0 : ((ms + 999UL) / 1000UL);
                }
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
                else if (!strcmp(key, "splashMelodyEnabled")) cfg.splashMelodyEnabled = (!strcmp(val,"true"));
                else if (!strcmp(key, "messageAlertSound")) cfg.msgAlertSound = parseMsgAlertSound(val);
                else if (!strcmp(key, "messageAlertBeep")) {
                    cfg.msgAlertSound = parseBoolValue(val)
                        ? MSG_ALERT_SOUND_DEFAULT
                        : MSG_ALERT_SOUND_OFF;
                }
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
                else if (!strcmp(key, "enabled"))            cfg.mqttEnabled    = parseBoolValue(val);
                else if (!strcmp(key, "port")) {
                    long p = atol(val);
                    if (p >= 1 && p <= 65535) cfg.mqttPort = (uint16_t)p;
                }
                else if (!strcmp(key, "tlsEnabled") || !strcmp(key, "tls")) cfg.mqttTls = parseBoolValue(val);
                else if (!strcmp(key, "encryptionEnabled"))  cfg.mqttEncryption = parseBoolValue(val);
                else if (!strcmp(key, "mapReportingEnabled"))cfg.mqttMapReport  = parseBoolValue(val);
                else if (!strcmp(key, "password"))           strncpy(cfg.mqttPass,    val, sizeof(cfg.mqttPass)    - 1);
                else if (!strcmp(key, "root"))               strncpy(cfg.mqttRoot,    val, sizeof(cfg.mqttRoot)    - 1);
                else if (!strcmp(key, "username"))           strncpy(cfg.mqttUser,    val, sizeof(cfg.mqttUser)    - 1);
                else if (!strcmp(key, "ok_to_mqtt"))         cfg.okToMqtt     = parseBoolValue(val);
                else if (!strcmp(key, "ignore_mqtt"))        cfg.ignoreMqtt   = parseBoolValue(val);
            } else if (!strcmp(section, "module_config") && !strcmp(subsection, "telemetry")) {
                if      (!strcmp(key, "deviceTelemetryEnabled"))        cfg.telDeviceEnabled   = (!strcmp(val,"true"));
                else if (!strcmp(key, "deviceUpdateInterval"))          cfg.telDeviceIntervalS = (uint32_t)atol(val);
                else if (!strcmp(key, "environmentMeasurementEnabled")) cfg.telEnvEnabled      = (!strcmp(val,"true"));
                else if (!strcmp(key, "environmentUpdateInterval"))     cfg.telEnvIntervalS    = (uint32_t)atol(val);
            } else if (!strcmp(section, "module_config") && !strcmp(subsection, "neighbor_info")) {
                if      (!strcmp(key, "enabled"))            cfg.neighborInfoEnabled = parseBoolValue(val);
                else if (!strcmp(key, "update_interval"))    cfg.neighborInfoIntervalS = (uint32_t)atol(val);
                else if (!strcmp(key, "transmit_over_lora")) cfg.neighborInfoOverLora = parseBoolValue(val);
            } else if (!strcmp(section, "module_config") && !strcmp(subsection, "cannedMessage")) {
                if (!strcmp(key, "enabled")) cfg.cannedEnabled = (!strcmp(val,"true"));
            } else if (!strcmp(section, "module_config") && !strcmp(subsection, "storeForward")) {
                if (!strcmp(key, "client_enabled")) cfg.snfClientEnabled = (!strcmp(val,"true"));
            } else if (!strcmp(section, "channels") && chanIdx >= 0 && chanIdx < MESH_CHANNELS) {
                if (!strcmp(key, "role"))
                    CHANNEL_KEYS[chanIdx].role = !strcmp(val,"SECONDARY") ? 1 : !strcmp(val,"DISABLED") ? 2 : 0;
            }
        }
    }

    cfg.msgAlertSound = (uint8_t)constrain((int)cfg.msgAlertSound, 0, kNumMsgAlertSounds - 1);
    if (cfg.telDeviceIntervalS < 3600UL) cfg.telDeviceIntervalS = 3600UL;
    if (cfg.telEnvIntervalS < 3600UL) cfg.telEnvIntervalS = 3600UL;
    if (cfg.neighborInfoIntervalS < NEIGHBORINFO_MIN_INTERVAL_S) {
        cfg.neighborInfoIntervalS = NEIGHBORINFO_MIN_INTERVAL_S;
    }
#if !HAS_ENV_SENSOR_TELEMETRY
    cfg.telEnvEnabled = false;
#endif
    // Only client roles are supported; coerce anything else from imported YAML.
    cfg.deviceRole = cfgCoerceClientRole(cfg.deviceRole);
    // Re-derive freq/BW/SF/CR from region + preset; any imported loraFreq is
    // advisory and must not override the name-hashed channel slot.
    applyPresetParams(cfg);
    return true;
}

// ── Export to SD ──────────────────────────────────────────────
bool cfgExport(const RhinoConfig &cfg) {
    if (!ensureSdMounted()) return false;
    SD.mkdir("/camillia");
    File f = SD.open(kPath, FILE_WRITE);
    if (!f) return false;
    String yaml;
    cfgToYaml(cfg, yaml);
    f.print(yaml);
    f.close();
    Serial.printf("[cfg] exported to %s (%u bytes)\n", kPath, (unsigned)yaml.length());
    return true;
}

// ── Import from SD ────────────────────────────────────────────
bool cfgImport(RhinoConfig &cfg) {
    if (!ensureSdMounted()) return false;
    File f = SD.open(kPath, FILE_READ);
    if (!f) return false;
    String content = f.readString();
    f.close();
    bool ok = cfgImportFromBuf(content.c_str(), content.length(), cfg);
    if (ok) Serial.printf("[cfg] imported from %s\n", kPath);
    return ok;
}

bool cfgSdConfigExists() {
    if (!ensureSdMounted()) return false;
    return SD.exists(kPath);
}
