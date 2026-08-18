// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New

- **MQTT monitor can now put its top channels on the mesh.** Press **S** in the MQTT monitor (on Heltec, the new **Send** button in the header) to broadcast the five busiest channels and their message counts as an ordinary text message, e.g. `MQTT msh/US/MI 12m: KAM-NET 42, CFW 18, LongFast 7`.
- Sending walks through a channel picker listing every configured channel slot - move with the d-pad and press Enter, or tap on Heltec - followed by a confirmation screen showing the exact text that will be transmitted and the channel it is going to.
- The summary is trimmed to whole entries so it never runs past Meshtastic's 200-byte text limit, and sends are limited to one per minute; the status line reports the result (`Sent to LongFast`, `Wait 43s before sending again`, `Nothing recorded yet - nothing to send`).

Changed

- **ThinkNode M9: holding the d-pad centre now puts the screen to sleep**, matching the T-Deck's held trackball click. A tap of the centre is still Enter, and the hold length is set by the keyboard controller itself, so it may not be exactly two seconds.
- **ThinkNode M9: the Home button only opens Home now** - holding it no longer sleeps the screen.
- **ThinkNode M9: a held Enter no longer closes a modal.** Back is the close key, and holding Back is the long-press close; past the hold threshold Enter is the sleep gesture.

Fixed

- **ThinkNode M9: no more messages sent from your pocket.** Only the d-pad centre (or Enter) wakes the screen - every other key is ignored while the screen is off - and the press that wakes the device is consumed, so it no longer also opens the surface it belongs to. Previously the first pocket press woke the screen and everything after it landed as real input.)CAMNOTES";
