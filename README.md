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
- [Maps (docs/MAPS.md)](docs/MAPS.md)
- [Hardware Targets (docs/HARDWARE.md)](docs/HARDWARE.md)
- [Bluetooth Keyboards (docs/BLUETOOTH_KEYBOARDS.md)](docs/BLUETOOTH_KEYBOARDS.md)

## Hardware

- [LilyGo T-Deck](https://lilygo.cc/products/t-deck) — ESP32-S3, SX1262 LoRa, 320x240 display, physical keyboard, trackball, L76K GPS
- [LilyGo T-Deck Pro](https://lilygo.cc/products/t-deck-pro) — ESP32-S3, SX1262 LoRa, 240x320 e-paper touchscreen, physical keyboard, MIA-M10Q GPS
- [LilyGo T-Lora Pager TFT](https://lilygo.cc/products/t-lora-pager) — ESP32-S3, SX1262 LoRa, 480x222 TFT, physical keyboard, roller wheel + click, GPS
- [M5Stack Cardputer](https://shop.m5stack.com/products/m5stack-cardputer-kit-w-m5stamps3) + Cap LoRa/GPS module
- [Heltec](https://heltec.org/) WiFi LoRa 32 V4 + TFT expansion kit (Heltec V4 expansion profile)
- [Attaky Mesh Deck](https://shop.attaky.com/) — ESP32-S3, SX1262 LoRa, 320x240 touch display, 48-key QWERTY, D-pad, GPS
- [Elecrow ThinkNode M9](https://www.elecrow.com/thinknode-m9-meshcore-communication-terminal-with-full-keyboard-2-4inch-lcd-esp32-s3-lr1110-gps-2300mah.html) — ESP32-S3, **LR1110** LoRa, 2.4" 320x240 display, 37-key QWERTY + d-pad, GPS, 2300 mAh
- Seeed Wio Tracker L2 — ESP32-S3, SX1262 LoRa, 320x240 touch UI, GNSS, 16 MB flash and 8 MB PSRAM

No additional hardware required.

## Supported Devices

- LilyGo T-Deck (`tdeck`): keyboard + trackball + touch input, microSD config import/export, GPS, and full mesh UI support (channels, ANN, DMs, MAP, CFG, web config).
- LilyGo T-Deck Pro (`tdeck-pro`): e-paper touch UI with a fixed black-on-white, outline-only interface, T-Deck-compatible keyboard shortcuts, microSD config import/export, GPS, and full mesh UI support. Initial port; physical display/touch/radio validation is pending.
- LilyGo T-Lora Pager TFT (`tlora-pager-tft`): keyboard + roller wheel input, microSD config import/export, GPS, and full mesh UI support (channels, ANN, DMs, MAP, CFG, web config).
- M5Stack Cardputer + Cap LoRa/GPS (`cardputer-cap`): keyboard-driven input/navigation, microSD config import/export, GPS, and full mesh UI support (channels, ANN, DMs, MAP, CFG, web config).
- Heltec WiFi LoRa 32 V4 + TFT expansion kit (`heltec-v4`): touch-first UI, GPS, and full mesh UI support (channels, ANN, DMs, MAP, CFG, web config); microSD is not enabled in this profile.
- Heltec WiFi LoRa 32 V4 + TFT expansion kit, vertical UI (`heltec-v4-vertical`): same functionality as `heltec-v4` with a vertical-oriented UI layout.
- Attaky Mesh Deck (`mesh-deck`): keyboard + D-pad + touch input, GPS, and full mesh UI support; no microSD — config, DM history and the node archive live in internal flash.
- Elecrow ThinkNode M9 (`m9`): keyboard + d-pad input (no touch), microSD config import/export, GPS, and full mesh UI support (channels, ANN, DMs, MAP, CFG, web config). The only LR1110 board in the lineup.
- Seeed Wio Tracker L2 (`wio-tracker-l2`): bring-up target with a touch-first 320x240 UI, optional external BLE keyboard, ES8311 sound notifications, GNSS, browser VNC Host/Remote control, and 1-bit SD_MMC storage, including firmware config import/export at `/camillia/config.yaml`. LP5814 brightness, ADS1115 battery, audio, SD, BLE, and Remote support still need hardware verification.

Notes:
- All keyboard-specific shortcuts apply to keyboard builds (`tdeck`, `tdeck-pro`, `tlora-pager-tft`, `cardputer-cap`, `mesh-deck`, and `m9`).
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

Releases are cut by [the release workflow](.github/workflows/release.yml), run
manually from the Actions tab. It builds every device profile, merges factory
images, signs the OTA images, tags the commit, and publishes a GitHub release
with the `.bin`/`.sig` assets. Debug symbols (`.elf`) are uploaded as a
`debug-symbols-<tag>` workflow artifact rather than bloating the release by
~35MB per profile. T-Deck Pro publishes the `tdeck-pro` factory and OTA images;
the release script verifies the target list and every OTA signature before
publication.

Release notes are written and reviewed on your machine, then carried to the
release in `RELEASE_NOTES.md` — which has to be committed anyway, since the
build bakes it into the firmware so the device shows the same text that gets
published.

```bash
./release.sh --notes-only -y          # draft, review and edit the notes
git add RELEASE_NOTES.md && git commit -m "Release notes for v4.9.0" && git push
gh workflow run release.yml -f channel=stable -f version=4.9.0
```

`--notes-only` builds nothing, publishes nothing, and leaves `VERSION` alone —
it only writes the notes and prints the exact `gh workflow run` line to follow
it with. Pass that version to the workflow; left blank it derives its own, and
the notes would then be published under a different tag.

The workflow's `notes` input defaults to `committed`, which publishes that file
verbatim. Setting it to `generate` has the workflow write the notes itself,
unreviewed, and requires an `ANTHROPIC_API_KEY` secret.

Signing uses the `OTA_SIGNING_KEY` secret on the `release` environment; the
workflow refuses to publish if that key does not match the public key baked into
`src/ota_signing_pubkey.h`, since a mismatch would lock out every device in the
field.

[`release.sh`](release.sh) is what the workflow runs, and still cuts a release
entirely locally (it needs `ota_signing_key.pem` present):

```bash
./release.sh                    # prompts for a version, e.g. 3.2.0
./release.sh --version 4.9.0    # release exactly this version
./release.sh --alpha -y         # next alpha in the current series
./release.sh --check-targets    # validates release/OTA slugs without building
```

[The build workflow](.github/workflows/build.yml) compiles every environment
from a clean checkout on pushes and PRs. It is a breakage check on ordinary
work, not a release gate, and it never uploads release assets.

On-device OTA and the website flasher both track GitHub's *latest* release,
which is why an alpha publishes as a GitHub *prerelease* — that is what keeps it
off the stable channel.


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
