# Camillia-MT

Meshtastic-compatible mesh radio firmware for ESP32-S3 handheld LoRa devices.

**Website:** <https://www.sumat.org/camillia>

## Table of Contents

- [Hardware](#hardware)
- [Supported Devices](#supported-devices)
- [Features](#features)
- [Build and Flash](#build-and-flash)
- [First-Time Setup](#first-time-setup)
- [Configuration](#configuration)
- [Roadmap](#roadmap)
- [Use of AI](#use-of-ai)
- [License](#license)
- [Usage and Controls Guide (docs/USE.md)](docs/USE.md)
- [Build and Flash Guide (docs/BUILD.md)](docs/BUILD.md)

## Hardware

- [LilyGo T-Deck](https://www.lilygo.cc/products/t-deck) — ESP32-S3, SX1262 LoRa, 320x240 display, physical keyboard, trackball, L76K GPS
- [LilyGo T-Lora Pager TFT](https://lilygo.cc/) — ESP32-S3, SX1262 LoRa, 480x222 TFT, physical keyboard, roller wheel + click, GPS
- [M5Stack Cardputer](https://shop.m5stack.com/products/m5stack-cardputer-kit-w-m5stamps3) + Cap LoRa/GPS module
- [Heltec](https://heltec.org/) WiFi LoRa 32 V4 + TFT expansion kit (Heltec V4 expansion profile)

No additional hardware required.

## Supported Devices

- LilyGo T-Deck (`tdeck`): keyboard + trackball + touch input, microSD config import/export, GPS, and full mesh UI support (channels, ANN, DMs, MAP, CFG, web config).
- LilyGo T-Lora Pager TFT (`tlora-pager-tft`): keyboard + roller wheel input, microSD config import/export, GPS, and full mesh UI support (channels, ANN, DMs, MAP, CFG, web config).
- M5Stack Cardputer + Cap LoRa/GPS (`cardputer-cap`): keyboard-driven input/navigation, microSD config import/export, GPS, and full mesh UI support (channels, ANN, DMs, MAP, CFG, web config).
- Heltec WiFi LoRa 32 V4 + TFT expansion kit (`heltec-v4`): touch-first UI, GPS, and full mesh UI support (channels, ANN, DMs, MAP, CFG, web config); microSD is not enabled in this profile.
- Heltec WiFi LoRa 32 V4 + TFT expansion kit, vertical UI (`heltec-v4-vertical`): same functionality as `heltec-v4` with a vertical-oriented UI layout.

Notes:
- All keyboard-specific shortcuts apply to keyboard builds (`tdeck`, `tlora-pager-tft`, and `cardputer-cap`).
- Environmental telemetry via BME280/BMP280/AHT20 is available on Heltec V4 expansion builds when a compatible sensor is present.

## Features

- **8 configurable LoRa channels** — each independently named, keyed, and color-coded
- **ANN tab** — read-only announcement feed (join/leave events, channel activity)
- **Web configuration** — browser-based settings UI served over Wi-Fi AP
- **YAML config** — import/export all settings and channel keys via microSD at `/camillia/config.yaml`

## First-Time Setup

On first boot, connect to the `camillia-mt` Wi-Fi access point, then open `http://192.168.4.1` in a browser. Set your node name, region, and channel keys. All settings are saved to the device and persist across reboots.

## Configuration

### Web config

Connect to the `camillia-mt` Wi-Fi access point and navigate to `http://192.168.4.1`. All settings (node identity, LoRa parameters, channel keys, etc.) can be configured here without reflashing. Changes persist across reboots.

### SD card

Export or import a full YAML configuration file via the **CFG** tab. The file is read from and written to `/camillia/config.yaml` on the microSD card.

## Roadmap

### Done

- [x] Tabs for channels
- [x] Tab for announcements
- [x] Tab for settings display
- [x] Web-based configuration UI (Wi-Fi AP + HTTP server)
- [x] YAML config import/export via microSD
- [x] Pretty IRC like interface
- [x] GPS
- [x] Consistent Node Information Storage
- [x] Direct messaging
- [x] Re-enable web config session authentication
- [x] Gate web config auto-start behind onboarding/settings/button
- [x] Themes
- [x] Traceroute (RouteDiscovery request + hop rendering)
- [x] Request NodeInfo from a peer (nodes-action menu)
- [x] Request Position from a peer (nodes-action menu)
- [x] Date markers in chat + DM history


### In Progress

- [ ] Re-enable web config session authentication

### Planned

- [ ] Persistent message history across reboots (write to SD)
- [ ] Position sharing (configurable interval, manual override)
- [ ] Theme builder through web config

#### Meshtastic parity — high impact

- [ ] BLE + phone client API (`ToRadio` / `FromRadio` protobuf framing for Android/iOS/web apps)
- [ ] PKI / Curve25519 per-node E2E DMs (X25519 + HKDF + AES-CTR; PKI channel 8; pubkey in NodeInfo)
- [ ] Waypoints (sharable map pins broadcast across the mesh)
- [ ] OTA firmware updates (DFU over BLE or HTTPS)
- [ ] Admin messages (remote config / reboot / shutdown over the mesh, secured via admin key)

#### Meshtastic parity — medium impact

- [ ] Modulation presets (LongFast, LongModerate, LongSlow, MediumFast, MediumSlow, ShortFast, ShortSlow, ShortTurbo)
- [ ] Region-aware frequency slot picker (channel-slot → MHz mapping)
- [ ] Multi-channel RX audit (decode all configured channel PSKs concurrently)
- [ ] Ham radio mode (disable encryption, set callsign, allow higher TX power per local regs)
- [ ] Traffic management (dedup, rate limiting, role-aware policing)
- [ ] Additional device roles (Router_Late, Repeater, ClientHidden, ClientMute, Tracker, Sensor, TAK, TAK_Tracker, Lost_and_Found)
- [ ] Power saving / deep sleep wiring (`isPowerSaving` / `lsSecs` / `minWakeSecs` → `esp_*_sleep_*`)
- [ ] Store & Forward server side (record-and-replay for offline peers)

#### Meshtastic parity — modules

- [ ] External Notification module (LED / buzzer / vibration on RX)
- [ ] Detection Sensor module (watch GPIO; broadcast on edge)
- [ ] Range Test module (periodic test packet + GPS for distance/SNR logging)
- [ ] Serial Module (UART passthrough — bridge external devices into the mesh)
- [ ] Remote Hardware module (read/write GPIO on a remote node over mesh)
- [ ] Audio module (Codec2 voice messages over mesh)
- [ ] Paxcounter module (anonymous BLE/WiFi people-counter)
- [ ] Ambient Lighting module (NCP5623 RGB LED control)

### Thinking About
- [ ] Wireless MQTT uplink/downlink support (may keep radio-only by default)


## Use of AI

Hello!  I've been a developer professionally since about 2001 working on a large list of technologies.  I've created this project in my spare time so I could contribute to my favorite new hobby (mesh networking) and try out coding with an AI partner (Claude).  Lots of this code has been touched by AI but as I go through the process I'm reviewing the code.  AI is tool, and like any other tool can be used well or used poorly.

This project is a bit more than a proof of concept but not something that has any commercial value.  I'm doing this for fun and to learn.  Feel free to contribute, use or ignore.

## License

Camillia-MT is released under the **GNU General Public License v3.0 or later**
(`SPDX-License-Identifier: GPL-3.0-or-later`).

Copyright © 2025–2026 Michael A. Cojocari and contributors.

This firmware is Meshtastic-compatible and links against, or interoperates
with, portions of the Meshtastic project (also GPLv3), so it is distributed
under the same license. There is **no warranty**; see the full license text
for details. The full license should accompany this source distribution in a
file named `LICENSE` (or `COPYING`); if it is missing, you can obtain a copy at
<https://www.gnu.org/licenses/gpl-3.0.html>.

Third-party libraries used by this project (e.g. LVGL, RadioLib, Arduino-ESP32,
TFT_eSPI / LovyanGFX, nanopb) remain under their respective licenses; see each
library's source for terms.
