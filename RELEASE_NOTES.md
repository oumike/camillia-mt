### New
- Wio Tracker L2: new lock screen — hold Wake, or let the screen timeout run out, and the panel stays lit showing the clock, node name, date, battery and the newest unread messages; a press of Wake goes back to the UI. On by default, with the panel going fully dark after 5 minutes.
- Wio Tracker L2: "Lock Screen" and "Lock Screen Off" settings, on-device under Config below Screen Timeout and in the web config page. The dwell runs 5–60 minutes in five-minute steps, or "Stay on" to keep the lock screen lit until Wake is pressed.
- T-Deck Pro: the sleeping screen now lists the actual unread messages — time, channel (or DM), sender and message text, newest first, up to eight lines — instead of a single unread count.
- Discovery preset scan: pick another LoRa preset and the node parks on it for five minutes to find meshes there, then retunes itself back. Reached with the new Preset button or the P key; the picker warns that the node hears nothing on its own mesh while a scan runs, and that it only finds meshes sharing your primary channel key. Leaving the Discovery screen or saving config brings the radio home early.
- Attaky Mesh Deck: with the Keyboard module attached, its left and right RGB indicators now blink for unread messages alongside the Core RGB LED, in the same color and pattern.
- Heltec V4 and Attaky Mesh Deck: Discovery snapshots can now be saved on these boards, to internal flash — previously the Save option existed only on boards with an SD card slot.
- Heltec V4 and Wio Tracker L2: Discovery gained a full-width Sweep / Preset / Clear / Save button row and a close X. The Wio Tracker L2 had an SD card but no way to reach Save without a keyboard.

### Changed
- Tools is now a top-level destination on every board with a keyboard or nav bar. The L key, Alt+L and the nav bar's wrench button open Tools, and the Live packet feed is its first row alongside the SNR/RSSI, Channel Utilization and Discovery entries.
- The Live feed screen no longer has its own Tools button or T shortcut; C to clear and F to filter are unchanged.
- T-Deck Pro: the function button below Home now opens Nodes, and the GPS-area button below Back now opens Tools.
- T-Lora Pager TFT: the third trackball-row key now opens Tools rather than the Live feed.
- T-Deck Pro: the sleeping screen puts the date in the top-left and the battery in the top-right as a status band, with the Camillia title, node name and clock grouped under it.
- Discovery now waits 15 seconds after a sweep finishes before another can be started, on top of the existing one-per-minute limit, and the countdown message names whichever wait is actually holding things up.

### Fixed
- T-Deck Pro: the DM button in the nav bar drew as a solid filled block on the e-paper panel. It is now a speech balloon that renders correctly.
- Pressing the BOOT/user button while the screen is off now only wakes the screen, instead of also activating whatever control was underneath on the hidden UI.
