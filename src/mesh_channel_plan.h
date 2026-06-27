#pragma once
#include <stdint.h>

// Meshtastic modem presets (Config_LoRaConfig_ModemPreset ordering).
enum ModemPreset : uint8_t {
    PRESET_LONG_FAST     = 0,
    PRESET_LONG_MODERATE = 1,
    PRESET_LONG_SLOW     = 2,
    PRESET_LONG_TURBO    = 3,
    PRESET_MEDIUM_FAST   = 4,
    PRESET_MEDIUM_SLOW   = 5,
    PRESET_SHORT_FAST    = 6,
    PRESET_SHORT_SLOW    = 7,
    PRESET_SHORT_TURBO   = 8,
    PRESET_COUNT         = 9
};

struct PresetParams {
    const char *name;         // human label, e.g. "Long Fast"
    const char *channelName;  // Meshtastic on-air channel name, e.g. "LongFast"
    float       bw;           // kHz
    uint8_t     sf;           // 7–12
    uint8_t     cr;           // coding-rate denominator (5 = 4/5 … 8 = 4/8)
};

// Per-region band edges (MHz) and hardware-capped TX power ceiling (dBm).
// Meshtastic derives the operating frequency from these via a name-hashed
// channel slot, so we store the band rather than a single fixed frequency.
struct RegionPlan {
    const char *code;
    float       freqStart;  // MHz
    float       freqEnd;    // MHz
    uint8_t     power;      // dBm
};

extern const PresetParams kPresets[PRESET_COUNT];
extern const RegionPlan   kRegions[];
extern const uint8_t      kRegionCount;

// Returns preset index for name, or PRESET_LONG_FAST if not found.
uint8_t presetFromName(const char *name);

// Computes the Meshtastic frequency-slot operating frequency (MHz) for a
// region code, bandwidth (kHz), and primary-channel name. The slot is
// hash(channelName) % numChannels, matching stock Meshtastic. Returns
// MESH_FREQ if the region is unknown.
float   regionSlotFreq(const char *code, float bwKhz, const char *channelName);

// Returns regulatory TX power ceiling for a region code, or MESH_POWER if unknown.
uint8_t regionPower(const char *code);
