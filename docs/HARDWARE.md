# Hardware Targets

The table below covers **five distinct boards** across six build envs (the two
Heltec envs are the same hardware, different UI orientation). All share an
**ESP32-S3** SoC (dual-core Xtensa LX7 @ 240 MHz, 512 KB internal SRAM), the
`espressif32@7.0.1` / Arduino toolchain, and a **dual-slot OTA** flash layout — two
3.125 MB app partitions (`app0`/`app1`) + 64 KB NVS + 64 KB coredump
([partitions.csv](../partitions.csv)). The table below captures what differs.

The Attaky Mesh Deck (`mesh-deck`) also ships and is not yet in this table; its
pin map is in [`src/hal/hw_mesh_deck.h`](../src/hal/hw_mesh_deck.h).

Specs are cross-checked against the manufacturers' sites (see [Sources](#sources))
and reconciled with each board's build config in [platformio.ini](../platformio.ini)
and its [`src/hal/hw_*.h`](../src/hal/) pin map.

| Spec | T-Deck | T-LoRa Pager | Cardputer + LoRa-1262 Cap | Heltec V4 (expansion) | Elecrow ThinkNode M9 |
| --- | --- | --- | --- | --- | --- |
| **Build env** | `tdeck` | `tlora-pager-tft` | `cardputer-cap` | `heltec-v4`, `heltec-v4-vertical` | `m9` |
| **MCU** | ESP32-S3FN16R8 | ESP32-S3 | ESP32-S3FN8 (StampS3) | ESP32-S3R2 | ESP32-S3R8 |
| **PSRAM** | 8 MB octal | 8 MB (firmware uses quad `qio_qspi` access) | **None** | 2 MB | 8 MB octal |
| **Flash** | 16 MB | 16 MB | 8 MB | 16 MB | 16 MB |
| **Display** | 2.8″ ST7789 IPS, 320×240 (landscape) | 2.3″ ST7796 IPS, 480×222 (landscape) | 1.14″ ST7789V2, 240×135 | Onboard 0.96″ OLED is unused; Camillia drives the **TFT expansion** — ST7789 320×240 (custom init); vertical variant rotates the UI | 2.4″ ST7789 TFT-TN, 240×320 native, driven landscape 320×240; backlight enable is **active-low** (PNP) |
| **LoRa radio** | SX1262, shared SPI2 bus | SX1262 (default) / LR1121 (optional SKU, adds 2.4 GHz); power rail via XL9555 expander | SX1262 on M5 LoRa-1262 Cap (SPI3; PI4IOE5V6408 expander must arm it first) | SX1262 + external FEM (PA/LNA, TX/RX switch GPIOs); high-power SKU up to 28 dBm, low-power 22 dBm | **Semtech LR1110** — the only non-SX126x board here; shared SPI2 bus, RF switch on the radio's own DIO5/DIO6, TCXO 3.3 V |
| **GNSS** | u-blox MIA-M10Q (UART1) | u-blox MIA-M10Q (UART1; rail via XL9555) | GPS on the LoRa/GPS cap (UART1 @ 115 200 baud) | External GNSS via SH1.25-8Pin connector (UART1) | CC1167Q (UART1 @ 115 200 baud; multi-constellation GPS/BeiDou/Galileo/GLONASS), enable + reset lines both inverted |
| **Input** | I²C QWERTY keyboard (0x55) + GT911 capacitive touch + optical trackball | TCA8418 matrix keyboard (backlit); no touch | Full QWERTY via M5Cardputer lib; no touch | CHSC6X capacitive touch + USER/side buttons; no keyboard | 37-key QWERTY + d-pad via a companion matrix controller on its own I²C bus (0x6c); no touch, no trackball |
| **Audio** | I²S speaker amp (MAX98357A / NS4168) | ES8311 I²S codec + speaker | M5Stack speaker driver (tones) | Passive buzzer (GPIO PWM) | Passive buzzer (GPIO PWM) |
| **Battery sensing** | ADC resistor divider on GPIO4 | BQ25896 charger / fuel-gauge over I²C (no ADC pin) | 1520 mAh (120 mAh internal + 1400 mAh in base); read via M5Unified | ADC divider on GPIO1 + switched sense-enable (auto-polarity) | 2300 mAh cell; plain 1:2 ADC divider on GPIO13, no sense gate |
| **Onboard sensors / extras** | Microphone | BHI260AP IMU + AI sensor, ST25R3916 NFC, RTC (onboard; not yet used by Camillia) | Microphone (via M5Unified) | BME280 / BMP280 / AHT20 — auto-detected over I²C (temp/humidity/pressure) | PCF8563 RTC, QMI8658 IMU, QMC6309 compass (peripheral I²C; not yet used by Camillia), keypad backlight |
| **microSD** | Yes (shared LoRa SPI) | Yes (shared SPI) | Yes (shared LoRa SPI) | No — a 9.5 MB LittleFS partition in flash holds the same files (`partitions_16mb_fs.csv`) | Yes (shared LoRa SPI) |
| **Sensor / GPIO headroom** | Minimal — one SPI bus shared by LoRa/TFT/SD, I²C runs keyboard/touch/trackball, UART is GPS; `USER_BUTTON_PIN = -1` | Minimal — most rails are XL9555-managed | Grove port available (may be claimed by the LoRa/GPS cap) | **Most headers exposed** — best candidate for add-on sensors (e.g. the Detection Sensor module) | Minimal — one SPI bus shared by LoRa/TFT/SD, two I²C buses already claimed, UART is GPS |
| **Vendor** | [LilyGo T-Deck](https://www.lilygo.cc/products/t-deck) | [LilyGo T-Lora Pager](https://lilygo.cc/products/t-lora-pager) | [M5Stack Cardputer](https://shop.m5stack.com/products/m5stack-cardputer-kit-w-m5stamps3) + Cap LoRa/GPS | [Heltec WiFi LoRa 32 V4](https://heltec.org/project/wifi-lora-32-v4/) + TFT expansion kit | [Elecrow ThinkNode M9](https://www.elecrow.com/thinknode-m9-meshcore-communication-terminal-with-full-keyboard-2-4inch-lcd-esp32-s3-lr1110-gps-2300mah.html) |

> **Notes.**
> - **PSRAM/flash** are taken from the ESP32-S3 part number where the vendor lists it
>   (`FN16R8` = 16 MB flash + 8 MB octal PSRAM; `FN8` = 8 MB flash, no PSRAM;
>   `R2` = 2 MB PSRAM). The **Cardputer has no PSRAM**, which is why it carries the
>   tightest internal-DRAM budget for the LVGL pool and the TLS reserve that MQTT/OTA
>   need.
> - **GNSS:** LilyGo currently specs the u-blox **MIA-M10Q** on both the T-Deck and
>   Pager. Camillia parses any receiver as generic NMEA over UART, so the `L76K` label
>   in the HAL headers is just the driver — not a guarantee of the fitted chip, which
>   has varied across production batches.
> - **Heltec display:** the base V4 board ships a 0.96″ OLED that Camillia does not
>   use; the `heltec-v4` profiles target the ST7789 320×240 **TFT expansion**.
> - **M9 radio:** the LR1110 is the one radio here that is not an SX126x, and the
>   differences reach the firmware — no DIO2-as-RF-switch, no current-limit or
>   RX-boost setters, `setIrqAction()` in place of `setDio1Action()`, and an
>   antenna switch the chip drives itself from DIO5/DIO6 that has to be given a
>   truth table (see [`hw_m9.h`](../src/hal/hw_m9.h), which carries the warning
>   attached to it). Elecrow lists the flash as "16GB"; the part is an
>   ESP32-S3**R8** with **16 MB**.
> - **Channel count:** ten configurable channels everywhere except the Cardputer,
>   which keeps eight — each channel owns a `MAX_MSG_LINES` history ring, and on
>   the only board without PSRAM those rings come out of internal DRAM. Nothing
>   on the air depends on the number: a Meshtastic header carries a channel hash,
>   not a slot index. See issue #44.
> - **Node table:** 250 entries everywhere except the Cardputer, which holds 50.
>   `NodeEntry` is 168 bytes, so a full table is ~41 KB, and the `MAX_NODES`-sized
>   index, snapshot and id arrays around it (the Nodes screen, the DM picker, and
>   the NVS save/load paths) add ~10 KB more — about 41 KB reclaimed in total by
>   the cut. On the boards with PSRAM that is free; on the only one without, it came
>   out of the same internal DRAM the SoftAP and the web-config page need, and a
>   failing 24-byte DMA allocation during an AP session is what it cost. The mesh
>   does not notice: the table is local bookkeeping about who has been heard
>   recently, not anything the protocol carries. Favorites are evicted last, which
>   is the mitigation to reach for on that board. See issue #49.
> - **M9 receive ceiling:** the LR11x0's `SetPacketParams` `PayloadLength` field
>   is both "bytes to transmit" and "largest payload the receiver will accept".
>   RadioLib writes the TX length there on every transmit and never restores it
>   for explicit-header RX, so after one transmission the radio silently refuses
>   anything longer than its own last packet — no interrupt, no error. All RX
>   arming goes through `MeshRadio::_armRx()`, which puts the ceiling back to 255
>   first; do not call `startReceive()` directly. The SX126x ignores this field
>   on RX, which is why only this board was affected. See issue #43.
> - **M9 packet reads:** the LR11x0 hands out its 256-byte RX buffer as a ring —
>   `GetRxBufferStatus` returns a start offset that advances a few bytes per
>   packet — so a packet that starts late enough has its tail wrapped to the base
>   of the buffer. RadioLib reads it as one linear run and loses the wrapped
>   part, which drops long packets while short ones sail through and moves the
>   cutoff around as the offset creeps. `MeshRadio::_readPacketLr11xx()` reads
>   across the wrap in two parts instead; see it and issue #43 for the full
>   diagnosis. This is why `mesh_radio.h` subclasses the RadioLib driver.
> - **M9 console:** this board brings the serial console out through an external
>   UART bridge rather than native USB-CDC, so its env builds with
>   `ARDUINO_USB_CDC_ON_BOOT=0`. With CDC on boot the logs go to a `ttyACM` port
>   the board does not expose and it looks silent. GPIO19/20 are the S3's USB
>   pads and this board reuses 20/21 for the keyboard bus, which is why the
>   keyboard driver has to release the ROM's pad claim first.
> - **M9 preproduction radios:** units shipping with LR1110 transceiver firmware
>   older than `0x0308` reject RadioLib's `DriveDiosInSleepMode`, which aborts
>   `begin()` with `-706` on a perfectly healthy radio.
>   [`tools/patch_radiolib_lr11x0.py`](../tools/patch_radiolib_lr11x0.py) patches
>   that tolerance into the `m9` env's own RadioLib copy at build time; on a fresh
>   checkout the first build may run before RadioLib is fetched, in which case it
>   warns and lands on the next build.

## Per-board HAL headers

Each board's full pin map and feature flags (`HAS_KEYBOARD`, `HAS_TOUCH`, `HAS_GPS`,
`HAS_SD_CARD`, LoRa/FEM pins, battery config, etc.) live in a dedicated HAL header:

