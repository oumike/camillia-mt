### New
- Weather and terrain line-of-sight are now available on the Cardputer, which previously had neither; both server addresses are set through the microSD `config.yaml` (`network: losElevServer:` / `weatherServer:`), and weather ships with a working default so it needs no setup at all.
- The sleep clock screen now shows current conditions and temperature beside the node name and time, using the last reading the Weather screen fetched; it stays blank rather than showing a stale reading after the device has been asleep for hours.
- Web config has an Idle Timeout setting (5 minutes through 1 hour, or Never) to control how long it stays up with no activity before closing itself and releasing WiFi — previously the 10-minute default could only be changed by hand-editing a config file.
- Config export now carries the line-of-sight elevation server and weather server addresses.

### Changed
- The sleep clock screen has a divider rule under the wordmark, separating the device name from the status below it.
- Cardputer: the weather screen uses a compact layout for its 240x135 panel — temperature and conditions share one line, and the details fold into three lines instead of four.
- Cardputer: the line-of-sight cross-section is drawn shorter so the verdict and numbers below it stay on screen.

### Fixed
- Cardputer: the `,` and `/` arrow keys now move left and right without holding Fn, matching the `;` and `.` keys — sliders and anything else using left/right can be driven from the bare arrow cluster.
- Restoring a config backup no longer comes back with weather and terrain line-of-sight switched off, since the backup now includes their server addresses.
- An out-of-range web config idle timeout imported from a config file is clamped to a usable value instead of closing the page before it can be read.
