// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- Heltec V4 (`heltec-v4`, `heltec-v4-vertical`): an external Bluetooth LE keyboard can now be paired and used to type anywhere in the firmware - messages, config fields, menus. Two new Config rows: **BT Keyboard** turns the radio on or off (off by default), and **Pair BT Keyboard** opens a scan-and-pair dialog.
- Pairing dialog is driven by on-screen buttons (Close / Scan / Pair / Forget) so it works on a board with no keys of its own, and starts scanning the moment it opens; once a keyboard is paired, arrows, Enter, `N` and `F` drive it too.
- Keyboards that ask for a code show a six-digit passkey on screen to type on the keyboard; the bond is stored on the device and survives reboots, so pairing is a once-per-keyboard job.
- The **BT Keyboard** row reads the live link back - `Off`, `Connecting to ...`, `Connected: <name> (78%)` with the keyboard's battery level, or `Waiting for ...` - and a remembered keyboard reconnects on its own when it comes back in range or wakes up.
- Typing support covers letters, digits, punctuation, Space with Shift and Caps Lock, Enter, Backspace, Delete, Escape, Tab, Page Up/Down and the four arrows (mapped to the usual scroll and channel navigation). Holding a key repeats it. Volume, media keys and trackpads on combo devices do not pass through.
- Traceroute results now show the measured signal quality of each radio hop, e.g. `NodeA -> NodeB (7.5dB)`, instead of just `radio`. Hops the reply reported no measurement for still read `radio`, and MQTT hops still read `mqtt`.

Changed
- Bluetooth keyboard and Web Config cannot run at the same time on Heltec V4 - the two share one radio. Switching either on switches the other off and explains why on screen; turning one back off does not automatically restore the other. LoRa is unaffected by both.
- Light-sleep power saving is suspended while a Bluetooth keyboard is connected, because a nap drops the link.
- Starting a firmware update on Heltec V4 shuts the Bluetooth keyboard down for the duration to free memory, and brings it back if the update does not install.
- Web Config wins at boot: if a device somehow comes up with both enabled, the keyboard stays off so a keyboard-less device can still be reconfigured.

Fixed
- Traceroutes aimed at a Camillia node now report the signal quality that node heard the request at. Previously the reply echoed the request unchanged, so a direct traceroute between two Camillia nodes had no signal readings at all.)CAMNOTES";
