// SPDX-License-Identifier: GPL-3.0-or-later
// Modem presets, region band plans and Meshtastic frequency-slot arithmetic
// (declared in mesh_channel_plan.h). Split out of config_io.cpp, unchanged, so
// targets that have no config store (the CardputerZero Linux build) can use it.
#include "mesh_channel_plan.h"
#include "config.h"
#include "licensed_mode.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

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
    // Meshtastic 2.7/2.8 additions. Parameters and channel names are taken from
    // modemPresetToParams() and getModemPresetDisplayName() at protobufs
    // 7b2464c / firmware 7239fe8. The channel name is not cosmetic: it feeds
    // both the channel hash and the frequency-slot hash, so a byte that differs
    // from upstream puts us on a different frequency talking to nobody.
    {"Lite Fast",     "LiteFast",    125.0f,  9, 5},
    {"Lite Slow",     "LiteSlow",    125.0f, 10, 5},
    {"Narrow Fast",   "NarrowFast",   62.5f,  7, 6},
    {"Narrow Slow",   "NarrowSlow",   62.5f,  8, 6},
    // 15.6 kHz. Note the asymmetry with the other Fast/Slow pairs: upstream
    // gives TinyFast CR 4/5 and TinySlow CR 4/6, not a shared rate.
    {"Tiny Fast",     "TinyFast",     15.6f,  7, 5},
    {"Tiny Slow",     "TinySlow",     15.6f,  8, 6},
    {"Medium Turbo",  "MediumTurbo", 500.0f,  9, 5},
};

// Keeps LORA_BW_CODE_MIN's board list honest: MESH_RADIO_HAS_TCXO is what the
// preprocessor can branch on, MESH_TCXO_V is the value the radio is actually
// brought up with, and a board added to one but not the other would silently
// offer 15.6 kHz on a crystal.
static_assert((MESH_TCXO_V > 0.0f) == (MESH_RADIO_HAS_TCXO != 0),
              "MESH_RADIO_HAS_TCXO disagrees with MESH_TCXO_V for this board");

bool presetUsableOnThisRadio(uint8_t preset) {
    if (preset >= PRESET_COUNT) return false;
    // Compared as codes rather than kHz so this asks exactly the question
    // LORA_BW_CODE_MIN answers, and stays in step if a radio ever moves.
    return loraBwToCode(kPresets[preset].bw) >= LORA_BW_CODE_MIN;
}

