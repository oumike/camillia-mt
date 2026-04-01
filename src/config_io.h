#pragma once
#include <Arduino.h>
#include "config.h"
#include "mesh_proto.h"

// Runtime config (loaded from SD or defaulted from compile-time #defines)
struct RhinoConfig {
    char     nodeLong[40];
    char     nodeShort[5];
    uint32_t nodeIdOverride;  // 0 = derive from MAC; non-zero = use this as myNodeId
    bool    gpsEnabled;           // use hardware GPS when available
    int32_t latI, lonI, alt;      // manual / last-known position (fallback)
    float   loraFreq, loraBw;
    uint8_t loraSf, loraCr, loraPower, loraHopLimit;
    uint8_t  deviceRole;          // 0=CLIENT … 10=TAK_TRACKER
    uint8_t  rebroadcastMode;     // 0=ALL, 1=ALL_SKIP_DECODING, 2=LOCAL_ONLY, 3=KNOWN_ONLY
    uint32_t nodeInfoIntervalS;   // NodeInfo broadcast period (s), default 900
    uint32_t posIntervalS;        // Position broadcast period (s), default 1800
    char     region[12];          // Meshtastic region string, e.g. "US"

    // Device (additional)
    char     tzDef[48];

    // Display
    uint32_t screenOnSecs;
    uint8_t  displayUnits;       // 0=METRIC, 1=IMPERIAL
    bool     compassNorthTop;
    bool     flipScreen;

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

    // Power
    bool     isPowerSaving;
    uint32_t lsSecs;
    uint32_t minWakeSecs;

    // Module: Telemetry
    bool     telDeviceEnabled;
    uint32_t telDeviceIntervalS;
    bool     telEnvEnabled;
    uint32_t telEnvIntervalS;

    // Module: Canned Messages
    bool     cannedEnabled;
    char     cannedMessages[200];
};

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
