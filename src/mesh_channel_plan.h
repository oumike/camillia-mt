#pragma once
#include <stdint.h>

// Our own modem-preset ordering. NOT Meshtastic's — the two are related only
// through kPresetFromMeshtastic/presetToMeshtastic in config_io.cpp, and casting
// between them is always a bug.
//
// Values 0..8 are load-bearing: modemPreset is persisted to NVS as this index,
// so an existing config decodes against them. New presets are therefore
// appended rather than slotted into a tidier order.
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
    // Appended for Meshtastic 2.7/2.8. Lite and Narrow arrived in 2.7; Tiny and
    // Medium Turbo in 2.8.
    PRESET_LITE_FAST     = 9,
    PRESET_LITE_SLOW     = 10,
    PRESET_NARROW_FAST   = 11,
    PRESET_NARROW_SLOW   = 12,
    PRESET_TINY_FAST     = 13,
    PRESET_TINY_SLOW     = 14,
    PRESET_MEDIUM_TURBO  = 15,
    PRESET_COUNT         = 16
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
// spacing/padding are Meshtastic 2.7's region profiles, in MHz. Both are zero
// for every classic region, which is why the slot arithmetic could ignore them
// until now:
//
//   freqSlotWidth = spacing + 2*padding + bw
//   numFreqSlots  = round((freqEnd - freqStart + spacing) / freqSlotWidth)
//   freq          = freqStart + bw/2 + padding + slot * freqSlotWidth
//
// spacing is a gap between slots and before the first one; padding is a guard
// band inside each slot at both ends. PROFILE_LITE uses both to fit four
// 400 kHz-separated channels into EU_866; PROFILE_NARROW uses padding alone to
// widen a 62.5 kHz slot to the raster its band expects.
//
// overrideSlot pins the slot rather than hashing for it: 0 means hash the
// channel name (every classic region), and a value above 0 is a 1-based slot
// number the region always uses. Upstream also has -1 for "hash the preset
// name"; no region we carry uses it, so it is not implemented here.
struct RegionPlan {
    const char *code;
    float       freqStart;    // MHz
    float       freqEnd;      // MHz
    uint8_t     power;        // dBm
    float       spacing;      // MHz, gap between slots (0 for continuous spectrum)
    float       padding;      // MHz, guard at each end of a slot
    uint8_t     overrideSlot; // 1-based pinned slot; 0 = hash the channel name
};

extern const PresetParams kPresets[PRESET_COUNT];

// Whether this board's radio can actually run a preset. The Tiny presets sit at
// 15.6 kHz, which needs both a TCXO and an SX126x; a board failing either would
// take the setting, reconfigure, and go silent. Offering a preset that cannot
// work is worse than never listing it — see LORA_BW_CODE_MIN in config.h.
bool presetUsableOnThisRadio(uint8_t preset);
extern const RegionPlan   kRegions[];
extern const uint8_t      kRegionCount;

// Returns preset index for name, or PRESET_LONG_FAST if not found.
uint8_t presetFromName(const char *name);

// ── Meshtastic enum translation ─────────────────────────────────────────────
// Received MeshBeacon offers carry Meshtastic's own ModemPreset and RegionCode
// enums. These must be translated, never cast: the two preset orderings differ
// from index 1 onward (Meshtastic LONG_SLOW=1, ours PRESET_LONG_MODERATE=1), so
// a cast would quietly report the wrong preset to the user.
//
// Returns -1 for a preset with no local equivalent (Meshtastic's deprecated
// VERY_LONG_SLOW), so callers can say "unknown" rather than guess.
int presetFromMeshtastic(uint8_t meshtasticPreset);

// Region code string ("US", "EU_868", ...) for a Meshtastic RegionCode enum, or
// nullptr when unset/unknown. Our kRegions[].code strings already use the same
// names as the Meshtastic enum, so this is the whole translation.
const char *regionCodeFromMeshtastic(uint8_t meshtasticRegion);

// The other direction, for anything that has to report our settings in
// Meshtastic's own enums rather than act on theirs — the MQTT map report is the
// only caller today. Both read the same tables as the functions above, so the
// pairs cannot drift apart.
//
// Returns Meshtastic's ModemPreset value for one of our PRESET_* indices, or -1
// if we ever add a preset upstream does not have.
int presetToMeshtastic(uint8_t preset);

// Returns Meshtastic's RegionCode for a region code string, or 0 (UNSET) when
// the string is empty or names a region their enum does not carry.
uint8_t regionCodeToMeshtastic(const char *code);

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

// Inverse of loraBwFromCode: kHz -> Meshtastic bandwidth code. Answers for any
// radio, not only the one fitted here.
uint16_t loraBwToCode(float kHz);

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
