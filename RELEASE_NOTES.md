### Changed
- Notification and alert tones now compute their volume once per tone instead of once per audio sample, leaving more headroom during playback on T-Deck, T-Lora Pager TFT and Wio Tracker L2.

### Fixed
- With GPS debug logging on, setting a GPS duty-cycle period below the supported minimum no longer floods the serial log with the "staying always-on" warning hundreds of times a second — it is printed once, when the setting changes.