// Band edges (freqStart, freqEnd in MHz) mirror Meshtastic's RegionInfo table;
// power is the SX1262 hardware-capped TX ceiling (dBm), <= the regulatory limit.
const RegionPlan kRegions[] = {
    //  code         start       end        pwr  spacing  padding  slot
    {"US",       902.0f,     928.0f,    22,  0.0f,    0.0f,    0},
    {"EU_433",   433.0f,     434.0f,    10,  0.0f,    0.0f,    0},
    {"EU_868",   869.4f,     869.65f,   22,  0.0f,    0.0f,    0},
    {"CN",       470.0f,     510.0f,    19,  0.0f,    0.0f,    0},
    {"JP",       920.5f,     923.5f,    13,  0.0f,    0.0f,    0},
    {"ANZ",      915.0f,     928.0f,    22,  0.0f,    0.0f,    0},
    {"ANZ_433",  433.05f,    434.79f,   14,  0.0f,    0.0f,    0},
    {"RU",       868.7f,     869.2f,    20,  0.0f,    0.0f,    0},
    {"KR",       920.0f,     923.0f,    22,  0.0f,    0.0f,    0},
    {"TW",       920.0f,     925.0f,    22,  0.0f,    0.0f,    0},
    {"IN",       865.0f,     867.0f,    22,  0.0f,    0.0f,    0},
    {"NZ_865",   864.0f,     868.0f,    22,  0.0f,    0.0f,    0},
    {"TH",       920.0f,     925.0f,    16,  0.0f,    0.0f,    0},
    {"UA_433",   433.0f,     434.7f,    10,  0.0f,    0.0f,    0},
    {"UA_868",   868.0f,     868.6f,    14,  0.0f,    0.0f,    0},
    {"MY_433",   433.0f,     435.0f,    20,  0.0f,    0.0f,    0},
    {"MY_919",   919.0f,     924.0f,    22,  0.0f,    0.0f,    0},
    {"SG_923",   917.0f,     925.0f,    20,  0.0f,    0.0f,    0},
    {"PH_433",   433.0f,     434.7f,    10,  0.0f,    0.0f,    0},
    {"PH_868",   868.0f,     869.4f,    14,  0.0f,    0.0f,    0},
    {"PH_915",   915.0f,     918.0f,    22,  0.0f,    0.0f,    0},
    {"KZ_433",   433.075f,   434.775f,  10,  0.0f,    0.0f,    0},
    {"KZ_863",   863.0f,     868.0f,    22,  0.0f,    0.0f,    0},
    {"NP_865",   865.0f,     868.0f,    22,  0.0f,    0.0f,    0},
    {"BR_902",   902.0f,     907.5f,    22,  0.0f,    0.0f,    0},
    {"LORA_24",  2400.0f,    2483.5f,   10,  0.0f,    0.0f,    0},
    // Meshtastic 2.7 additions, and the first regions whose slot arithmetic
    // uses spacing/padding at all — see RegionPlan.
    //
    // EU_866 is upstream's PROFILE_LITE: 400 kHz spacing plus 37.5 kHz padding
    // around a 125 kHz Lite preset, giving four channels at 865.7 / 866.3 /
    // 866.9 / 867.5 MHz. EU_N_868 is PROFILE_NARROW and pins slot 1 instead of
    // hashing — its band is 250 kHz wide, so there is exactly one place to be.
    {"EU_866",   865.6f,     867.6f,    22,  0.4f,    0.0375f, 0},
    {"EU_N_868", 869.4f,     869.65f,   22,  0.0f,    0.0104f, 1},
    // Amateur-band regions, from upstream 9bcf0f7 RadioInterface.cpp:286-349.
    // licensedOnly: selectable only in licensed mode (cfgCoerceLicensed and
    // regionSelectable enforce it). tinyPresets: the band plan expects the
    // 15.6 kHz Tiny presets rather than Narrow. Power is the same hardware
    // ceiling as the rows above, not a regulatory figure.
    //
    // The 2 m rows sit below kRadioMinMhz (150 MHz), the floor of every radio
    // we currently build for, so regionUsableOnThisRadio() rejects them; they
    // are carried so an imported or offered 2 m region is recognised.
    //  code          start     end      pwr  spacing  padding   slot  licOnly tiny
    {"ITU1_2M",    144.0f,   146.0f,  22,  0.0f,    0.0022f,  26,   true,   true},
    {"ITU2_2M",    144.0f,   148.0f,  22,  0.0f,    0.0022f,  51,   true,   true},
    {"ITU3_2M",    144.0f,   148.0f,  22,  0.0f,    0.0022f,  33,   true,   true},
    {"ITU2_125CM", 220.0f,   225.0f,  22,  0.0f,    0.01875f, 37,   true,   false},
    {"ITU1_70CM",  430.0f,   440.0f,  22,  0.0f,    0.01875f, 37,   true,   false},
    {"ITU2_70CM",  420.0f,   450.0f,  22,  0.0f,    0.01875f, 137,  true,   false},
    {"ITU3_70CM",  430.0f,   450.0f,  22,  0.0f,    0.01875f, 37,   true,   false},
};
const uint8_t kRegionCount = (uint8_t)(sizeof(kRegions) / sizeof(kRegions[0]));

uint8_t presetFromName(const char *name) {
    if (!name) return PRESET_LONG_FAST;
    for (uint8_t i = 0; i < PRESET_COUNT; i++) {
        if (strcmp(kPresets[i].name, name) == 0) return i;
    }
    return PRESET_LONG_FAST;
}

