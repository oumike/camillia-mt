// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(Fixed
- A firmware update that gets interrupted - power loss, a stall, or a reboot mid-download - no longer leaves the device stuck restarting into the minimal update screen; it now comes back up in normal firmware, still running the previous version, ready to retry.)CAMNOTES";
