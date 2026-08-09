### New
- Discovery: press C (or the Clear button) to empty every group and refill the screen from live traffic, so it shows who is on the air now instead of everything the device has ever met — nodes and names on the Nodes screen are untouched.
- Discovery: press S to save a timestamped JSON snapshot to `/camillia/discovery-YYYYMMDD-HHMMSS.json` on boards with an SD card (T-Deck, T-Lora Pager), including the raw neighbor reports; with no clock set yet the file is named by uptime instead.

### Changed
- Discovery lists each node by its long name when one is known, clipped to fit the column instead of wrapping, falling back to the short name and then the hex ID.
- Discovery group headings (DIRECT, 1 HOP, HEARD ABOUT, …) are drawn larger and in amber so the groups separate at a glance.
- Discovery sweep moved from S to W to make room for Save; the Heltec V4 sweep button is unchanged.

### Fixed
- Discovery now redraws when a node's hop distance or signal changes, not only when a node is added or a new neighbor report arrives.
- Discovery no longer rebuilds the whole list once a second while a sweep counts down; the countdown updates on the status line only.
