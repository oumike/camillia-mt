### New
- New board: Elecrow CrowPanel Advance 5.0" with an SX1262 module (`crowpanel-50`). It has the same touch UI as the CrowPanel 3.5", with the on-screen keyboard, chat fonts, browser VNC Host/Remote and BLE keyboard support. Landscape is the default, and you can pick either portrait orientation on the device. This build has not been tested on hardware yet.
- CrowPanel Advance 5.0": set the function DIP switch on the back of the board to the wireless module, or the radio won't be detected. The boot log names the switch if the SX1262 isn't found.
- CrowPanel Advance 5.0": no SD card support. The DIP switch disconnects the SD slot when it selects the radio, so files are kept in internal flash.
- CrowPanel Advance 5.0": the buzzer works. There is no GPS and no battery reading, and the backlight can only be on or off, so it doesn't dim.
- CrowPanel Advance 5.0": this build is for the V1.0 board. On later revisions that control the backlight differently, the backlight and touch reset may not work.

### Changed
- Chat server: your device now sends the server the list of channels it's on. The server then announces only the channels you share with it, even when it hosts more than your device can track.

Pull request: #109
