// SPDX-License-Identifier: GPL-3.0-or-later
// Settings for the CardputerZero daemon, read from ~/.config/camillia/config.
// A commented default file is written on first run.
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

struct ChannelSetting {
    std::string name;
    uint8_t key[32] = {};
    uint8_t keyLen = 0;
};

struct Settings {
    std::string region;
    uint8_t preset = 0;          // PRESET_* (mesh_channel_plan.h)
    int hopLimit = 3;
    int txPower = 22;
    uint8_t role = 0;            // 0 CLIENT, 1 CLIENT_MUTE
    bool rxOnly = false;
    bool gps = true;
    std::string gpsDevice = "/dev/serial0";
    int gpsBaud = 115200;
    uint32_t positionIntervalS = 0;
    uint8_t positionPrecision = 32;
    uint32_t nodeInfoIntervalS = 0;
    std::vector<ChannelSetting> channels;   // empty: the preset's own channel
};

// Defaults, then the file at path (written with defaults if it is missing).
// Unknown keys and bad values are reported on stderr and ignored.
Settings loadSettings(const std::string &path);

// Applies channels to CHANNEL_KEYS, with the preset's channel when none are set.
void applyChannels(const Settings &s);

// Role names as Meshtastic spells them.
const char *roleName(uint8_t role);
