# Camillia-MT

Meshtastic-compatible mesh radio firmware for the LilyGo T-Deck (ESP32-S3).

![Channel view](docs/screenshots/channel.png)
![Node list](docs/screenshots/nodes.png)
![GPS view](docs/screenshots/gps.png)
![Web config](docs/screenshots/webconfig.png)

## Hardware

- [LilyGo T-Deck](https://www.lilygo.cc/products/t-deck) — ESP32-S3, SX1262 LoRa, 320x240 display, physical keyboard, trackball, L76K GPS

No additional hardware required.

## Features

- **8 configurable LoRa channels** — each independently named, keyed, and color-coded
- **ANN tab** — read-only announcement feed (join/leave events, channel activity)
- **Web configuration** — browser-based settings UI served over Wi-Fi AP
- **YAML config** — import/export all settings and channel keys via microSD at `/camillia/config.yaml`

## Flashing

Download the latest firmware from the [Releases](../../releases) page. Flash with [esptool](https://github.com/espressif/esptool) or any ESP32 flashing tool (e.g. [ESP Flash Tool](https://www.espressif.com/en/support/download/other-tools)):

```
esptool.py --chip esp32s3 --port /dev/ttyUSB0 write_flash 0x0 camillia-mt.bin
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

### In Progress

- [ ] Direct messaging

### Planned

- [ ] Persistent message history across reboots (write to SD)
- [ ] Position sharing — configurable interval, manual override
- [ ] Themes
- [ ] Theme builder through web config

### Thinking about
- [ ] Wireless connectivity for MQTT Up/Download (I might just keep it radio only)


## Use of AI

Hello!  I've been a developer professionally since about 2001 working on a large list of technologies.  I've created this project in my spare time so I could contribute to my favorite new hobby (mesh networking) and try out coding with an AI partner (Claude).  Lots of this code has been touched by AI but as I go through the process I'm reviewing the code.  AI is tool, and like any other tool can be used well or used poorly.

This project is a bit more than a proof of concept but not something that has any commercial value.  I'm doing this for fun and to learn.  Feel free to contribute, use or ignore.

## License

GNU General Public License v3.0 (GPLv3)
