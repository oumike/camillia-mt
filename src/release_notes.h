// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- Deleting a direct message conversation now raises a **Delete conversation?** dialog that names the peer and warns the message history goes with it - Y/Enter confirms, N/close cancels, and it ignores every other key while it is up.
- Sent messages in the Classic message style now show an `[ACK]` marker right after the timestamp in channel chat and Direct Messages, on every board: green for a confirmed routing ACK, accent-colored when a relay carried a broadcast onward. Failed sends still turn red.
- On the Nodes screen, Enter now commits a filter: the list stays narrowed while the keyboard returns to the rows, so Up/Down move the cursor and A opens Actions instead of typing a letter. Backspace or Space goes back to editing the filter text without discarding it.

Changed
- **D** is now the delete key for the selected DM conversation on every keyboard build, replacing Backspace on T-Deck and T-Lora Pager, Fn+Backspace on Cardputer, and Sym+Backspace elsewhere. On the Heltec touch build, long-press a conversation row for 3 seconds to raise the same dialog.
- In the DM list, Backspace now only steps focus out of the message panel and keeps your place in the list; it no longer arms a delete.
- The live feed now draws at the size chosen in Font Size, matching chat and DMs, instead of a fixed size of its own.

Fixed
- Emoji in live feed lines drew as empty boxes.
- Changing Font Size - from the device menu or from web config - did not update the live feed until something else redrew it.)CAMNOTES";
