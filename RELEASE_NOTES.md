### New
- T-Deck: added a one-time serial boot message that identifies which touch controller your board actually has, to help diagnose touch issues.

### Fixed
- T-Deck: touch no longer goes dead after the screen sleeps and wakes on panel revisions that never recovered from the deep-sleep command (the units at I2C address 0x5D); the display now leaves touch in a low-power idle that always wakes cleanly.
