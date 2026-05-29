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
pio run -e tdeck-lvgl-poc
pio run -e tlora-pager-tft-lvgl-poc
pio run -e cardputer-cap-lvgl-poc
```

Open serial monitor without rebuilding:

```bash
pio device monitor
```

### Build and flash with helper script

```bash
Usage: ./build-upload-monitor.sh [--tdeck|-t] [--debug|-d] [--cardputer|-C] [--pager|-P] [--heltec|-H] [--heltec-vertical|--vertical|-V] [--tdeck-lvgl] [--pager-lvgl] [--cardputer-lvgl] [--erase|-E]
  --tdeck, -t  Use T-Deck environment (tdeck)
  --debug, -d   Use debug PlatformIO environment (tdeck-debug)
  --cardputer, -C  Use Cardputer + Cap LoRa/GPS environment (cardputer-cap)
  --pager, -P   Use T-Lora Pager TFT environment (tlora-pager-tft)
  --heltec, -H  Use Heltec V4 expansion environment (heltec-v4)
  --heltec-vertical, --vertical, -V  Use vertical Heltec env (heltec-v4-vertical)
  --tdeck-lvgl       Use LVGL POC on T-Deck (tdeck-lvgl-poc)
  --pager-lvgl       Use LVGL POC on T-Lora Pager TFT (tlora-pager-tft-lvgl-poc)
  --cardputer-lvgl   Use LVGL POC on Cardputer + Cap LoRa/GPS (cardputer-cap-lvgl-poc)
                If neither is provided, you'll be prompted to choose a device.
  --erase, -E   Erase flash before clean build/upload
```

Example usage:

```bash
./build-upload-monitor.sh --tdeck
./build-upload-monitor.sh --cardputer
./build-upload-monitor.sh --pager
./build-upload-monitor.sh --heltec
./build-upload-monitor.sh --vertical
./build-upload-monitor.sh --tdeck-lvgl
./build-upload-monitor.sh --pager-lvgl
./build-upload-monitor.sh --cardputer-lvgl
```

You can also run the script with no flags and pick a device from the prompt.

## Environment

| Setting | Value |
|---|---|
| Platform | espressif32 6.7.0 |
| Framework | Arduino |
| Flash | 16 MB, `huge_app` partition |
| PSRAM | enabled (OPI) |
| Upload speed | 115200 |

## Notes

- The board must be in download mode to flash. On the T-Deck, hold the trackball button while pressing reset, or let PlatformIO trigger it automatically via USB CDC.
- `-DARDUINO_USB_CDC_ON_BOOT=1` routes `Serial` over USB, no UART adapter needed.
- After flashing, the device boots directly into the firmware.
