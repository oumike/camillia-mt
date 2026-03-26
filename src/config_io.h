#pragma once
#include <Arduino.h>
#include "config.h"
#include "mesh_proto.h"

// Runtime config (loaded from SD or defaulted from compile-time #defines)
struct RhinoConfig {
    char    nodeLong[40];
    char    nodeShort[5];
    int32_t latI, lonI, alt;
    float   loraFreq, loraBw;
    uint8_t loraSf, loraCr, loraPower, loraHopLimit;
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
