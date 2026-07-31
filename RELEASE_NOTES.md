### New
- Time and Date screen in the device config lets you choose where the clock comes from — automatic (internet or GPS) or a time you set by hand — and enter the date and time directly on the device.
- The clock now sets itself from a GPS fix when there's no internet connection (boards with GPS, e.g. T-Lora Pager and T-Deck).
- Release Notes viewer in the config menu shows what changed in the build you're running; it works offline on every board, including boards without over-the-air updates like the Cardputer.
- Web config adds a Time and Date section for picking the time source and setting the clock, with a "Use this browser's time" button that fills in the current date and time for you.

### Changed
- In automatic mode the device keeps retrying an internet time sync after startup instead of trying only once, so a device that had no network when it booted catches up on its own.
- Web config now gathers notification volume, notification sound, and splash melody together under one "Sound" section instead of splitting them across the Display and Modules areas.
- Web config's login/access settings moved to the bottom of the page, after the settings you actually came to change.
