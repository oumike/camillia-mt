#pragma once
// Persistent configuration import/export and runtime config model.
#include <Arduino.h>
#include "config.h"
#include "mesh_proto.h"
#include "mesh_channel_plan.h"

enum UiThemeFamily : uint8_t {
    UI_THEME_CAMELLIA = 0,
    UI_THEME_EVERGREEN = 1,
    UI_THEME_EARTHEN = 2,
    UI_THEME_SOLARIZED = 3,
    UI_THEME_CRIMSON = 4,
    UI_THEME_SCARLET_POP = 5,
    UI_THEME_INK_WASH = 6,
    UI_THEME_LAVENDAR_FIELDS = 7,
    UI_THEME_WILD_FLOWERS = 8,
    UI_THEME_QUIET_LUXURY = 9,
    UI_THEME_MORNING_DEW = 10,
    UI_THEME_WINTER_CHILL = 11,
    UI_THEME_COUNT = 12
};

enum UiThemeMode : uint8_t {
    UI_MODE_DARK = 0,
    UI_MODE_LIGHT = 1
};

// Runtime config (loaded from SD or defaulted from compile-time #defines)
struct RhinoConfig {
    char     nodeLong[40];
    char     nodeShort[5];
    uint32_t nodeIdOverride;  // 0 = derive from MAC; non-zero = use this as myNodeId
    bool    gpsEnabled;           // use hardware GPS when available
    int32_t latI, lonI, alt;      // manual / last-known position (fallback)
    float   loraFreq, loraBw;
    uint8_t loraSf, loraCr, loraPower, loraHopLimit;
    uint8_t modemPreset;   // ModemPreset enum; drives loraBw/loraSf/loraCr on boot
    uint8_t  deviceRole;          // 0=CLIENT … 10=TAK_TRACKER
    uint8_t  rebroadcastMode;     // 0=ALL, 1=ALL_SKIP_DECODING, 2=LOCAL_ONLY, 3=KNOWN_ONLY
    bool     okToMqtt;            // set Data.bitfield OK_TO_MQTT bit on outgoing packets
    bool     ignoreMqtt;          // drop received packets with via_mqtt flag set
    uint32_t nodeInfoIntervalS;   // NodeInfo broadcast period (s), default 900
    uint32_t posIntervalS;        // Position broadcast period (s), default 1800
    uint32_t gpsPollIntervalS;    // GPS polling period (s), 0 = every loop
    char     region[12];          // Meshtastic region string, e.g. "US"

    // Device (additional)
    char     tzDef[48];

    // WiFi (web config)
    bool     wifiEnabled;        // master switch; gates web config + MQTT bridge
    char     wifiSsid[64];
    char     wifiPass[64];
    bool     webCfgAuthEnabled;  // require login for web config; off by default
    char     webCfgPass[64];

    // Display
    uint8_t  brightness;         // backlight percent, 10..100 in 10% steps
    uint32_t screenOnSecs;
    uint8_t  displayUnits;       // 0=METRIC, 1=IMPERIAL
    bool     compassNorthTop;
    bool     flipScreen;
    bool     splashMelodyEnabled;
    uint8_t  msgAlertSound;      // 0=DEFAULT, 1=CHIRPY, 2=BASS, 3=OFF
    uint8_t  uiTheme;            // UiThemeFamily
    uint8_t  uiMode;             // UiThemeMode
    uint8_t  chatStyle;          // 0=CLASSIC, 1=BUBBLES, 2=OUTLINE (applies live)
    uint8_t  chatNameStyle;      // 0=SHORT (4-char), 1=LONG (full node name) in chat
    bool     chatColorsEnabled;  // classic mode: use per-node text colors
    uint8_t  userMsgColor;       // own-message color: 0..15 = basic palette index,
                                 // 0xFF = adaptive default (theme yellow/amber)

    // Bluetooth
    bool     btEnabled;
    uint8_t  btMode;             // 0=RANDOM_PIN, 1=FIXED_PIN, 2=NO_PIN
    uint32_t btFixedPin;

