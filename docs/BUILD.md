# Build and Flash

## Requirements

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html) (CLI or IDE extension)
- USB cable connected to your target device

Dependencies are fetched automatically by PlatformIO on first build.


## Flashing a release binary

Download your device image and `flash.sh` from the [Releases](https://github.com/oumike/camillia-mt/releases) page.

Supported release file names:

- `camillia-mt-tdeck-vX.Y.Z.bin`
- `camillia-mt-tlora-pager-tft-vX.Y.Z.bin`
- `camillia-mt-cardputer-cap-vX.Y.Z.bin`
- `camillia-mt-heltec-vX.Y.Z.bin`
- `camillia-mt-heltec-vertical-vX.Y.Z.bin`
- `camillia-mt-mesh-deck-vX.Y.Z.bin`
- `camillia-mt-m9-vX.Y.Z.bin`

Then run:

```bash
./flash.sh camillia-mt-vX.Y.Z.bin [port]
```

Port defaults to `/dev/ttyUSB0`. On macOS use `/dev/cu.usbmodem*`.

`flash.sh` requires `esptool.py`:

```bash
pip install esptool
```

## Development
### Build and flash with PlatformIO

Build only (no flash):

```bash
pio run -e tdeck
pio run -e tlora-pager-tft
pio run -e cardputer-cap
pio run -e heltec-v4
pio run -e heltec-v4-vertical
pio run -e mesh-deck
pio run -e m9
```

Open serial monitor without rebuilding:

```bash
pio device monitor
```

### Build and flash with helper script

```bash
Usage: ./build-upload-monitor.sh [--tdeck|-t] [--debug|-d] [--cardputer|-C] [--pager|-P] [--heltec|-H] [--heltec-vertical|--vertical|-V] [--mesh-deck|--attaky|-M] [--m9|-9] [--erase|-E]
  --tdeck, -t  Use T-Deck environment (tdeck)
  --debug, -d   Use debug PlatformIO environment (tdeck-debug)
  --cardputer, -C  Use Cardputer + Cap LoRa/GPS environment (cardputer-cap)
  --pager, -P   Use T-Lora Pager TFT environment (tlora-pager-tft)
  --heltec, -H  Use Heltec V4 expansion environment (heltec-v4)
  --heltec-vertical, --vertical, -V  Use vertical Heltec env (heltec-v4-vertical)
  --mesh-deck, --attaky, -M  Use Attaky Mesh Deck environment (mesh-deck)
  --m9, -9      Use Elecrow ThinkNode M9 environment (m9)
                If neither is provided, you'll be prompted to choose a device.
  --erase, -E   Erase flash before clean build/upload
                M9 uses the combined upload_erase target.
```

Example usage:

```bash
./build-upload-monitor.sh --tdeck
./build-upload-monitor.sh --cardputer
./build-upload-monitor.sh --pager
./build-upload-monitor.sh --heltec
./build-upload-monitor.sh --vertical
./build-upload-monitor.sh --m9
```

You can also run the script with no flags and pick a device from the prompt.

## Environment

| Setting | Value |
|---|---|
| Platform | espressif32 7.0.1 |
| Framework | Arduino |
| Flash | 16 MB, dual-slot OTA partitions (8 MB on Cardputer) |
| PSRAM | enabled (OPI; none on Cardputer) |
| Upload speed | 115200 |

## Notes

- The board must be in download mode to flash. On the T-Deck, hold the trackball button while pressing reset, or let PlatformIO trigger it automatically via USB CDC.
- `-DARDUINO_USB_CDC_ON_BOOT=1` routes `Serial` over USB, no UART adapter needed.
- After flashing, the device boots directly into the firmware.

### ThinkNode M9

- **The console is an external UART bridge, not native USB-CDC.** The `m9` env
  builds with `ARDUINO_USB_CDC_ON_BOOT=0` for that reason; a build with CDC on
  boot sends its logs to a `ttyACM` port this board does not expose and looks
  completely silent.
- Erase and flash in one esptool session with `pio run -e m9 -t upload_erase`
  (or `./build-upload-monitor.sh --m9 --erase`). The separate `erase` target
  needs a second port grab this board does not always give up cleanly.
- First build on a fresh checkout may print
  `[patch_radiolib_lr11x0] NOT patched - run the build once more`. That is the
  RadioLib old-firmware patch running before PlatformIO has fetched RadioLib —
  run the build again and it lands. Skipping it shows up as `[radio] init
  failed: -706` on preproduction units.
