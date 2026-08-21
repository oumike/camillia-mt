# Camillia for Meshtastic

Meshtastic-compatible mesh radio firmware for ESP32-S3 handheld LoRa devices.

**Website:** <https://camillia.sumat.org/>

## Table of Contents

- [Hardware](#hardware)
- [Supported Devices](#supported-devices)
- [Features](#features)
- [First-Time Setup](#first-time-setup)
- [Configuration](#configuration)
- [Releases](#releases)
- [Use of AI](#use-of-ai)
- [License](#license)
- [Usage and Controls Guide (docs/USE.md)](docs/USE.md)
- [Build and Flash Guide (docs/BUILD.md)](docs/BUILD.md)
- [Hardware Targets (docs/HARDWARE.md)](docs/HARDWARE.md)
- [Bluetooth Keyboards (docs/BLUETOOTH_KEYBOARDS.md)](docs/BLUETOOTH_KEYBOARDS.md)

## Hardware

- [LilyGo T-Deck](https://lilygo.cc/products/t-deck) — ESP32-S3, SX1262 LoRa, 320x240 display, physical keyboard, trackball, L76K GPS
- [LilyGo T-Lora Pager TFT](https://lilygo.cc/products/t-lora-pager) — ESP32-S3, SX1262 LoRa, 480x222 TFT, physical keyboard, roller wheel + click, GPS
- [M5Stack Cardputer](https://shop.m5stack.com/products/m5stack-cardputer-kit-w-m5stamps3) + Cap LoRa/GPS module
- [Heltec](https://heltec.org/) WiFi LoRa 32 V4 + TFT expansion kit (Heltec V4 expansion profile)
- [Attaky Mesh Deck](https://shop.attaky.com/) — ESP32-S3, SX1262 LoRa, 320x240 touch display, 48-key QWERTY, D-pad, GPS
- [Elecrow ThinkNode M9](https://www.elecrow.com/thinknode-m9-meshcore-communication-terminal-with-full-keyboard-2-4inch-lcd-esp32-s3-lr1110-gps-2300mah.html) — ESP32-S3, **LR1110** LoRa, 2.4" 320x240 display, 37-key QWERTY + d-pad, GPS, 2300 mAh

No additional hardware required.

## Supported Devices

- LilyGo T-Deck (`tdeck`): keyboard + trackball + touch input, microSD config import/export, GPS, and full mesh UI support (channels, ANN, DMs, MAP, CFG, web config).
- LilyGo T-Lora Pager TFT (`tlora-pager-tft`): keyboard + roller wheel input, microSD config import/export, GPS, and full mesh UI support (channels, ANN, DMs, MAP, CFG, web config).
- M5Stack Cardputer + Cap LoRa/GPS (`cardputer-cap`): keyboard-driven input/navigation, microSD config import/export, GPS, and full mesh UI support (channels, ANN, DMs, MAP, CFG, web config).
- Heltec WiFi LoRa 32 V4 + TFT expansion kit (`heltec-v4`): touch-first UI, GPS, and full mesh UI support (channels, ANN, DMs, MAP, CFG, web config); microSD is not enabled in this profile.
- Heltec WiFi LoRa 32 V4 + TFT expansion kit, vertical UI (`heltec-v4-vertical`): same functionality as `heltec-v4` with a vertical-oriented UI layout.
- Attaky Mesh Deck (`mesh-deck`): keyboard + D-pad + touch input, GPS, and full mesh UI support; no microSD — config, DM history and the node archive live in internal flash.
- Elecrow ThinkNode M9 (`m9`): keyboard + d-pad input (no touch), microSD config import/export, GPS, and full mesh UI support (channels, ANN, DMs, MAP, CFG, web config). The only LR1110 board in the lineup.

Notes:
- All keyboard-specific shortcuts apply to keyboard builds (`tdeck`, `tlora-pager-tft`, `cardputer-cap`, `mesh-deck`, and `m9`).
- Environmental telemetry via BME280/BMP280/AHT20 is available on Heltec V4 expansion builds when a compatible sensor is present.

## Features

- **8 configurable LoRa channels** — each independently named, keyed, and color-coded
- **ANN tab** — read-only announcement feed (join/leave events, channel activity)
- **Web configuration** — browser-based settings UI, on by default, served over the device's own Wi-Fi AP or your network
- **YAML config** — import/export all settings and channel keys via microSD at `/camillia/config.yaml`

## First-Time Setup

On first boot, connect to the `camillia-mt` Wi-Fi access point, then open `http://192.168.4.1` in a browser. Set your node name, region, and channel keys. All settings are saved to the device and persist across reboots.

## Configuration

### Web config

Web config is enabled by default on a new device, so a freshly flashed board comes up as the `camillia-mt` access point. Connect to it and navigate to `http://192.168.4.1`. All settings (node identity, LoRa parameters, channel keys, etc.) can be configured here without reflashing. Changes persist across reboots.

Once the device joins your own Wi-Fi network it serves the full page — the same settings plus Utilities, a live feed, chat, and the node map — at the address shown on the Config screen. In access-point mode it serves **Web Config Lite**: the complete settings form without those extra tabs, which do not fit in the memory left once Wi-Fi is running. The Cardputer always serves Lite, and pauses chat while web config is on; see [USE.md](docs/USE.md#web-config).

The Config screen's **Choose WiFi** list includes an **AP** entry that forces the device's own access point even when a network is configured, so web config stays reachable out of range. That choice persists across reboots.

### SD card

Export or import a full YAML configuration file via the **CFG** tab. The file is read from and written to `/camillia/config.yaml` on the microSD card.

## Releases

Releases are cut with [`release.sh`](release.sh), which builds every device
profile, merges factory images, signs the OTA images, tags the commit, and
publishes a GitHub release with the `.bin`/`.sig` assets. Debug symbols
(`.elf`/`.map`) stay in `dist/` on the release machine rather than bloating the
release by ~35MB per profile.

Pushing the tag also triggers [the build workflow](.github/workflows/build.yml),
which compiles every environment from a clean checkout. It is a breakage check,
not a release gate — `release.sh` publishes before CI finishes — and it never
uploads release assets, since only the release machine holds the OTA signing
key.

```bash
./release.sh        # prompts for a version, e.g. 3.2.0
```

Publishes a release. On-device OTA and the website flasher both track GitHub's
*latest* release.


## Use of AI

Hello!  I've been a developer professionally since about 2001 working on a large list of technologies.  I've created this project in my spare time so I could contribute to my favorite new hobby (mesh networking) and try out coding with an AI partner (Claude).  Lots of this code has been touched by AI but as I go through the process I'm reviewing the code.  AI is tool, and like any other tool can be used well or used poorly.

This project is a bit more than a proof of concept but not something that has any commercial value.  I'm doing this for fun and to learn.  Feel free to contribute, use or ignore.

## License

Camillia for Meshtastic is released under the **GNU General Public License v3.0 or later**
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
