### New
- Clock Format setting: choose 12-hour (`2:32 PM`) or 24-hour (`14:32`) for every time the device shows you — chat header, message timestamps, live feed, sleep and lock screens, Device Info and archived-node details. Found on the Config screen under Time and Date, and in web config; applies instantly with no reboot, and travels with config export/import.
- TRACKER is now offered as a device role, on the device Config screen, in web config and during onboarding. It relays like a CLIENT and advertises itself as a position reporter; it does not change power use, so pair it with Share Location on and a shorter GPS Broadcast Interval. Device Info shows `TRACKER (not sharing location)` if Share Location is off.
- Automatic Updates in Web Config → Firmware Updates: set the device to check every 1, 6, 12 or 24 hours and install a newer release with no prompt and no keypress, then reboot into it. Off by default on fresh flashes and on upgrades. It follows your Release Channel, waits while the battery is low or WiFi is down, will not reboot over an open dialog, and gives up on a release that fails to install three times (reporting why on the next boot). While it is on it supersedes Check for Updates on Boot. Not available on the Cardputer, where OTA is disabled.

### Changed
- The firmware install screen now names the release it is installing (e.g. "Installing update v4.9.2"), on both the colour and T-Deck Pro screens, so you can see which version the device is about to become.
- A rhino runs back and forth under the install progress, and slows to a stop when the download stalls.
- The clock on the sleep screen is now orange instead of blue on colour displays, so it reads at a glance; e-paper screens are unchanged.

### Fixed
- The front D-pad on the Attaky Mesh Deck did nothing while a message was open. Up/down now scroll and left/right move within the compose box.
- The install progress screen no longer blinks the whole panel several times a second, and it no longer repaints furiously during a stalled download — the download itself gets that time back.
- Device Info showed role 2 as "CLIENT_HIDDEN_MQTT", which is not a Meshtastic role. It now reads ROUTER, the role that value actually means.
