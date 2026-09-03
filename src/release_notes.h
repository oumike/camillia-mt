// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(Changed
- The board previously known only by its codename "Square" is now identified as the Seeed Wio Tracker L2 throughout the firmware, docs, and downloads.
- Wio Tracker L2 release downloads are now named `camillia-mt-wio-tracker-l2-vX.Y.Z.bin` instead of `camillia-mt-square-vX.Y.Z.bin`.
- Wio Tracker L2: over-the-air updates now look for the renamed firmware file, so a device still running an older "square" build needs one manual flash of v4.7.11 before OTA updates resume.
- Building from source for the Wio Tracker L2 now uses `--wio-tracker-l2` (environment `wio-tracker-l2`) in place of the old `--square` flag, and the device picker lists it as "Seeed Wio Tracker L2".)CAMNOTES";
