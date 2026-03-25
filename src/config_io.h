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

// Mount SD card (call after SPI.begin). Returns true if card present.
bool sdBegin();

// Write /camillia/config.ini. Returns true on success.
bool cfgExport(const RhinoConfig &cfg);

// Read /camillia/config.ini.
// Updates CHANNEL_KEYS[] and fills cfg. Returns true on success.
bool cfgImport(RhinoConfig &cfg);
