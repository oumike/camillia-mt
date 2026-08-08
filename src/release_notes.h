// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- Location Precision: position broadcasts can be rounded to a grid before they leave the device - Precise, or anything from ~50 m to ~23 km. Set it on the device under Config, on the row below Share Location, or under Position in web config.
- Coarsened positions are tagged with how exact they are, so other Meshtastic clients show an area rather than a false pinpoint; the device keeps using your real fix locally for the compass, distances and the nodes list.
- Location Precision is saved with the rest of your settings and survives export and import; it defaults to Precise, so updating does not change what your node transmits until you pick a coarser setting.)CAMNOTES";