    // Network
    char     ntpServer[48];
    bool     mqttEnabled;
    char     mqttServer[64];
    char     mqttUser[32];
    char     mqttPass[48];
    char     mqttRoot[48];
    bool     mqttEncryption;
    bool     mqttMapReport;
    uint16_t mqttPort;           // broker TCP port (8883 TLS, 1883 plaintext)
    bool     mqttTls;            // connect via WiFiClientSecure when set

    // Power
    bool     isPowerSaving;
    uint32_t lsSecs;
    uint32_t minWakeSecs;

    // Module: Telemetry
    bool     telDeviceEnabled;
    uint32_t telDeviceIntervalS;
    bool     telEnvEnabled;
    uint32_t telEnvIntervalS;

    // Module: Neighbor Info
    bool     neighborInfoEnabled;
    uint32_t neighborInfoIntervalS;
    bool     neighborInfoOverLora;

    // Module: Canned Messages
    bool     cannedEnabled;
    char     cannedMessages[200];

    // Module: Store and Forward (client)
    bool     snfClientEnabled;

    // Firmware updates
    bool     otaAutoCheckEnabled; // check for a newer release once per boot

    // Node management
    bool     nodeArchiveEnabled;  // preserve nodes evicted from the full table to SD
    bool     autoFavoriteEnabled; // auto-favorite nodes within autoFavoriteRangeM
    uint32_t autoFavoriteRangeM;  // auto-favorite distance threshold, meters

    // Chat display
    uint8_t  chatSpacing;   // 0=Tight(8px), 1=Normal(10px), 2=Loose(12px)
    uint8_t  fontSize;      // 0=Small, 1=Medium (default), 2=Large, 3=Extra Large (chat + DM)

    // Serial debug categories
    bool     debugAcks;
    bool     debugMessages;
    bool     debugGps;

    // APPEND-ONLY BELOW. Settings are stored as one NVS blob of this struct
    // (see kCfgBlobVersion in main_lvgl.cpp): a shorter stored blob is copied
    // into the front of the struct and anything newer keeps its default here.
    // Inserting or reordering a field silently misreads every existing device's
    // settings — add at the end, or bump the blob version.
    //
    // "At the end" is stricter than it looks: a new field must start at an
    // offset >= the PREVIOUS sizeof(RhinoConfig), not merely after the previous
    // last member. The stored blob is sizeof() bytes long, so it includes the
    // old struct's *trailing padding*, and the load memcpy's all of it over the
    // front of the struct. A field placed in those pad bytes is overwritten
    // with the old padding value instead of keeping its compiled default.
    //
    // That is not hypothetical: loraRxBoostedGain was first added at offset 879
    // against an old sizeof of 880, so it came back false on every device with
    // saved settings and quietly disabled RX boosted gain. Hence the explicit
    // pad below — keep new fields underneath it.
    uint8_t  volumePct;     // notification volume, 0..100 in 10% steps
    uint8_t  timeSource;    // TIME_SOURCE_AUTO / TIME_SOURCE_MANUAL
    uint8_t  _reservedPad0; // was trailing padding when sizeof was 880
    // SX1262 RX boosted gain. Worth ~2 dB of sensitivity for roughly 2 mA of
    // extra receive current — and the radio is in RX essentially all the time,
    // so this is a continuous cost. On by default to preserve existing range.
    bool     loraRxBoostedGain;
    // Stop the web config server after this many seconds with no HTTP request.
    // A connected station with modem power-save disabled (which web config
    // requires) is one of the largest continuous draws on the device, and it
    // otherwise runs until the user remembers to turn it off. 0 disables the
    // timeout.
    uint32_t webCfgIdleTimeoutS;
    // Park the GPS in standby between position samples (period = gpsPollIntervalS).
    // Off by default: the standby command dialect cannot be probed at runtime,
    // so this stays opt-in until verified on a given unit. See gpsSetDutyCycle().
    bool     gpsDutyCycleEnabled;
};

