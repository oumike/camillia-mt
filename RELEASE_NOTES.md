### New
- Mesh Deck: the keyboard is now optional. It is detected at boot and when plugged in or removed while running. Without a keyboard, the device switches to touch input and the on-screen keyboards. The D-pad and screen buttons keep working either way.
- `flash.sh` now accepts a board name, such as `./flash.sh tdeck-pro /dev/cu.usbmodem1101`, and picks the newest factory image for that board in the current folder.

### Changed
- The web config page now refuses changes, exports and file downloads when the request uses an address other than the device's IP, `localhost` or its hostname. Custom DNS aliases get a 403 error, so use the IP address instead.
- Web config: once Wi-Fi is set up, the onboarding page and config file uploads now require a login.
- Remote admin replies are now accepted only over PKI and only from the node being administered. The legacy admin channel is no longer supported.
- Unencrypted packets are now accepted only on channels you have actually set up.
- Packets that can't be decrypted no longer add new nodes to the node list.
- Config exports now end with a format marker. A truncated export is refused on import instead of being partly applied.
- Config exports are written safely: the previous file is kept until the new one is complete, so an interrupted save no longer loses your backup.
- Importing a config file with a bad or mismatched identity key now keeps your current identity and shows a warning. Keys with the Meshtastic CLI `base64:` prefix are now read correctly.
- Imported config files are capped at 32 KB, and out-of-range broadcast and GPS intervals are clamped to safe values.
- MQTT no longer freezes the screen while looking up or connecting to a broker. The broker lookup runs in the background, and connection attempts give up after a few seconds.
- T-Display P4: the ESP32-C6 coprocessor flasher now writes the firmware to the C6's app partition instead of overwriting its bootloader.
- `flash.sh` now refuses OTA or app-only images, which would leave a board unbootable if written as a factory image.

### Fixed
- Malformed radio packets can no longer crash the device.
- Traceroute results are now checked against the request they answer, so stray or malformed replies are ignored.
- Changing the theme while a dialog is open no longer crashes the device. The change waits until onboarding or the lock screen has closed.
- Settings now survive a downgrade. Wi-Fi networks, config, channels and custom themes saved by a newer version are kept instead of being reset. Older versions that save themes will still drop theme slots they don't know about.
- Cancelling the Wi-Fi password prompt while a connection attempt is running no longer causes errors.
- Very long broadcast intervals no longer make announcements go out immediately.
- Brownout and low-battery boot protection now works reliably on all boards and no longer triggers on a healthy device.
- Names, channel names and server addresses containing special characters now display correctly on the web config page and can no longer inject content into it.
- Heltec R8: the environment sensor scan no longer disables the touch screen or interferes with the SD card and buzzer pins.

Pull request: #120
