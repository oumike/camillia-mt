### New
- Elecrow ThinkNode M9 is now a supported board: LoRa mesh over its LR1110 radio, GPS, 320x240 display, 37-key QWERTY plus d-pad, microSD config import/export, and the full UI (channels, DMs, Nodes, Live, Map/Discovery, web config).
- ThinkNode M9: the six hardware buttons under the screen jump straight to Messages, Home, Live, Nodes and Map from anywhere, closing whatever is open first; holding Home for a second sleeps the screen.
- ThinkNode M9: d-pad Up/Down scrolls lists and chat, Left/Right switches channels and moves between columns in the Tools, channel, color, font-size and alert-sound grids.
- MQTT Monitor — a new tool under Live → Tools (press M) that shows a live count of how many messages are arriving on each channel under your configured MQTT root, so you can tell at a glance whether a root is actually carrying traffic and on which channels. Counts merge all gateways relaying the same channel into one row, reset when the screen closes, and C (Reset on Heltec) restarts the count in place. Available on all WiFi boards except Cardputer.

### Changed
- The Live → Tools grid now lists five tools; the MQTT row is shown but greyed out and reads "MQTT - WiFi off" when WiFi is switched off, instead of disappearing.
- ThinkNode M9 firmware should be flashed with the combined erase-and-upload step (`./build-upload-monitor.sh --m9 --erase`); the board does not reliably release its port for a separate erase pass.

### Fixed
- ThinkNode M9 units shipped with older LR1110 radio firmware no longer fail to start the radio with `[radio] init failed: -706`.
- ThinkNode M9: the radio's antenna switch is now configured on startup, restoring the receive sensitivity it would otherwise lose.
- Saving a channel no longer prints a false "does not exist" storage error to the serial console.