// Where the wall clock comes from. AUTO is NTP when there's a network path and
// GPS otherwise; MANUAL means the user set it and nothing may overwrite it.
enum : uint8_t {
    TIME_SOURCE_AUTO   = 0,
    TIME_SOURCE_MANUAL = 1,
};

static inline uint8_t cfgCoerceTimeSource(int v) {
    return (v == TIME_SOURCE_MANUAL) ? TIME_SOURCE_MANUAL : TIME_SOURCE_AUTO;
}

// Clamps a notification volume to range and snaps it to the nearest 10% step,
// mirroring cfgCoerceBrightness. Shared by the on-device slider, the web form
// and YAML import so an out-of-range value from any source lands somewhere sane.
static inline uint8_t cfgCoerceVolume(int pct) {
    if (pct < VOLUME_PCT_MIN) return VOLUME_PCT_MIN;
    if (pct > VOLUME_PCT_MAX) return VOLUME_PCT_MAX;
    return (uint8_t)(((pct + VOLUME_PCT_STEP / 2) / VOLUME_PCT_STEP) * VOLUME_PCT_STEP);
}

// Clamps a backlight percentage to the supported range and snaps it to the
// nearest 10% step. Shared by the on-device slider, the web form and YAML
// import so an out-of-range value from any source lands somewhere sane.
static inline uint8_t cfgCoerceBrightness(int pct) {
    if (pct < BRIGHTNESS_PCT_MIN) return BRIGHTNESS_PCT_MIN;
    if (pct > BRIGHTNESS_PCT_MAX) return BRIGHTNESS_PCT_MAX;
    return (uint8_t)(((pct + BRIGHTNESS_PCT_STEP / 2) / BRIGHTNESS_PCT_STEP)
                     * BRIGHTNESS_PCT_STEP);
}

// Backlight percent -> LGFX setBrightness() duty (0-255).
static inline uint8_t cfgBrightnessDuty(uint8_t pct) {
    return (uint8_t)((cfgCoerceBrightness(pct) * 255 + 50) / 100);
}

// Only client device roles are supported on this firmware. Values are the
// canonical Meshtastic enum positions so they stay wire-compatible.
//   0 = CLIENT, 1 = CLIENT_MUTE, 8 = CLIENT_HIDDEN
// Any other role is coerced to CLIENT.
static inline uint8_t cfgCoerceClientRole(uint8_t role) {
    return (role == 1 || role == 8) ? role : 0;
}

// Derives loraFreq/loraBw/loraSf/loraCr from cfg.region and cfg.modemPreset.
// Call after any config load to ensure radio params are consistent.
void applyPresetParams(RhinoConfig &cfg);

// Initialise from compile-time defaults. Call once before sdBegin().
void cfgInitDefaults(RhinoConfig &cfg);

// Serialise cfg (and CHANNEL_KEYS[], plus the identity keypair) to YAML,
// appending into out. The output carries the node's private key — an exported
// config is a secret, not just a settings file.
void cfgToYaml(const RhinoConfig &cfg, String &out);

// Parse YAML from an in-memory buffer. Updates CHANNEL_KEYS[] and fills cfg.
// Returns true on success.
bool cfgImportFromBuf(const char *buf, size_t len, RhinoConfig &cfg);

// True when the last import restored a complete identity keypair into
// myPubKey/myPrivKey. The parser only touches RAM, so the caller must write
// them to NVS for the identity to survive the reboot that follows an import.
bool cfgImportRestoredKeys();

// Mount SD card (call after SPI.begin). Returns true if card present.
bool sdBegin();

// Cached mount state — true if a card is currently mounted. Unlike sdBegin()
// this never probes the bus, so UI/web paths can ask cheaply and repeatedly.
bool sdCardMounted();

// Write /camillia/config.yaml. Returns true on success.
bool cfgExport(const RhinoConfig &cfg);

// Read /camillia/config.yaml.
// Updates CHANNEL_KEYS[] and fills cfg. Returns true on success.
bool cfgImport(RhinoConfig &cfg);

// Returns true if /camillia/config.yaml is present on the SD card. Mounts
// the card if needed; returns false if SD is unavailable.
bool cfgSdConfigExists();
