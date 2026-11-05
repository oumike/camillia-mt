### New
- Early support for the Nicolai Electronics Tanmatsu. It has not been tested on a real unit yet. Flashing Camillia replaces the badge.team launcher; to go back, flash the launcher again from tanmatsu.cloud.
- Tanmatsu: LoRa runs through the built-in ESP32-C6, which keeps its factory firmware. Wi-Fi, microSD, sound alerts with volume control, battery level and charging state, and power-off from the firmware are all supported.
- Tanmatsu: keyboard shortcuts. F1 opens Home, F2 channels, F3 DMs, F4 nodes, F5 tools and F6 settings. The arrow keys scroll and switch channels, Fn+Up/Down pages, and Fn+Backspace goes back.
- Tanmatsu: the messages LED blinks while you have unread messages. The power LED shows charge state and the Wi-Fi LED shows connection state. A new "Status Lights" setting sets their brightness (5% to 100%, or Off).

### Changed
- Attaky Mesh Deck: the leftmost top button (L1) now opens the home dashboard. With the screen on, the Boot button opens chat, and pressing it again on chat opens the channel list. With the screen off or locked, Boot still wakes and unlocks the device.

### Fixed
- Attaky Mesh Deck: the battery calibration trim now changes the battery percentage. Before, the fuel gauge's reading was always used, even when it stopped short of 100% on a full battery.
