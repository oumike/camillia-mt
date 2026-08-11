// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- **Mesh Beacons**: the device can now recognise Meshtastic 2.7 beacon advertisements - brief broadcasts from *other* meshes carrying an offer of a channel, preset and region. Off by default; the switch is on the Config screen and under Modules in web config, and it travels with config export and import. Receive-only - nothing is transmitted, and an offer is only ever shown, never applied to your radio.
- **Beacons screen** under Live -> Tools (press B, or the Beacons cell on Heltec): one card per sender with its message, what it offered, the SNR, RSSI and hop count it arrived on, and how often it has repeated. Beacons arrive minutes to hours apart, so the list is kept until you clear it with C (Heltec: the Clear button); switching Mesh Beacons off clears it too.
- Beacons are available on every board, Cardputer included, where they join the two charts in the Tools modal; Cardputer keeps four senders, the other boards eight.

Changed
- The Config screen action list now wraps - going up from the first row lands on the last, and down from the last returns to the first.)CAMNOTES";
