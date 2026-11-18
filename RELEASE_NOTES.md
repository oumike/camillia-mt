### New
- **M5Stack CardputerZero + Cap LoRa-1262 support.** Camillia runs as an app in the CardputerZero's launcher rather than as a flashed firmware image. It has the same screens as the other boards: chat, DMs, nodes, channels, Live, Tools, Config and the Home dashboard. Install it from the M5Stack App Store (share code `CMLA`) or install the `.deb` attached to this release with `sudo apt install ./camillia-cardputer-zero-<version>_arm64.deb`. Close the stock Cap LoRa-1262 and meshterm apps first, because only one app can use the radio at a time.
- CardputerZero: the radio runs only while the app is open.
- CardputerZero: Camillia uses the system's own Wi-Fi connection and shows its status. There is no Wi-Fi picker and no Wi-Fi step during setup.
- CardputerZero: Bluetooth keyboards pair through the system's Bluetooth, from the usual Config row.
- CardputerZero: web config runs on port 18765 and has the same pages as on the other boards. Chat and Bluetooth keep working while it is open.
- CardputerZero: GPS from the cap's L76K module and battery level from the system fuel gauge.
- CardputerZero: updates come from the M5Stack App Store, so the in-app update check points there.
- CardputerZero: press Esc twice to go Home, or hold Esc for 4 seconds to quit to the launcher. Outside text entry, the number row works as shortcut keys: 1 Home, 2 Chat, 3 DMs, 4 Nodes, 5 Tools, 6 Config, 0 Help.
- CardputerZero: larger text and taller rows in Config lists and channel grids, and a taller screen header with bigger text on every screen except Home.

### Changed
- Export Config: "Append date" is now a checkbox instead of a slider (all boards).
- Developers with their own build environments that list `config_io.cpp` must also add the new `src/mesh_channel_plan.cpp`.
- Three new on-screen text items are in English only for now and have not been translated yet.

### Fixed
- Cardputer ADV: up, down, left and right now move as expected in the Config grid and in grid popups. Before, Up could move right.

Pull request: #156
