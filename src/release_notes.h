// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- Ten configurable LoRa channels on T-Deck, T-Lora Pager TFT, Heltec V4, Attaky Mesh Deck and ThinkNode M9; the Cardputer keeps eight, since its channel history has to live in internal RAM rather than PSRAM.
- Channels 8 and 9 are ordinary channels to the rest of the mesh - a Meshtastic header carries a channel hash, not a slot number, so ten-channel devices and stock eight-channel nodes talk to each other exactly as before.
- The web config pages and the VNC viewer page now carry a site icon, so the browser tab shows the Camillia mark instead of a blank page symbol.

Changed
- The channel list in the web config and on-device settings grows to match the board's channel count instead of stopping at eight.
- The Meshtastic phone app and `meshtastic --export-config` only understand eight slots, so a config exported through stock tooling will not include channels 8 and 9; Camillia's own export does.

Fixed
- Moving a device from a ten-channel build back to an eight-channel one (or to a Cardputer's saved config) no longer wipes every channel - the first eight keep their names and keys, and only the per-channel hop overrides fall back to the device default.)CAMNOTES";
