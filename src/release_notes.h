// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(Fixed
- Beacons from senders using Meshtastic's legacy split option (which sends the offer and its text as two separate packets) now show their message text in the Beacons view instead of appearing blank.
- A repeat beacon from one of those senders no longer wipes the text off an advert that was already showing it.)CAMNOTES";
