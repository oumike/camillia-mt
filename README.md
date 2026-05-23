# Camillia-MT

Meshtastic-compatible mesh radio firmware for ESP32-S3 handheld LoRa devices.

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
- Environmental telemetry via BME280 is available on Heltec V4 expansion builds when a compatible sensor is present.

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


### In Progress

- [ ] Re-enable web config session authentication

### Planned

- [ ] Persistent message history across reboots (write to SD)
- [ ] Position sharing (configurable interval, manual override)
- [ ] Theme builder through web config

### Thinking About
- [ ] Wireless MQTT uplink/downlink support (may keep radio-only by default)


## Use of AI

Hello!  I've been a developer professionally since about 2001 working on a large list of technologies.  I've created this project in my spare time so I could contribute to my favorite new hobby (mesh networking) and try out coding with an AI partner (Claude).  Lots of this code has been touched by AI but as I go through the process I'm reviewing the code.  AI is tool, and like any other tool can be used well or used poorly.

This project is a bit more than a proof of concept but not something that has any commercial value.  I'm doing this for fun and to learn.  Feel free to contribute, use or ignore.

## License

GNU General Public License v3.0 (GPLv3)
