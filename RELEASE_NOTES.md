### New
- Support for the Attaky Mesh Deck 1.0 — ESP32-S3 core with the LoRa/GPS expansion, 48-key keyboard and Standard Cell battery. Display, touch, both keyboard halves, the D-pad, the radio, GPS and battery all work; build and flash it with `--mesh-deck` (or pick "Attaky Mesh Deck" from the device prompt).
- Mesh Deck: the symbol key opens an on-screen tray of punctuation the 48-key matrix can't otherwise reach (`! ? : ; ' " - _ / @ # & + = ( ) [ ] { } < > * % $ | \ ~ ^ \``); press it again to dismiss. Shift also types the US QWERTY symbols off the number row.
- Mesh Deck: the front RGB LED double-blinks for every new channel message and DM, and keeps blinking every few seconds while anything stays unread — the only notification you get while the screen is asleep, and it works with the alert tone off.
- Mesh Deck: the D-pad drives every screen — up/down to scroll, left/right to change channel, centre to select. The BOOT button turns the screen off and on; stray touches and keypresses will not wake it back up.
- Mesh Deck: message history, exported config, the node archive and offline state maps are stored in internal flash, so everything that needed a microSD card on other boards works on this one despite having no card slot.
- Mesh Deck: battery percentage is read from the pack's fuel gauge rather than estimated from voltage, so it stays honest under load instead of dipping while transmitting.
- Mesh Deck: chat uses the full width of the 320x240 panel with the channel list as a drop-down, and the compose box opens nearly full height so a whole message is visible while typing.
- Mesh Deck: after flashing, power the board off and on — it is deliberately left halted rather than auto-reset, because a warm reset out of download mode hangs at the splash screen.
