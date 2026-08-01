// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(Changed
- Library updates
- On the Cardputer, the emoji picker now offers a shorter list (faces, hands, and common reaction symbols; the celebrate, weather, and food emoji at the end are dropped) to fit its limited memory.

Fixed
- Messages containing emoji no longer crash the device while being displayed.
- Opening or scrolling the emoji picker no longer crashes when memory is tight; it now closes cleanly instead of freezing.
- Direct message conversations now scroll all the way to the newest message instead of stopping partway through a long one.
- Channel chat now stays pinned to the newest message instead of occasionally jumping away from the bottom.
- In direct messages, the j and k keys now scroll up and down consistently with the rest of the interface (j was scrolling the wrong direction).)CAMNOTES";
