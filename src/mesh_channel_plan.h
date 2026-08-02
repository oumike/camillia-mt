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

// ── Custom (non-preset) modem settings ──────────────────────────────────────
// Bandwidth is stored the way Meshtastic stores it: an integer count of kHz,
// where two values are shorthand for a fractional bandwidth the radio actually
// produces — 31 is 31.25 kHz and 62 is 62.5 kHz. Keeping the code rather than
// the float means a number copied off a working node in the user's local mesh
// ("we run 62") means the same thing here as it does there.
extern const uint16_t kBwCodes[];      // supported by THIS board's radio, ascending
extern const uint8_t  kBwCodeCount;

// Spreading factor and coding-rate denominator limits for custom settings.
#define LORA_SF_MIN 7
#define LORA_SF_MAX 12
#define LORA_CR_MIN 5
#define LORA_CR_MAX 8

// Actual bandwidth in kHz for a Meshtastic bandwidth code, or 0 if this board's
// radio does not support it.
float    loraBwFromCode(uint16_t code);

// Nearest supported bandwidth code, for values arriving from YAML, an HTTP form
// or a config blob written by a build with a different radio.
uint16_t loraCoerceBwCode(uint16_t code);

// Name an unnamed primary channel takes while custom settings are active, and
// therefore what the frequency slot hashes. Meshtastic's Channels::getName()
// substitutes this same literal when use_preset is false, so a mesh running
// custom settings on an unnamed channel lands on the same slot as we do.
#define CUSTOM_CHANNEL_NAME "Custom"

// Computes the Meshtastic frequency-slot operating frequency (MHz) for a
// region code, bandwidth (kHz), and primary-channel name. The slot is
// hash(channelName) % numChannels, matching stock Meshtastic. Returns
// MESH_FREQ if the region is unknown.
float   regionSlotFreq(const char *code, float bwKhz, const char *channelName);

// How many frequency slots a region's band holds at the given bandwidth, or 0
// if the region is unknown. Narrow bandwidths give many more slots than the
// preset ones do (62.5 kHz over the US band is 416), which is why a custom
// setup usually pins the slot number instead of relying on the name hash.
uint32_t regionSlotCount(const char *code, float bwKhz);

// Center frequency (MHz) of a 0-based slot, clamped to the region's slot count.
// Returns MESH_FREQ if the region is unknown.
float   regionSlotFreqNum(const char *code, float bwKhz, uint32_t slot);

// Returns regulatory TX power ceiling for a region code, or MESH_POWER if unknown.
uint8_t regionPower(const char *code);
