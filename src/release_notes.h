// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- Config screen has a new "Node Name" entry for editing your long and short name on the device; saving broadcasts the new name to the mesh right away instead of waiting for the next scheduled node info. Leaving the short name blank fills it in from the long name.

Changed
- T-Deck Pro: the e-paper screen now waits for a brief pause in your typing before repainting, so a line appears complete instead of the panel committing halfway through a word. If you keep typing without a break, it refreshes anyway about once a second.
- T-Deck Pro: the firmware update screen is now a single static black-on-white "Firmware updating..." notice with a reminder to keep the device powered, rather than a progress bar - a percentage that costs most of a second to redraw would slow the download itself.

Fixed
- T-Deck Pro and T-Lora Pager TFT: characters no longer go missing when you type quickly. Keystrokes are now collected and held while the display is busy, instead of being discarded by the keyboard controller.
- T-Deck Pro: firmware updates started from the menu no longer leave the screen frozen on the config page for the whole install with no indication anything is happening, and the interface redraws cleanly if you return to it.)CAMNOTES";
