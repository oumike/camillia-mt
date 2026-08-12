// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(Fixed
- Mesh Deck: quick keystrokes no longer go missing - a key press now registers the instant it is seen instead of having to be held across two scans, so fast typing and short taps on Enter land reliably.
- Mesh Deck: when a single scan catches two keys at once, both are delivered right away rather than one being held back until the next pass.
- Opening the channel list no longer bogs the interface down; the channel buttons are only redrawn when something about them actually changes, keeping key presses responsive while the list is on screen.)CAMNOTES";
