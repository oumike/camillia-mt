### New
- The lock screen's band under the clock is now a carousel: unread message previews, the channel/air-utilisation and SNR/RSSI charts, and the Recently Heard / Longest Silent node lists, turning itself every 30 seconds so a locked device cycles through everything it knows in about two minutes. The T-Deck Pro keeps its static e-paper sleep screen, which does not rotate.
- The lock screen skips its message page whenever nothing is unread, so the band never sits on a blank rectangle; a message arriving while locked brings it back at the next turn.
- **j** and **k** turn the home dashboard's glance carousel on any board with a keyboard — k forward, j back, matching how j and k move everywhere else in the firmware.
- T-Deck Pro: tap anywhere on the dashboard band to turn it one page forward, instead of having to land a slow drag on the e-paper panel. Left/right on the keyboard still goes back.

### Changed
- Wio Tracker L2: the top Wake button now locks or blanks the screen on a short press and needs a two-second hold to bring the device back, from the lock screen and from a dark panel alike. A short press while locked or dark does nothing, so a button pressed in a pocket no longer wakes the device.
- The Recently Heard and Longest Silent lists show each node's long name when it has advertised one, falling back to the short name and then the `!hex` id — the same naming the Discovery and Beacons screens use.
- Node rows keep the age pinned to the right edge and ellipsize a too-long name, so the age the page is sorted by can no longer be pushed off the card.

### Fixed
- Pressing j or k while typing no longer turns the dashboard carousel out from under the text you are entering.
