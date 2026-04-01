# Build

## Requirements

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html) (CLI or IDE extension)
- USB-C cable connected to the T-Deck

Dependencies are fetched automatically by PlatformIO on first build.

## Commands

Build and flash:
```
pio run -e tdeck -t upload
```

Build, flash, and open serial monitor (115200 baud):
```
./build-upload-monitor.sh
```

Build only (no flash):
```
pio run -e tdeck
```

Open serial monitor without rebuilding:
```
pio device monitor
```

## Environment

| Setting | Value |
|---|---|
| Platform | espressif32 6.7.0 |
| Framework | Arduino |
| Flash | 16 MB, `huge_app` partition |
| PSRAM | enabled (OPI) |
| Upload speed | 115200 |

## Flashing a release binary

Download `camillia-mt-vX.Y.Z.bin` and `flash.sh` from the [Releases](https://github.com/oumike/camillia-mt/releases) page, then:

```
./flash.sh camillia-mt-vX.Y.Z.bin [port]
```

Port defaults to `/dev/ttyUSB0`. On macOS use `/dev/cu.usbmodem*`. Requires `esptool.py` (`pip install esptool`).

## Notes

- The board must be in download mode to flash. On the T-Deck, hold the trackball button while pressing reset, or let PlatformIO trigger it automatically via USB CDC.
- `-DARDUINO_USB_CDC_ON_BOOT=1` routes `Serial` over USB; no UART adapter needed.
