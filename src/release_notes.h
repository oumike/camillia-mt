// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- T-Deck Pro: the sleep clock now shows the battery level under the date and unread lines, in percent or volts according to the Battery Display setting, updating with the clock so it costs no extra e-paper refresh.
- The web interface Danger Zone now has a Reboot Device button - it restarts the firmware with a confirmation prompt and keeps all messages, nodes and settings.
- The notification light timeout can now be set to 5 seconds, below the previous shortest setting.

Changed
- The board previously known only by its codename "Square" is now identified as the Seeed Wio Tracker L2 throughout the firmware, docs, and downloads.
- Wio Tracker L2 release downloads are now named `camillia-mt-wio-tracker-l2-vX.Y.Z.bin` instead of `camillia-mt-square-vX.Y.Z.bin`.
- Wio Tracker L2: over-the-air updates now look for the renamed firmware file, so a device still running an older "square" build needs one manual flash of this release before OTA updates resume.
- Building from source for the Wio Tracker L2 now uses `--wio-tracker-l2` (environment `wio-tracker-l2`) in place of the old `--square` flag, and the device picker lists it as "Seeed Wio Tracker L2".
- The light timeout setting is now labelled "Notification Light Timeout" on the device and in the web interface, so it reads clearly as the notification LED rather than the screen.

Fixed
- T-Deck, T-Deck Pro, Cardputer and ThinkNode M9: a microSD card that would not start up at the usual clock speed is now retried down to 400 kHz, so cards that previously looked like an empty slot are found and mounted.
- Devices running without a microSD card inserted no longer stutter - a failed card check now backs off instead of re-running the full mount every time storage is touched, which had been stalling the device for about two seconds out of every ten for the whole session.
- The one-time check for old-style map files now gives up after three attempts on a device with no card, instead of re-probing storage every ten seconds forever.
- Exporting or importing config from the menu always rescans for a card first, so a card inserted moments earlier is still detected.
- Beacons from senders using Meshtastic's legacy split option (which sends the offer and its text as two separate packets) now show their message text in the Beacons view instead of appearing blank.
- A repeat beacon from one of those senders no longer wipes the text off an advert that was already showing it.)CAMNOTES";
