// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- Battery Calibration in Settings: a slider that trims the measured battery voltage by up to 20% in 0.5% steps, with the reading updating live so it can be matched against a multimeter - the fix for a pack that reads high or low, or two units disagreeing at the same charge.
- The battery trim is saved per device and travels with a config backup/restore (drop it by hand when restoring onto a different unit).
- On Mesh Deck the trim moves the voltage readout only; the percentage there comes from the fuel gauge chip.
- New "Camillia Black" theme - true black background including the chat area, available on the device and in the web config. It is dark-only.
- "Reset Chat Colors" in Settings and in the web config: re-rolls which color each node gets, for when two nodes you talk to land on the same one. Press again for another arrangement; messages are untouched.
- Mesh Deck now has its own firmware asset, so over-the-air updates work on that board.
- The AP-mode web config page now offers Restore Config - upload a YAML backup while connected to the device's own access point. Export still requires the device to be on your WiFi.

Changed
- The auto-favorite range now defaults to 1 km on metric and 1 mile on imperial, so the field reads "1.00" instead of "0.62" or "1.61". Switching units re-rounds it only if you never typed a value of your own.
- Mesh Deck: the channel dropdown now matches the width of the channel button above it and uses the same text size, so it reads as that button opening rather than a separate panel.
- Mesh Deck: larger rows and text in the settings list.

Fixed
- Mesh Deck: dropped characters when typing at speed and quick taps on Enter doing nothing - the keyboard is now scanned five times faster and confirms a press in about 20 ms instead of 60-90 ms.
- Mesh Deck: two keys pressed in the same keyboard row at nearly the same moment no longer lose the second one.
- Boards without their own firmware asset no longer offer an over-the-air update that would install T-Deck firmware; they now report "OTA unsupported for this device build".
- Web config: a config file larger than 8 KB is now rejected outright instead of quietly applying only the settings that fit before the cutoff.
- Web config: changing units and the auto-favorite range in the same save no longer reinterprets the number you typed as the new unit.
- Web config: importing a config while on the device's access point now returns a small confirmation page instead of reloading the full config page, which could fail on the limited memory available there.)CAMNOTES";
