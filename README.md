# Camillia-MT

Meshtastic-compatible mesh radio firmware for ESP32-S3 handheld LoRa devices.

## Hardware

- [LilyGo T-Deck](https://www.lilygo.cc/products/t-deck) — ESP32-S3, SX1262 LoRa, 320x240 display, physical keyboard, trackball, L76K GPS
- Heltec WiFi LoRa 32 V4 + TFT expansion kit (Heltec V4 expansion profile)

No additional hardware required.

## Supported Devices

- LilyGo T-Deck (`tdeck`): keyboard + trackball + touch input, microSD config import/export, GPS, and full mesh UI support (channels, ANN, DMs, MAP, CFG, web config).
- M5Stack Cardputer + Cap LoRa/GPS (`cardputer-cap`): keyboard-driven input/navigation, microSD config import/export, GPS, and full mesh UI support (channels, ANN, DMs, MAP, CFG, web config).
- Heltec WiFi LoRa 32 V4 + TFT expansion kit (`heltec-v4`): touch-first UI, GPS, and full mesh UI support (channels, ANN, DMs, MAP, CFG, web config); microSD is not enabled in this profile.
- Heltec WiFi LoRa 32 V4 + TFT expansion kit, vertical UI (`heltec-v4-vertical`): same functionality as `heltec-v4` with a vertical-oriented UI layout.

Notes:
- All keyboard-specific shortcuts apply to keyboard builds (`tdeck` and `cardputer-cap`).
- Environmental telemetry via BME280 is available on Heltec V4 expansion builds when a compatible sensor is present.

## Features

- **8 configurable LoRa channels** — each independently named, keyed, and color-coded
- **ANN tab** — read-only announcement feed (join/leave events, channel activity)
- **Web configuration** — browser-based settings UI served over Wi-Fi AP
- **YAML config** — import/export all settings and channel keys via microSD at `/camillia/config.yaml`

## Flashing

Download the latest firmware from the [Releases](../../releases) page, or build and flash it directly using [PlatformIO](https://platformio.org/):

```
pio run -e tdeck --target upload --upload-port /dev/<tdeck-port>
```

For Heltec V4 expansion kit builds:

```
pio run -e heltec-v4 --target upload --upload-port /dev/<heltec-port>
```

For vertical Heltec UI builds (separate env):

```
pio run -e heltec-v4-vertical --target upload --upload-port /dev/<heltec-port>
```

Using the helper script:

```
./build-upload-monitor.sh --heltec
./build-upload-monitor.sh --vertical
```

Or, to build and monitor the serial output after flashing:

```
pio run -e tdeck --target upload --upload-port /dev/<port> && pio device monitor
```

After flashing, the device boots directly into the firmware. No build tools required.

## First-Time Setup

On first boot, connect to the `camillia-mt` Wi-Fi access point, then open `http://192.168.4.1` in a browser. Set your node name, region, and channel keys. All settings are saved to the device and persist across reboots.

## Controls

| Input | Action |
|---|---|
| Trackball left / right | Previous / next channel tab |
| Trackball up / down | Scroll messages |
| Trackball click | Confirm / send (context-dependent) |
| Enter | Send message |
| Backspace | Delete character |
| Tab | Cycle focus between message pane and node list |
| Alt + E | Toggle node list focus |

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
