### New
- Share Location: a single on/off switch, directly under GPS in Settings and on the web config page, that decides whether the device puts its coordinates on the mesh at all — off silences the scheduled position broadcast and the manual announce alike, whether the fix would have come from live GPS, the last known position, or fixed coordinates; on by default, so existing devices keep behaving as before.
- Keyboard backlight blinks on new messages while the screen is off (T-Deck and T-Lora Pager TFT) — one blink for a channel message, two for a DM, repeating about once a second until the message is read; these boards have no notification LED, so it is the only cue for a message arriving with the screen asleep. Toggle it under "Keyboard Blink" in Settings or on the web config page.
- Saved WiFi networks can be deleted from the Choose WiFi list: highlight an entry, press D, confirm with Y. Deleting the network you are on is called out before you confirm, and the device then switches to the next usable network, or falls back to AP mode when none is left.

### Fixed
- The letters "j" and "k" can now be typed into the WiFi password field and the channel name/PSK fields — they were being read as scroll commands and dropped.
- The "config stopped (idle)" notice from the web config server no longer appears as a message in the first channel's chat history; it goes to the live feed with the other status lines.
- Pressing announce with location sharing off now says so in the live feed instead of reporting a missing GPS fix.
