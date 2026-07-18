# Hardware Targets

Camillia MT ships as five PlatformIO build envs across **four distinct boards** (the
two Heltec envs are the same hardware, different UI orientation). All share an
**ESP32-S3** SoC (dual-core Xtensa LX7 @ 240 MHz, 512 KB internal SRAM), the
`espressif32@6.7.0` / Arduino toolchain, and a **dual-slot OTA** flash layout — two
3.125 MB app partitions (`app0`/`app1`) + 64 KB NVS + 64 KB coredump
([partitions.csv](../partitions.csv)). The table below captures what differs.

Specs are cross-checked against the manufacturers' sites (see [Sources](#sources))
and reconciled with each board's build config in [platformio.ini](../platformio.ini)
and its [`src/hal/hw_*.h`](../src/hal/) pin map.

| Spec | T-Deck | T-LoRa Pager | Cardputer + LoRa-1262 Cap | Heltec V4 (expansion) |
| --- | --- | --- | --- | --- |
| **Build env** | `tdeck` | `tlora-pager-tft` | `cardputer-cap` | `heltec-v4`, `heltec-v4-vertical` |
| **MCU** | ESP32-S3FN16R8 | ESP32-S3 | ESP32-S3FN8 (StampS3) | ESP32-S3R2 |
| **PSRAM** | 8 MB octal | 8 MB (firmware uses quad `qio_qspi` access) | **None** | 2 MB |
| **Flash** | 16 MB | 16 MB | 8 MB | 16 MB |
| **Display** | 2.8″ ST7789 IPS, 320×240 (landscape) | 2.3″ ST7796 IPS, 480×222 (landscape) | 1.14″ ST7789V2, 240×135 | Onboard 0.96″ OLED is unused; Camillia drives the **TFT expansion** — ST7789 320×240 (custom init); vertical variant rotates the UI |
| **LoRa radio** | SX1262, shared SPI2 bus | SX1262 (default) / LR1121 (optional SKU, adds 2.4 GHz); power rail via XL9555 expander | SX1262 on M5 LoRa-1262 Cap (SPI3; PI4IOE5V6408 expander must arm it first) | SX1262 + external FEM (PA/LNA, TX/RX switch GPIOs); high-power SKU up to 28 dBm, low-power 22 dBm |
| **GNSS** | u-blox MIA-M10Q (UART1) | u-blox MIA-M10Q (UART1; rail via XL9555) | GPS on the LoRa/GPS cap (UART1 @ 115 200 baud) | External GNSS via SH1.25-8Pin connector (UART1) |
| **Input** | I²C QWERTY keyboard (0x55) + GT911 capacitive touch + optical trackball | TCA8418 matrix keyboard (backlit); no touch | Full QWERTY via M5Cardputer lib; no touch | CHSC6X capacitive touch + USER/side buttons; no keyboard |
| **Audio** | I²S speaker amp (MAX98357A / NS4168) | ES8311 I²S codec + speaker | M5Stack speaker driver (tones) | Passive buzzer (GPIO PWM) |
| **Battery sensing** | ADC resistor divider on GPIO4 | BQ25896 charger / fuel-gauge over I²C (no ADC pin) | 1520 mAh (120 mAh internal + 1400 mAh in base); read via M5Unified | ADC divider on GPIO1 + switched sense-enable (auto-polarity) |
| **Onboard sensors / extras** | Microphone | BHI260AP IMU + AI sensor, ST25R3916 NFC, RTC (onboard; not yet used by Camillia) | Microphone (via M5Unified) | BME280 / BMP280 / AHT20 — auto-detected over I²C (temp/humidity/pressure) |
| **microSD** | Yes (shared LoRa SPI) | Yes (shared SPI) | Yes (shared LoRa SPI) | No |
| **Sensor / GPIO headroom** | Minimal — one SPI bus shared by LoRa/TFT/SD, I²C runs keyboard/touch/trackball, UART is GPS; `USER_BUTTON_PIN = -1` | Minimal — most rails are XL9555-managed | Grove port available (may be claimed by the LoRa/GPS cap) | **Most headers exposed** — best candidate for add-on sensors (e.g. the Detection Sensor module) |
| **Vendor** | [LilyGo T-Deck](https://www.lilygo.cc/products/t-deck) | [LilyGo T-Lora Pager](https://lilygo.cc/products/t-lora-pager) | [M5Stack Cardputer](https://shop.m5stack.com/products/m5stack-cardputer-kit-w-m5stamps3) + Cap LoRa/GPS | [Heltec WiFi LoRa 32 V4](https://heltec.org/project/wifi-lora-32-v4/) + TFT expansion kit |

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

## Per-board HAL headers

Each board's full pin map and feature flags (`HAS_KEYBOARD`, `HAS_TOUCH`, `HAS_GPS`,
`HAS_SD_CARD`, LoRa/FEM pins, battery config, etc.) live in a dedicated HAL header:

| Board | HAL header |
| --- | --- |
| T-Deck | [`src/hal/hw_tdeck.h`](../src/hal/hw_tdeck.h) |
| T-LoRa Pager | [`src/hal/hw_tlora_pager.h`](../src/hal/hw_tlora_pager.h) |
| Cardputer + LoRa-1262 Cap | [`src/hal/hw_cardputer.h`](../src/hal/hw_cardputer.h) |
| Heltec V4 (expansion) | [`src/hal/hw_heltec_v4.h`](../src/hal/hw_heltec_v4.h) |

## Sources

Manufacturer spec pages used to verify the table above:

- LilyGo T-Deck — <https://wiki.lilygo.cc/products/t-deck-series/t-deck/>
- LilyGo T-LoRa Pager — <https://lilygo.cc/products/t-lora-pager> and <https://wiki.lilygo.cc/products/t-lora-series/t-lora-pager/>
- M5Stack Cardputer — <https://shop.m5stack.com/products/m5stack-cardputer-kit-w-m5stamps3>
- Heltec WiFi LoRa 32 V4 — <https://heltec.org/project/wifi-lora-32-v4/> and <https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32-v4/>
