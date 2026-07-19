# Camillia-MT

Meshtastic-compatible mesh radio firmware for ESP32-S3 handheld LoRa devices.

**Website:** <https://camillia.sumat.org/>

## Table of Contents

- [Hardware](#hardware)
- [Supported Devices](#supported-devices)
- [Features](#features)
- [Build and Flash](#build-and-flash)
- [First-Time Setup](#first-time-setup)
- [Configuration](#configuration)
- [Releases](#releases)
- [Roadmap](#roadmap)
- [Use of AI](#use-of-ai)
- [License](#license)
- [Usage and Controls Guide (docs/USE.md)](docs/USE.md)
- [Build and Flash Guide (docs/BUILD.md)](docs/BUILD.md)
- [Hardware Targets (docs/HARDWARE.md)](docs/HARDWARE.md)

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

## Releases

Releases are cut with [`release.sh`](release.sh), which builds every device
profile, merges factory images, tags the commit, and publishes a GitHub release
with the `.bin`/`.elf` assets.

### Stable

```bash
./release.sh        # prompts for a version, e.g. 3.2.0
```

Publishes a normal release. On-device OTA and the website's **Stable** flasher
channel both track GitHub's *latest* release, so this is what most users get.

### Alpha (prerelease)

```bash
git checkout alpha
./release.sh --alpha    # prompts: New alpha version [v3.2.0-alpha.1]
```

`--alpha` must be run from the **`alpha`** branch (it aborts otherwise). It tags
`v<next-patch>-alpha.N` — auto-incrementing `N` — and publishes a GitHub
*prerelease*. Because GitHub excludes prereleases from *latest*, alpha builds are
invisible to on-device OTA and to the Stable flasher; they only appear when the
website flasher's **Channel** selector is set to **Alpha (bleeding-edge)**.

Alphas are disposable test builds — use them for validating changes before
stamping a real release.

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
- [x] Persistent message history across reboots
- [x] Position sharing (configurable interval, manual override)
- [x] Re-enable web config session authentication
- [x] Wireless MQTT uplink/downlink support (may keep radio-only by default)


### In Progress


### Planned
- [ ] Theme builder through web config

### Thinking About



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
for details. The full license text accompanies this source distribution in
[docs/LICENSE.md](docs/LICENSE.md); you can also obtain a copy at
<https://www.gnu.org/licenses/gpl-3.0.html>.

Third-party libraries used by this project (e.g. LVGL, RadioLib, Arduino-ESP32,
TFT_eSPI / LovyanGFX, nanopb) remain under their respective licenses; see each
library's source for terms.
