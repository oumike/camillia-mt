### New
- Added initial LilyGo T-Display P4 V1.0 AMOLED support with separate `p4-amoled-sx1262` and `p4-amoled-lr2021` builds. Both use the ESP32-P4, 568x1232 RM69A10 display, GT9895 touch, L76K GPS, BQ27220 fuel gauge, four-bit SD_MMC, ESP32-C6 wireless coprocessor and optional 68-key keyboard.
- The P4 UI is tuned for its high-DPI rounded panel: portrait is the first-boot default, all three orientations are selectable, touch rotates with the display, headers and navigation avoid the curved edges, and landscape gains an anchored channel list plus a two-column Home dashboard.
- P4 Wi-Fi uses ESP-Hosted over the onboard ESP32-C6 and can reset the coprocessor when hosted networking stops responding. The matching C6 image is a separate release asset because P4 OTA cannot update the coprocessor.
- Discovery Sweep/Scan Settings now offers 30-minute, 1-hour, 2-hour and 6-hour runs in addition to the existing shorter durations. A long preset scan keeps the radio away from the node's own mesh for the entire selected window.
- Discovery can now **Save while discovering** on builds with file storage. The current run is rewritten at most every five seconds and once more when it finishes or is cancelled, preserving partial results through a power loss.
- Message Actions now includes **Sender Info**, showing whether a message came through LoRa or MQTT, its last relayer or gateway when known, hops, SNR and RSSI. Delivery details are retained for the most recent 64 messages this boot.
- Node Actions now includes **Share**, which sends another known node's name, identity and public key as a zero-hop NodeInfo. Shares stay on LoRa, are limited to one every five seconds and are disabled for the local node or unnamed nodes.

### Changed
- P4 factory and OTA assets are now radio-specific: `camillia-mt-p4-amoled-sx1262-*` and `camillia-mt-p4-amoled-lr2021-*`. Select the build matching the fitted radio; the former `tdisplay-p4` application target is no longer published.
- LR2021 builds now use the module's required 3.3 V TCXO setting, DIO11 host interrupt and internal DIO6/7/8/10 RF paths. SX1262 behavior remains on the original shared P4 wiring.
- The browser flasher exposes both P4 AMOLED radio variants with distinct downloads while sharing the same P4 product image and ESP32-C6 companion firmware.
- The release workflow now builds, merges, signs and verifies both P4 variants, archives their matching debug symbols, validates their OTA slugs and P4 flash layout, and uploads `flash.sh` plus the P4 C6 flashing helper. P4 packages use their isolated Arduino 3.x PlatformIO core.
- The OTA worker screen now uses substantially larger text and vertically centers status and progress content. Compact panels retain a smaller font so every row still fits.
- The Time and Date modal now has one action area instead of a second Save cell beside Hour and Minute. Touch-capable builds use the bottom Cancel/Save row; keyboard-only builds save with Enter or the roller, and Automatic mode gives back the space used by both manual date/time rows.
- P4 landscape Home now left-aligns the detailed weather block beneath the clock.
- Discovery progress uses minutes and hours for long runs, such as `43m/6h`, instead of displaying large raw second counts.
- Notification and alert tones now compute volume once per tone instead of once per audio sample, leaving more playback headroom on T-Deck, T-Lora Pager TFT and Wio Tracker L2.

### Fixed
- Web Config VNC now fits the remote canvas against both viewport width and height, so the P4's tall 568x1232 portrait display stays fully visible and pointer coordinates remain accurate after scaling or browser resizing.
- P4 GPS now uses the correct host-side UART direction, radio chip select is claimed before reset, and software restart performs a full system reset so the configured DSI host does not leave the AMOLED blank.
- Pressing Enter or the d-pad centre on Home no longer opens a message composer or Message Actions over the dashboard on ThinkNode M9. Wheel click and Tab no longer open those stray overlays on T-Lora Pager TFT.
- Action buttons, slider range labels and lock-screen glance cards now use readable colors on light themes instead of retaining dark-theme foregrounds or backgrounds.
- Opening Tools from the bottom navigation bar now clears Nodes, DMs or Config underneath it, so Discovery and other tool shortcuts no longer go to the hidden screen.
- Discovery's keyboard hint now changes from `C = Clear` to `C = Cancel` while a sweep or preset scan is running, matching the on-screen button.
- GPS debug logging no longer floods the serial console when a duty-cycle period below the supported minimum leaves GPS always on; the warning is emitted once when the setting changes.

> The T-Display P4 port remains hardware-verification pending. A successful
> build alone does not confirm AMOLED timing, touch orientation, hosted Wi-Fi,
> LoRa TX/RX, SD, battery sensing or the optional keyboard; use serial logs or
> measured behavior before marking those subsystems verified.
