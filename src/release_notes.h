// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- Added a middle-finger emoji to the emoji picker, available on every board.

Changed
- Chat and DM transcripts are now written in batches a moment after activity settles instead of once per line, so a busy channel no longer stutters the screen while messages arrive.
- Saves are held off while you are typing or scrolling and land in the gaps, keeping input responsive on boards where the display and SD card share a bus.
- T-Lora Pager: stepping through messages with the wheel now redraws only the messages around the cursor, so selecting and replying stays fast in long channels.
- Node list ordering is now recalculated only when something actually changes, speeding up screens that walk the whole node table.

Fixed
- T-Deck, Cardputer and T-Lora Pager: direct message conversations were never saved to storage and were lost on every reboot - they now persist and reload like channel history.
- Losing power partway through a save no longer destroys a channel transcript or DM conversation; the previous saved copy survives, and a save interrupted at the last moment is recovered on the next boot.
- Cardputer: opening web config no longer discards the last few seconds of chat.
- Recent messages now reach storage before a reboot, factory reset, onboarding, settings change or OTA update, instead of being lost inside the save delay.
- Clearing messages no longer leaves the old transcript to reappear after a restart.
- A deleted DM conversation no longer comes back after a reboot.)CAMNOTES";
