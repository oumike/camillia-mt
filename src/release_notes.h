// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- Square: firmware is now published as a downloadable release binary (`camillia-mt-square-vX.Y.Z.bin`) instead of being build-only.
- Square: can now pair an external Bluetooth Low Energy keyboard alongside the on-screen one, using the same default-off Config toggle, pairing dialog, and reconnect behavior as the Heltec V4 builds.
- Square: the built-in speaker now plays message notifications and the optional startup melody, with the Default/Chirpy/Bass/Off sound styles and notification-volume controls. The amplifier stays powered down between sounds to avoid stray clicks.

Changed
- Heltec V4 (both orientations) and Square: long-pressing a line in the Live feed now asks for confirmation before clearing the feed. Declining leaves the feed and your scroll position untouched; confirming erases all Live traffic, including lines hidden by the active filter.)CAMNOTES";
