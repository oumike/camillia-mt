// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- Seven modem presets from Meshtastic 2.7/2.8 are now selectable: Lite Fast, Lite Slow, Narrow Fast, Narrow Slow, Tiny Fast, Tiny Slow and Medium Turbo.
- The Tiny presets (15.6 kHz) are offered only on boards whose radio can produce them - they are absent on the Attaky Mesh Deck and the Elecrow ThinkNode M9, and a config importing one falls back to Long Fast.
- Two new regions, EU 866 (865.6-867.6 MHz) and EU Narrow 868 (869.4-869.65 MHz), in both the on-device Config menu and web config.
- A Release Channel setting (Stable or Alpha) under the update row, so a device can opt in to prerelease builds; Alpha images are signed and verified exactly like stable ones. Not present on the M5Stack Cardputer, which has no over-the-air update path.
- Node detail now shows a Signed row: whether a packet from that node has carried a Meshtastic 2.8 signature that verified against the key held for it, or whether no key is known yet. Nothing is rejected for being unsigned or for failing.
- Web config can derive this node's ID from its public key the way Meshtastic 2.8 does. Off by default and never changed by an upgrade; turning it on gives you a new address on the mesh, and your own chat history is carried across to it.

Changed
- First-time setup with region US now starts on Long Turbo rather than Long Fast, matching what a new Meshtastic 2.8 node comes up on. Existing devices keep their preset.
- Frequency slot numbers were being rounded down where a band is not a whole number of channels wide; they now round the way Meshtastic does. Your node may move to a different frequency on upgrade in **ANZ 433**, **UA 433**, **UA 868**, **PH 433**, **PH 868** and **KZ 433** - notably PH 868 on Long Fast moves from 868.125 MHz to 869.375 MHz. Upgrade every node in one of those regions together.
- Position sent on a channel anyone can decrypt (no key, or a stock default key) is capped at roughly 700 m however precision is set; the MQTT map report is capped the same way regardless of channel key. A channel with a real key still uses your chosen precision in full.
- A node that has not moved about 90 m since its last position now broadcasts at most every six hours instead of on the configured interval, because 2.8 receivers discard the repeat anyway. Moving restores the normal cadence immediately and Announce always transmits.
- Long name is capped at 24 bytes, matching what Meshtastic 2.8 stores and displays. A longer name saved by an older build is shortened on upgrade.
- Packets whose header claims more hops travelled than started are no longer relayed, matching 2.8. This also covers pre-2.3.0 firmware that never filled the field in; dropped frames are logged under `[fwd]`.
- The `square` build now identifies as Seeed Wio Tracker L2, and the M5Stack Cardputer now advertises as custom hardware rather than an ID that belongs to the Cardputer Adv.

Fixed
- Outgoing packets always carry the MQTT bitfield now. Without it, a node with OK-to-MQTT off and a hop limit of 0 had everything it originated silently discarded by every Meshtastic 2.8 node in range.
- Position requests, traceroutes, routing replies and store-and-forward messages now carry your OK-to-MQTT setting, like every other packet type already did.
- The packet log names the Store & Forward, Map Report, Mesh Beacon and LoRa OTA port numbers instead of showing bare numbers.)CAMNOTES";
