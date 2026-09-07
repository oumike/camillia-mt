### New
- Lock screen is now available on every backlit board — T-Deck, T-Lora Pager TFT, Heltec V4, Attaky Mesh Deck, ThinkNode M9 and Wio Tracker L2 — instead of the Wio Tracker L2 alone. The screen timeout and each board's usual screen-off gesture show the time, date, battery and newest unread messages before the panel goes fully dark.
- **Lock Screen** and **Lock Screen Off** (5–60 minutes or **Stay on**, default 5 minutes) appear in on-device Config and web config on all boards that support the feature.

### Changed
- The lock screen sizes itself to the panel it is on: five message previews on the T-Lora Pager TFT, eight on portrait screens, six on 320x240 landscape screens.
- Any input is swallowed while the lock screen is up — only that board's normal wake gesture dismisses it, so a stray key or touch can no longer trigger something in the UI hidden underneath.
- T-Lora Pager TFT turns its keyboard backlight off while the lock screen is showing and back on when it is dismissed.
- M5Stack Cardputer keeps going straight to screen sleep and does not show the new settings; T-Deck Pro keeps its existing e-paper sleep screen, which stays visible without a dwell timer.

### Fixed
- The button press or hold that brings up the lock screen no longer immediately dismisses it again.
