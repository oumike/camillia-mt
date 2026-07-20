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
    uint32_t screenOnSecs;
    uint8_t  displayUnits;       // 0=METRIC, 1=IMPERIAL
    bool     compassNorthTop;
    bool     flipScreen;
    bool     splashMelodyEnabled;
    uint8_t  msgAlertSound;      // 0=DEFAULT, 1=CHIRPY, 2=BASS, 3=OFF
    uint8_t  uiTheme;            // UiThemeFamily
    uint8_t  uiMode;             // UiThemeMode
    uint8_t  chatStyle;          // 0=CLASSIC, 1=BUBBLES (applied at boot; needs reboot)
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

    // Chat display
    uint8_t  chatSpacing;   // 0=Tight(8px), 1=Normal(10px), 2=Loose(12px)

    // Serial debug categories
    bool     debugAcks;
    bool     debugMessages;
    bool     debugGps;
};

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

// Serialise cfg (and CHANNEL_KEYS[]) to YAML, appending into out.
void cfgToYaml(const RhinoConfig &cfg, String &out);

// Parse YAML from an in-memory buffer. Updates CHANNEL_KEYS[] and fills cfg.
// Returns true on success.
bool cfgImportFromBuf(const char *buf, size_t len, RhinoConfig &cfg);

// Mount SD card (call after SPI.begin). Returns true if card present.
bool sdBegin();

// Write /camillia/config.yaml. Returns true on success.
bool cfgExport(const RhinoConfig &cfg);

// Read /camillia/config.yaml.
// Updates CHANNEL_KEYS[] and fills cfg. Returns true on success.
bool cfgImport(RhinoConfig &cfg);

// Returns true if /camillia/config.yaml is present on the SD card. Mounts
// the card if needed; returns false if SD is unavailable.
bool cfgSdConfigExists();