// Meshtastic bandwidth codes this board's radio can produce, ascending. The two
// narrowest are conditional: 16 (15.6 kHz, the Tiny presets) needs a TCXO and an
// SX126x, and 31 (31.25 kHz) is absent on the LR1121 pager variant. Both are
// decided by LORA_BW_CODE_MIN in config.h.
const uint16_t kBwCodes[] = {
#if LORA_BW_CODE_MIN <= 16
    16,
#endif
#if LORA_BW_CODE_MIN <= 31
    31,
#endif
    62, 125, 250, 500,
};
const uint8_t kBwCodeCount = (uint8_t)(sizeof(kBwCodes) / sizeof(kBwCodes[0]));

float loraBwFromCode(uint16_t code) {
    for (uint8_t i = 0; i < kBwCodeCount; i++) {
        if (kBwCodes[i] != code) continue;
        // The three shorthand codes; the rest are already the literal kHz.
        if (code == 16) return 15.6f;
        if (code == 31) return 31.25f;
        if (code == 62) return 62.5f;
        return (float)code;
    }
    return 0.0f;
}

// Inverse of loraBwFromCode, and the same mapping as upstream's bwKHzToCode():
// a bandwidth that is not a whole number of kHz has a code that is not its
// value. Ranges rather than equality because these arrive as floats — 15.6 in
// particular is not representable exactly, and upstream reads 15.5..15.7 as 16.
// Independent of kBwCodes, so it answers for any radio, not just this board's;
// presetUsableOnThisRadio() is what narrows it to what is fitted here.
uint16_t loraBwToCode(float kHz) {
    if (kHz > 15.5f  && kHz < 15.7f)  return 16;
    if (kHz > 31.1f  && kHz < 31.4f)  return 31;
    if (kHz > 62.4f  && kHz < 62.6f)  return 62;
    return (uint16_t)(kHz + 0.5f);
}

