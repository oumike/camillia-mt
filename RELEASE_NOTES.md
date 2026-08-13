### Added
- T-Deck: added an experimental browser VNC host that mirrors the live screen and accepts taps and keyboard input. It requires an active Wi-Fi network connection, runs alongside Web Config, and appears there as a VNC tab; the direct viewer remains available on port 8765.

### Fixed
- Mesh Deck: quick keystrokes no longer go missing — a key press now registers the instant it is seen instead of having to be held across two scans, so fast typing and short taps on Enter land reliably.
- Mesh Deck: when a single scan catches two keys at once, both are delivered right away rather than one being held back until the next pass.
- Opening the channel list no longer bogs the interface down; the channel buttons are only redrawn when something about them actually changes, keeping key presses responsive while the list is on screen.
