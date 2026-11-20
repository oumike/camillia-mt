### New
- When Camillia was installed through bmorcelli's Launcher, the device now recognises it. The Firmware Update action, the update checks and the install error say "Update with Launcher (installed by Launcher)" instead of asking for a factory-image reflash, which over USB would erase Launcher and every app it installed. Web config's Firmware Updates section explains how to update through Launcher. All ESP32 boards; only visible on Launcher installs.
- New "Installing with Launcher" and "Running under Launcher" guides cover these topics:
  - installing and updating through Launcher
  - why a USB flash removes Launcher
  - why settings don't carry over between a Launcher install and a USB install
  - what Launcher's web interface can see in the shared settings storage
  - how to back up the device first

### Changed
- On Launcher installs, Factory Reset now clears only Camillia's own settings. Launcher's settings, Wi-Fi, app list and the other installed apps' data are kept. All ESP32 boards. Nothing changes on a standard USB install, and no settings migration is needed.
- On a Launcher install with a BLE keyboard board, Factory Reset still clears the Bluetooth pairing store, which other installed apps share. Those apps may need to pair again. This affects the Heltec V4, Heltec R8, Seeed Wio Tracker L2, Elecrow CrowPanel Advance 3.5 and CrowPanel 5.0.

Pull request: #161
