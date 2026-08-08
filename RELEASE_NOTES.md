### New
- Per-channel location sharing: each channel can now be set to carry this node's position or not, from the Channel Actions popup (L), the Location row in the channel editor, or the Location checkbox in web config. The primary channel starts on and all others off, matching how earlier versions behaved; the global Share Location setting still overrides everything.
- Position announces now go out on every channel set to share location, not just the primary.
- Keyboard-backlight blink flash counts are now settable (1–3) and separate for channel messages and DMs, on the device under Config and in web config — T-Deck, T-Lora Pager, and Cardputer.
- Web config channel slots can be reordered with up/down arrows; channel 0 stays fixed as the primary.

### Changed
- The channel picker and channel editor now lay out in two columns, so all eight slots and the whole editor fit on screen without scrolling on every board. The picker rows show the slot name only, and the Key row opens the text editor where the full key is visible.
- In the channel editor, left/right toggles Location in place and hops between the two columns elsewhere.
- Web config channels are now one card per channel instead of a four-column grid, so the name, key, role and toggles line up properly.
- The web config Sound section is now called Notifications and holds the notification LED and keyboard blink settings alongside the audio ones.
- A manual position announce with no channel set to share location now says so in the live feed instead of appearing to do nothing.
- Sent messages show a single "ME (ACK)" tag; the separate "ME (SENT)" tag is gone.

### Fixed
- A channel name containing an apostrophe or quote no longer breaks the web config form and come back truncated on the next save.
- The Clear button in web config now unchecks a channel's Uplink/Downlink/Location boxes instead of leaving them ticked while submitting them as off.
- The Channel Actions popup no longer runs off the bottom of the screen on Cardputer.
