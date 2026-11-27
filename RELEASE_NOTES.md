### New
- CardputerZero: Camillia can now run in a resizable window on the Raspberry Pi desktop (the Zero on a monitor). It appears under **Internet** in the menu and uses the large-screen layout; the mouse acts as touch, the wheel scrolls, and only one copy runs at a time.

### Changed
- CardputerZero: Wi-Fi, Bluetooth keyboard and GPS are now set up in the system's own settings. The WiFi, Choose WiFi, Bluetooth Keyboard, GPS, GPS poll and standby rows are gone from Config, and first-time setup no longer has a Wi-Fi step. A Bluetooth keyboard connected in the system settings types into Camillia. Position sharing and the privacy settings stay in Camillia.
- CardputerZero: a saved config that had Wi-Fi or GPS turned off is overridden to on.
- CardputerZero: holding Esc now quits to the launcher after 3 s (was 4 s), with a countdown ring that appears after 0.5 s.
- Contributors: `main_lvgl.cpp` and `web_config.cpp` are split into ordered fragments under `src/ui/*.inc` and `src/web_config/*.inc`, with no behaviour change. Open branches that edit either file must move their edits to the matching fragment when they rebase.

Pull request: #167
