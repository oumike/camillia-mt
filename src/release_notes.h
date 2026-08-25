// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- Heltec V4: every popup that stages a value before saving now has a matching Cancel/Save button row - Brightness, Battery Trim, Location Precision, Light Timeout, Notification Sound (whose commit button reads **Apply**), Channel Edit, and Time & Date - so a value can be committed or discarded on a touch-only board without an Enter or Escape key.
- Heltec V4: the channel list popup has a close button pinned to its top-right corner, so it can be dismissed without scrolling to the bottom of a long channel list.

Changed
- The message compose box now grows to fill the space the popup leaves instead of sitting at a fixed height with empty margin above and below it, so more of what's being typed is visible (T-Deck, T-Lora Pager TFT, Cardputer, Mesh Deck, M9).
- Notification Sound picker: tapping a row previews the tone and **Apply** commits it; tapping the already-selected row a second time still applies as before.
- Channel Edit and Time & Date no longer instruct you to "Save" in their hint text, since the buttons now say it.

Fixed
- A notification tone that was auditioned in the picker but never applied no longer gets written to the device: previously it stayed active and was silently persisted by the next unrelated settings change or on leaving the Config screen.)CAMNOTES";
