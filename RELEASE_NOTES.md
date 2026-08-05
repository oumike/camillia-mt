### New
- `backup.sh` — a one-command full backup of every connected board: the entire flash, the settings and the node's identity key, plus a restore path that puts a device back exactly as it was, node ID and all. Run `./backup.sh` to sweep every attached board, `./backup.sh --list` to see what you've saved, `./backup.sh --restore <dir> --port <PORT>` to put one back. It reads the partition table off the device, so backups are correct on Launcher-installed units too, and it falls back to slower serial speeds automatically on boards that need them.
- Invert Scrolling (T-Deck) — flips the trackball's scroll direction everywhere it's used: chat, lists, menus and sliders. Available in Settings and on the web config page. The j/k keys are unaffected.
- Notification LED on/off (Mesh Deck) — turn the blinking unread-message light off if you don't want it. Available in Settings and on the web config page, on by default.

### Changed
- The Mesh Deck's unread-message LED now repeats about once a second instead of every four, so it reads as an active notification rather than an occasional flicker.
- Alert sound, startup melody and volume settings are no longer shown on the Mesh Deck, which has no speaker or buzzer — those rows and web controls did nothing on that board. The web config's sound section is now titled Notifications there and holds the LED setting.

### Fixed
- Saving from the web config page no longer switches off the startup melody or resets the message alert sound on boards where those controls aren't shown.
- Turning the notification LED off mid-blink now darkens it immediately instead of leaving it stuck lit, including when the change comes from the web page or a YAML import.
