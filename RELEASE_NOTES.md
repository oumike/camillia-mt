### New
- **Message Blink** setting in Config and the web config. It sets how long a new message blinks in chat and DMs: Off, or 5 to 60 seconds in 5-second steps. The default stays at 15 seconds, and changes apply right away with no reboot. Not on the T-Deck Pro, whose e-paper screen never blinks.
- Message Blink is included in config export and import as `chatBlinkSecs`.

### Changed
- CrowPanel Advance 5.0": firmware is now built for the V1.2/V1.3 boards Elecrow sells today, with the radio on GPIO8. V1.0 owners need a build made with `-DCROWPANEL_50_HW_V10=1` for the radio to work.
- CrowPanel Advance 5.0": the backlight now starts at full brightness. On V1.1 and later boards, any brightness below full makes the backlight whine.
- CrowPanel Advance 5.0": the buzzer is no longer driven on V1.1 and later boards, because the board's own controller owns it.
- CrowPanel Advance 5.0": the interface is resized for the 800x480 screen:
  - All text is larger.
  - The nav bar and screen headers are twice as tall.
  - Buttons and list rows are bigger.
  - The home and lock screen headers are larger.
  - The side nav column is wider.
  - The boot splash card is centred.
- CrowPanel Advance 5.0": Tools fills the screen in two columns, in either orientation.
- CrowPanel Advance 5.0": the on-screen keyboard has a fixed height at the bottom of the screen: 40% of it in portrait and half in landscape. It no longer covers almost the whole screen.
- CrowPanel Advance 5.0": Home shows all its widgets at once (the charts, then the node lists), with no carousel. The lock screen shows the same widgets under recent messages.

### Fixed
- Web config: Save All could silently do nothing. This happened when a wardrive log was on the device, or when the auto-favorite radius was shown in miles. If a field is rejected, its section now opens so the browser can show why.
- Web config: a stray `<` no longer appears at the bottom of the page when Wi-Fi networks are saved.
- CrowPanel Advance 5.0": the screen and backlight now work on V1.1 and later boards.
- CrowPanel Advance 5.0": touch now works, and the interface no longer stalls on every screen update.

Pull request: #111
