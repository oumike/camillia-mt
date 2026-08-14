// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- Store and Forward client: the device can now display messages replayed by a Store & Forward router, shown with an `[SF]` prefix in the channel or DM thread they originally belonged to. Off by default - turn on **Store&Fwd Client** on the Config screen (Modules -> *Receive Replayed Messages* in web config).
- **Request S&F Replay** on the Config screen, and **Request Replay Now** in web config under Utilities -> Diagnostics, ask the router for the last four hours of stored messages. Routers never replay on their own, so this is what actually fetches history; the row names the router it will ask, or reads `no router` until one has been heard.
- **Router Node ID** (web config -> Modules -> Store & Forward) pins which router to ask, as `!aabbccdd`. Worth setting when your router has heartbeats switched off - the Meshtastic default - which otherwise leaves it undiscoverable. Travels with config export/import.
- Node Actions can now be opened straight from a chat message: press Enter to move the cursor into the messages, then Enter again on a message to open the sender's action menu (Traceroute, Send DM, Favorite, Request Info, Request Position, Ignore) with the same T/D/F/I/P/G shortcuts. Works on T-Deck, T-Lora Pager, Cardputer and Mesh Deck.
- Tap and hold a chat message to open Node Actions for its sender on the touch builds - T-Deck, Mesh Deck and Heltec V4. On Heltec this is the only route from chat, since Enter keeps its new-message binding there.
- **Clear Nodes (Keep Favorites)** on the Config screen and in web config's Danger Zone drops every non-favorited node while keeping favorites with their names, keys and positions, and reports what it did (e.g. `Cleared 214 nodes, kept 6 favorites`).

Changed
- The old Clear Nodes action is now **Clear Nodes (All)** on both the device and web config, and its confirmation spells out that favorites go too.
- The Node Actions menu title now names the node it is acting on rather than just reading "Node Actions" - long name where there is room, short name on the Cardputer.
- Clearing all nodes from web config now also removes leftover node-database files on the SD card, matching what the on-device action has always done.

Fixed
- Full-length incoming messages are no longer dropped in silence: the receive buffer now matches Meshtastic's maximum payload size, and a payload that still does not fit is reported on the serial log instead of vanishing.
- Replayed messages respect your ignore list, and a replay no longer disturbs the node list - a sender's Last Heard and signal readings still describe when that node was really heard, not when a router repeated it.)CAMNOTES";