uint16_t loraCoerceBwCode(uint16_t code) {
    for (uint8_t i = 0; i < kBwCodeCount; i++) {
        if (kBwCodes[i] == code) return code;
    }
    // Unsupported: snap to the nearest supported code rather than silently
    // falling back to the default, so importing 31 on an LR1121 lands on 62
    // (the closest thing that radio can do) instead of jumping to 250.
    uint16_t best = kBwCodes[0];
    uint32_t bestDist = (uint32_t)abs((int32_t)best - (int32_t)code);
    for (uint8_t i = 1; i < kBwCodeCount; i++) {
        uint32_t d = (uint32_t)abs((int32_t)kBwCodes[i] - (int32_t)code);
        if (d < bestDist) { best = kBwCodes[i]; bestDist = d; }
    }
    return best;
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

// Width of one slot, including whatever guard the region's profile requires.
static float regionSlotWidthMhz(const RegionPlan &r, float bwKhz) {
    return r.spacing + (r.padding * 2.0f) + (bwKhz / 1000.0f);
}

uint32_t regionSlotCount(const char *code, float bwKhz) {
    const RegionPlan *r = regionLookup(code);
    if (!r || bwKhz <= 0.0f) return 0;
    const float slotWidth = regionSlotWidthMhz(*r, bwKhz);
    if (slotWidth <= 0.0f) return 0;
    // roundf, not truncation. Upstream rounds, and the two agree for every
    // classic region at the bandwidths we used to support — which is why this
    // went unnoticed. They stop agreeing at 15.6 kHz: US spans 26 MHz, so the
    // quotient is 1666.67 and the choice is between 1666 slots and 1667. The
    // count is the modulus for the channel-name hash, so picking the wrong one
    // puts us on a different frequency from every stock node in the region.
    uint32_t numChannels =
        (uint32_t)lroundf((r->freqEnd - r->freqStart + r->spacing) / slotWidth);
    return numChannels == 0 ? 1 : numChannels;
}

float regionSlotFreqNum(const char *code, float bwKhz, uint32_t slot) {
    const RegionPlan *r = regionLookup(code);
    uint32_t numChannels = regionSlotCount(code, bwKhz);
    if (!r || numChannels == 0) return MESH_FREQ;
    if (slot >= numChannels) slot = numChannels - 1;
    // Centre of the slot: the band edge, plus half a bandwidth to reach the
    // middle of the first channel, plus the profile's leading guard. Slots are
    // then spaced by the full slot width, not by the bandwidth — for a padded
    // profile those differ, and using the bandwidth would let the spacing drift
    // further out of step with every slot.
    return r->freqStart + (bwKhz / 2000.0f) + r->padding +
           ((float)slot * regionSlotWidthMhz(*r, bwKhz));
}

float regionSlotFreq(const char *code, float bwKhz, const char *channelName) {
    uint32_t numChannels = regionSlotCount(code, bwKhz);
    if (numChannels == 0) return MESH_FREQ;
    // A region that pins a slot uses it instead of the hash. overrideSlot is
    // 1-based, matching upstream's channel_num, so it converts here.
    const RegionPlan *r = regionLookup(code);
    if (r && r->overrideSlot > 0) {
        return regionSlotFreqNum(code, bwKhz, (uint32_t)r->overrideSlot - 1);
    }
    return regionSlotFreqNum(code, bwKhz, meshNameHash(channelName) % numChannels);
}

uint8_t regionPower(const char *code) {
    const RegionPlan *r = regionLookup(code);
    return r ? r->power : MESH_POWER;
}

const RegionPlan *regionPlan(const char *code) { return regionLookup(code); }

bool regionUsableOnThisRadio(const char *code) {
    const RegionPlan *r = regionLookup(code);
    return r && r->freqStart >= lic::kRadioMinMhz;
}

bool regionSelectable(const char *code, bool selfLicensed) {
    const RegionPlan *r = regionLookup(code);
    return r && lic::regionSelectable(r->licensedOnly, r->freqStart, selfLicensed);
}

bool presetAllowedInRegion(const char *code, uint8_t preset) {
    const RegionPlan *r = regionLookup(code);
    if (!r || !r->licensedOnly) return true;
    if (r->tinyPresets) return preset == PRESET_TINY_FAST || preset == PRESET_TINY_SLOW;
    return preset == PRESET_NARROW_FAST || preset == PRESET_NARROW_SLOW;
}

uint8_t regionDefaultPreset(const char *code) {
    const RegionPlan *r = regionLookup(code);
    if (!r || !r->licensedOnly) return PRESET_LONG_FAST;
    return r->tinyPresets ? PRESET_TINY_FAST : PRESET_NARROW_SLOW;
}


// Meshtastic ModemPreset enum -> our PRESET_* index. Deliberately a table, not
// a cast: the orderings diverge at index 1 (theirs LONG_SLOW, ours
// LONG_MODERATE) and again at 3, 5 and 7. Index into this by their value.
// -1 marks a preset we have no equivalent for.
static const int8_t kPresetFromMeshtastic[] = {
    PRESET_LONG_FAST,      // 0 LONG_FAST
    PRESET_LONG_SLOW,      // 1 LONG_SLOW (deprecated in 2.7 upstream, still carried
                           //   on air with unchanged params, so still translated)
    -1,                    // 2 VERY_LONG_SLOW (deprecated in 2.5, no local equivalent)
    PRESET_MEDIUM_SLOW,    // 3 MEDIUM_SLOW
    PRESET_MEDIUM_FAST,    // 4 MEDIUM_FAST
    PRESET_SHORT_SLOW,     // 5 SHORT_SLOW
    PRESET_SHORT_FAST,     // 6 SHORT_FAST
    PRESET_LONG_MODERATE,  // 7 LONG_MODERATE
    PRESET_SHORT_TURBO,    // 8 SHORT_TURBO
    PRESET_LONG_TURBO,     // 9 LONG_TURBO
    PRESET_LITE_FAST,      // 10 LITE_FAST     (2.7)
    PRESET_LITE_SLOW,      // 11 LITE_SLOW     (2.7)
    PRESET_NARROW_FAST,    // 12 NARROW_FAST   (2.7)
    PRESET_NARROW_SLOW,    // 13 NARROW_SLOW   (2.7)
    PRESET_TINY_FAST,      // 14 TINY_FAST     (2.8)
    PRESET_TINY_SLOW,      // 15 TINY_SLOW     (2.8)
    PRESET_MEDIUM_TURBO,   // 16 MEDIUM_TURBO  (2.8)
};

int presetFromMeshtastic(uint8_t meshtasticPreset) {
    if (meshtasticPreset >= (sizeof(kPresetFromMeshtastic) / sizeof(kPresetFromMeshtastic[0]))) return -1;
    return (int)kPresetFromMeshtastic[meshtasticPreset];
}

// Meshtastic RegionCode enum -> region code string. Index is their enum value;
// 0 is UNSET. Entries we have no band plan for still return their name, so the
// UI can say what was offered even when we could not tune to it.
static const char *const kRegionFromMeshtastic[] = {
    nullptr,    // 0  UNSET
    "US",       // 1
    "EU_433",   // 2
    "EU_868",   // 3
    "CN",       // 4
    "JP",       // 5
    "ANZ",      // 6
    "KR",       // 7
    "TW",       // 8
    "RU",       // 9
    "IN",       // 10
    "NZ_865",   // 11
    "TH",       // 12
    "LORA_24",  // 13
    "UA_433",   // 14
    "UA_868",   // 15
    "MY_433",   // 16
    "MY_919",   // 17
    "SG_923",   // 18
    "PH_433",   // 19
    "PH_868",   // 20
    "PH_915",   // 21
    "ANZ_433",  // 22
    "KZ_433",   // 23
    "KZ_863",   // 24
    "NP_865",   // 25
    "BR_902",   // 26
    // 27/28 are the ITU 2 m ham regions, 33-37 the rest of the ham block.
    // Upstream marks them licensed_only. They translate here like any other
    // region, but adopting one is gated by cfgCoerceLicensed(): without
    // licensed mode (or on a radio that cannot reach the band) the region is
    // put back to MY_REGION rather than transmitted on.
    "ITU1_2M",  // 27 (ham, licensed only)
    "ITU2_2M",  // 28 (ham, licensed only; renamed from ITU23_2M)
    "EU_866",   // 29
    // 30 and 31 are enum reservations upstream with no band table behind them.
    nullptr,    // 30 EU_874     (reserved upstream, no band)
    nullptr,    // 31 EU_917     (reserved upstream, no band)
    "EU_N_868", // 32
    "ITU3_2M",    // 33 (ham, licensed only)
    "ITU1_70CM",  // 34 (ham, licensed only)
    "ITU2_70CM",  // 35 (ham, licensed only)
    "ITU3_70CM",  // 36 (ham, licensed only)
    "ITU2_125CM", // 37 (ham, licensed only)
};

const char *regionCodeFromMeshtastic(uint8_t meshtasticRegion) {
    if (meshtasticRegion >= (sizeof(kRegionFromMeshtastic) / sizeof(kRegionFromMeshtastic[0]))) return nullptr;
    return kRegionFromMeshtastic[meshtasticRegion];
}

int presetToMeshtastic(uint8_t preset) {
    // Reverse scan of the same table, rather than a second table listing the
    // pairs the other way round: two tables would be two things to update when
    // a preset is added, and the one that got missed would be silent.
    for (uint8_t i = 0; i < (sizeof(kPresetFromMeshtastic) / sizeof(kPresetFromMeshtastic[0])); i++) {
        if (kPresetFromMeshtastic[i] >= 0 && (uint8_t)kPresetFromMeshtastic[i] == preset) return (int)i;
    }
    return -1;
}

uint8_t regionCodeToMeshtastic(const char *code) {
    if (!code || !code[0]) return 0;   // UNSET
    for (uint8_t i = 1; i < (sizeof(kRegionFromMeshtastic) / sizeof(kRegionFromMeshtastic[0])); i++) {
        if (kRegionFromMeshtastic[i] && strcmp(kRegionFromMeshtastic[i], code) == 0) return i;
    }
    return 0;
}
