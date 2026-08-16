// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- **Message Actions** - a single menu on a chat message with six quick reactions (     ), `...` for the full emoji tray, Reply, and the sender's node actions. Press Enter twice on a message, or tap and hold it on T-Deck, Mesh Deck and Heltec V4. The Cardputer keeps the plain Node Actions menu; reactions there are still on the **E** tray.
- **Battery Display** setting (Config screen, and under Display in web config) switches the chat header and the web `BAT` chip between percentage (`87%`) and voltage (`3.94V`). The colored dot still tracks charge in both modes, and transmitted telemetry is unchanged.
- **Remembered WiFi networks** - the device now keeps up to five networks besides the one it is on, so switching back does not mean re-entering a password. They survive reboots, appear in the on-device Choose WiFi picker (**D** to forget one), and travel with config export/import under `wifi_networks:`. The Cardputer has no room for the list and still holds one network at a time.
- **WiFi tab in web config**, between Config and Utilities: see the remembered networks, **Use** one (the network you are on moves into the list), **Forget** one, or add a network by name and password without switching to it. Not on the AP-mode Lite page.
- **Boot progress on the splash screen** - the splash now stays up through WiFi, storage, radio and GPS bring-up and names the step it is on, instead of blacking out for several seconds. The version moved up under the subtitle. T-Deck, Mesh Deck and Heltec V4.

Changed
- Whichever network the device actually connects to becomes the configured one, so a reboot comes back to where you left off. A network that fails to connect never displaces one that worked.
- Turning web config off no longer drops the WiFi connection when the device wanted WiFi anyway - no reassociation, and no MQTT/NTP gap after closing the page.
- An exported config now carries every remembered network password, not just the active one. Treat the file as a secret, as with channel keys.

Fixed
- A network joined from the on-device WiFi picker is no longer forgotten on the next boot - web config's stale copy of the credentials was overwriting it in storage.
- Turning on web config while joined to a network picked on the device no longer dials the old stored network, time out and fall back to AP mode, taking the device off the network it was reachable on.
- The last remembered network is no longer missing from the Choose WiFi picker when the list is full.
- WiFi scanning is more reliable from a cold radio or just after AP mode: the scan retries once, and a scan that failed now says "Scan failed - Rescan to try again" instead of "No networks found".
- Deleting the network in use no longer claims it will switch to the network being deleted.
- WiFi names and passwords containing an apostrophe no longer break the web config form and come back truncated on save.
- The chat header battery reading now refreshes as soon as either the voltage or the percentage changes, instead of the voltage sitting still on the flat part of the discharge curve.)CAMNOTES";
