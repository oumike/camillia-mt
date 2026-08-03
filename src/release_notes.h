// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(Changed
- Composing and replying to messages now uses the full screen with a four-line input area, and long messages wrap instead of scrolling off the side (M5Stack Cardputer).
- The message color picker now shows the whole palette on two rows without scrolling, and the live-feed and compose legends were shortened to fit the narrow screen (M5Stack Cardputer).
- The empty direct-message panel now reads "Select a conversation" instead of showing an incorrect key hint.

Fixed
- Choosing a recipient in the direct-message node picker is no longer sluggish on a busy mesh, and the highlighted node stays on screen while you scroll through a long list.
- The live signal feed scrolled the wrong way under the arrow and j/k keys; it now scrolls in the expected direction (all boards).
- The message color picker no longer clipped off the top and bottom of the screen (M5Stack Cardputer).)CAMNOTES";
