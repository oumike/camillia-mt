### New
- Screen orientation is now a picker listing Landscape, Portrait and Portrait 180 with the one in force marked, so any rotation is one reboot away instead of two (Heltec V4, Wio Tracker L2).
- Portrait can now be set a half turn round, for holding the device the other way up or keeping the cable clear (Heltec V4, Wio Tracker L2).
- The Wio Tracker L2 can now be switched between landscape and portrait from Config → Orientation or Web Config, with no separate firmware to flash.
- Web Config's Orientation dropdown now offers Portrait (180°) alongside Landscape and Portrait.
- Discovery has a new VIA MQTT group for nodes the broker told you about, kept apart from what the radio itself heard.
- The preset picker has a close X in the corner, so you can back out without picking a preset that reboots the device (Heltec V4, Wio Tracker L2).

### Changed
- Choosing a theme, a message colour or a radio preset now asks before rebooting, and answering No leaves you on the list.
- Turning the MQTT bridge on or off now asks before rebooting.
- Saving settings from Web Config now asks for confirmation first, as importing a settings file already did on the device.
- Importing a settings file from Web Config now warns in the browser that it replaces your settings and reboots.
- After a preset scan, Discovery keeps showing that scan's result, and the summary line names the preset scanned and how many nodes it heard there.

### Fixed
- A preset scan no longer counts or lists nodes that arrived over the MQTT bridge while the radio was parked on the scanned preset, so the "found" number reflects nodes actually on that preset.
- Discovery no longer files a node last heard over MQTT under DIRECT or a hop count, which made an internet-bridged node look like a radio neighbour.
