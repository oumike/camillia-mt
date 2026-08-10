// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- Web Config can save a copy of your messages: a new Export Messages (CSV) button downloads every channel message and direct message the device still holds - one row each with the channel or peer, timestamp, sender and delivery state - and long wrapped messages are joined back into a single row. The file goes to your browser only; nothing is written to the device's SD card. Web Config only, not the AP-mode Lite page.

Fixed
- Web Config pages now load in about two seconds instead of crawling for tens of seconds or giving up before they finish.
- On the T-Deck, T-Lora Pager and Heltec, Web Config pages came out garbled once the device had heard from a lot of nodes, with node details and stray markup running into each other; a full 250-node list now renders correctly.
- Web Config pages could be cut off partway through and show as junk in the browser; a page that cannot be sent in full is now dropped so the browser reports a failed load instead of displaying a broken page.)CAMNOTES";
