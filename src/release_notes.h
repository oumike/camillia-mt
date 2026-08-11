// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(Fixed
- Restoring a config during first-boot setup no longer loses the node's identity - the saved keypair is now written to storage, so the device keeps its old public key and peers can still reach it with encrypted direct messages. Imports from the Config screen and web config were already correct and remain so.
- A freshly flashed device no longer announces itself to the mesh before setup is finished - NodeInfo, position and telemetry are held back instead of going out with the default "Camillia"/"CaMi" name and the firmware's fallback coordinates, and the device stays quiet to other nodes' NodeInfo requests until onboarding completes or a config is imported. Receiving and relaying still work throughout, and pressing Announce in web config sends immediately.)CAMNOTES";
