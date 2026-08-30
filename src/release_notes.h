// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- **Locate** is now a live OpenStreetMap view: it opens centered on the selected node at zoom 13 and can be panned and zoomed anywhere in the world, from zoom 2 down to building level at zoom 19 (T-Deck, T-Lora Pager, Square, Mesh Deck, ThinkNode M9).
- Drag the map to pan, and use the on-screen `+` / `-` buttons and the GPS button to zoom or recenter the node (T-Deck, T-Lora Pager, Square, Mesh Deck).
- Keyboard controls in Locate: `I`/`J`/`K`/`L`, arrows or trackball to pan, `M`/`N` or the page keys to zoom, `H` or `C` to recenter the node, and Space to return to the opening view (T-Deck, T-Lora Pager, Mesh Deck, ThinkNode M9).
- Map tiles you view while the device is on Wi-Fi are saved to storage, so the same area redraws later with no network; areas never visited show as blank gaps offline.
- Panning wraps across the antimeridian, so the view is no longer confined to one state, country, or downloaded area, and nodes outside the United States now get a real map.
- Web Config's **Maps Download** now pre-loads offline tiles around every positioned node, with a Roads / Streets / Buildings detail choice, a progress bar, a Stop button, and cached-versus-pending tile counts; stopping keeps everything already saved and a later run picks up where it left off.
- After upgrading, the device offers once to **Keep** or **Remove** the old state and detail map files, which the new map no longer uses; the answer is remembered.
- The Web Config chat box is now multi-line with a live character counter, and sends with Ctrl+Enter (Cmd+Enter on a Mac).

Changed
- The Locate map now fills the whole panel - the state name and coordinate line under it is gone, and text appears over the map only when there is something to say (`Loading map...`, `Waiting for Wi-Fi...`, `Map unavailable`).
- Space no longer closes Locate; it resets the view instead. Close with Enter, the close key, or by tapping outside.
- Opening **Nodes** now brings up Wi-Fi using your saved network so Locate can fill in missing tiles, and puts Wi-Fi back the way it was when you leave.
- Position, telemetry and neighbour announcements are paused while the Locate map is open, so panning and zooming stay smooth.
- **Clear Maps** now deletes the offline tile cache along with any leftover legacy map files, and reports how many of each it removed.
- Chat messages typed in Web Config now use the mesh's own length limit instead of a fixed 200 characters.
- Maps and other images redraw noticeably faster on boards with PSRAM (T-Deck, T-Lora Pager, Square, Mesh Deck, ThinkNode M9).
- On-screen buttons drop their keyboard-shortcut letters on the boards that have no keyboard - "Locate", "Delete", "Reply", "Yes", "No" (Heltec V4, Square).

Fixed
- A tile service refusal is no longer drawn on the map as if it were a map image.
- An interrupted tile download can no longer leave a half-written file that later looks like a valid cached tile.
- Deleting a folder on the card could leave files behind on some cards; every file is now removed.)CAMNOTES";
