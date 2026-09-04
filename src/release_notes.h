// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- Low-battery protection: the device now warns as the pack runs down, then drops Wi-Fi, the web config and GPS and dims the screen to stretch what is left, and finally saves everything and switches off cleanly instead of brown-out looping until the cell is over-discharged.
- The device will not switch itself off while USB or a charger is connected, and after a low-battery shutdown the next boot says an empty battery was the reason.
- A battery that is already flat at power-on now stops the boot instead of restarting over and over, and three brownout resets in a row also stop it; plug in USB and it starts normally.
- On T-Lora Pager TFT and T-Deck Pro the low-battery shutdown genuinely disconnects the battery and recovers when USB is plugged in; other boards go into deep sleep and wake on the user button.
- Nodes screen can list nodes that have been dropped from the live table and archived to SD, shown dimmed and marked `~` below the live ones, searchable by name, with a details panel giving the archived date, when the node was last heard, the saved position and whether the public key was preserved (boards with an SD card slot).
- Archived nodes can be restored to the live list from the node action key, bringing back their name, position and public key; the confirmation warns that a full table archives its oldest non-favorite to make room.
- New Config rows "Archive Dropped Nodes" and "Show Archived Nodes" on boards with an SD slot, matched by a new "Show archived nodes on the Nodes screen" checkbox in the web config; both show "(no card)" when the slot is empty.
- Alt+H (home), Alt+D (direct messages), Alt+N (nodes), Alt+L (live) and Alt+C (config) now jump straight to a screen from anywhere on T-Deck, T-Deck Pro, Attaky Mesh Deck and T-Lora Pager TFT - previously only Alt+H - and all five are new on the M5Stack Cardputer.

Changed
- Switching the release channel now checks that channel for an update straight away: the Config row shows "checking..." and then the result, whether or not "Check for Updates on Boot" is on, and says why when a check cannot run.
- Changing the release channel from the web config runs that check after the reboot and offers any update it finds on the device's own screen.
- The Nodes header count includes archived nodes when they are shown, with a trailing "+" when the archive holds more than the screen lists.
- "Clear Nodes (All)" now also deletes the archived-node file from the SD card, both on the device and from the web config, and says so when it does.

Fixed
- An update found by the boot check is no longer lost when you open Config: the Firmware Update row keeps offering "Install <tag>" until it is installed or the channel changes.)CAMNOTES";
