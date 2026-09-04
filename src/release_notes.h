// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(Fixed
- Your own past messages now stay marked as yours after changing your node ID from the web config page: the ID change survives the reboot that follows saving, so older chat history is re-attributed on the next boot instead of appearing to come from another node.
- Chat history in quiet channels is also re-attributed after a node ID change, rather than keeping the old ID until that channel next receives a message.)CAMNOTES";