| Board | HAL header |
| --- | --- |
| T-Deck | [`src/hal/hw_tdeck.h`](../src/hal/hw_tdeck.h) |
| T-LoRa Pager | [`src/hal/hw_tlora_pager.h`](../src/hal/hw_tlora_pager.h) |
| Cardputer + LoRa-1262 Cap | [`src/hal/hw_cardputer.h`](../src/hal/hw_cardputer.h) |
| Heltec V4 (expansion) | [`src/hal/hw_heltec_v4.h`](../src/hal/hw_heltec_v4.h) |
| Attaky Mesh Deck | [`src/hal/hw_mesh_deck.h`](../src/hal/hw_mesh_deck.h) |
| Elecrow ThinkNode M9 | [`src/hal/hw_m9.h`](../src/hal/hw_m9.h) |

## Sources

Manufacturer spec pages used to verify the table above:

- LilyGo T-Deck — <https://wiki.lilygo.cc/products/t-deck-series/t-deck/>
- LilyGo T-LoRa Pager — <https://lilygo.cc/products/t-lora-pager> and <https://wiki.lilygo.cc/products/t-lora-series/t-lora-pager/>
- M5Stack Cardputer — <https://shop.m5stack.com/products/m5stack-cardputer-kit-w-m5stamps3>
- Heltec WiFi LoRa 32 V4 — <https://heltec.org/project/wifi-lora-32-v4/> and <https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32-v4/>
- Elecrow ThinkNode M9 — <https://www.elecrow.com/thinknode-m9-meshcore-communication-terminal-with-full-keyboard-2-4inch-lcd-esp32-s3-lr1110-gps-2300mah.html>. The pin map itself came from the M9 V1.0 schematic rather than this page.
