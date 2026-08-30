// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- Map settings page now has a "Map download debug" panel that records each tile attempt and its result, with a one-click "Copy report" button for sharing when downloads misbehave.
- Failed tile downloads now report why they failed - which stage broke, the upstream response, bytes received, and timings - instead of a bare error.

Changed
- Map tile downloads retry each tile up to three times and keep going past a failed tile, rather than stopping the whole run at the first error.
- If a tile download appears to fail but was actually written to the device, the page now re-checks storage and counts it as saved instead of reporting a failure.
- After five tiles in a row can't be confirmed, the download pauses and says so, keeping every tile already saved.
- Offline map status is checked in much smaller batches, so the check is far less likely to time out over a weak Wi-Fi link.
- The map download summary now reads in plain terms - tiles "stored on the device" and "remaining" instead of cache jargon - and confirms explicitly when everything at the chosen zoom is already stored.

Fixed
- The web config page no longer renders as garbled text or stray tag fragments when Wi-Fi is slow; page data is now written to the connection in a way that can't desynchronize the browser mid-page.
- Closing or navigating away from the config page while it is still loading now ends the transfer immediately instead of leaving the device building a page nobody is reading.)CAMNOTES";
