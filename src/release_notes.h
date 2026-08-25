// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- Terrain line of sight: pick a node in the Nodes list, press **S** (or tap **LOS**) and the device draws the ground profile between you and that contact, reporting **LINE OF SIGHT**, **MARGINAL (Fresnel)** or **NO LINE OF SIGHT**, with distance, bearing, and a marker on the tightest point. Available on every board except the Cardputer; the row is greyed until both ends have a position.
- Web Config gains an **Elevation Server (LOS)** field. Terrain analysis needs a plain-HTTP elevation service on your network - the firmware has no TLS client - and leaving the field empty simply disables the feature.
- A ready-to-run elevation proxy ships in `tools/elev-proxy/` with a Docker Compose file, a systemd unit, and setup instructions in `docs/LOS.md`, including caching and Nginx Proxy Manager notes.
- New `keys` serial command prints every raw and mapped key code as you press keys, for working out what a stray or dead key is actually sending.

Changed
- Elecrow ThinkNode M9: the d-pad now moves the text cursor while writing a message - Left/Right by a character, Up/Down by a line - so a typo several words back can be fixed without deleting everything after it. Outside the message box the d-pad still navigates as before.
- Elecrow ThinkNode M9: the dedicated Back button now discards the draft and closes the New Message box, instead of deleting one character at a time. The keyboard's own Backspace still deletes a character, and Back behaves as it always did everywhere else.)CAMNOTES";
