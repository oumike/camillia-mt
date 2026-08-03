// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- T-Deck: added a one-time serial boot message that identifies which touch controller your board actually has, to help diagnose touch issues.

Fixed
- T-Deck: touch no longer goes dead after the screen sleeps and wakes on panel revisions that never recovered from the deep-sleep command (the units at I2C address 0x5D); the display now leaves touch in a low-power idle that always wakes cleanly.)CAMNOTES";
