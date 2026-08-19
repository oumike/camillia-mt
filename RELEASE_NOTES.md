### New
- Ten configurable LoRa channels on T-Deck, T-Lora Pager TFT, Heltec V4, Attaky Mesh Deck and ThinkNode M9. The Cardputer stays at eight — it has no PSRAM, so each channel's message history has to come out of the memory the device needs to boot.
- Channels 8 and 9 are ordinary channels to everyone else on the mesh: a Meshtastic packet carries a channel hash rather than a slot number, so ten-channel devices and stock eight-channel nodes talk to each other exactly as before, as long as they share the key.
- The web config pages and the VNC viewer page now carry a site icon, so the browser tab shows the Camillia mark instead of a blank page symbol.

### Changed
- The channel list in Web Config and in the on-device Channels screen grows to match the board's channel count instead of stopping at eight.
- The Meshtastic phone app and `meshtastic --export-config` only understand eight slots, so a config exported through stock tooling will not include channels 8 and 9; Camillia's own export does.

### Fixed
- Moving a device from a ten-channel build back to an eight-channel one (or restoring a Cardputer's saved config) no longer wipes every channel — the first eight keep their names and keys, and only the per-channel hop overrides fall back to the device default.
