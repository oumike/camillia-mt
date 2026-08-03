// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(Fixed
- T-Deck: touch no longer intermittently stops working after the screen wakes from sleep; the touch controller is now confirmed awake and retried if it misses the wake pulse.)CAMNOTES";
