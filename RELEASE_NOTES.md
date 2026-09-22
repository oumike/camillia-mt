### New
- Discovery sweeps and preset scans can now be set to run for 30 minutes, 1 hour, 2 hours or 6 hours, on top of the existing 30 sec to 15 min choices — useful for leaving a board listening somewhere and coming back to it. Note that a long *preset scan* parks the radio on the foreign preset for the whole window, so the node hears nothing on its own channels until it ends; a long sweep costs only the wait.

### Changed
- Discovery's progress line now counts in minutes and hours rather than raw seconds, so a long run reads as `43m/6h` instead of a five-digit second count.

### Fixed
- On keyboard builds, the Discovery key-hint line kept saying `C = Clear` while a sweep or scan was running, even though the on-screen button had already switched to Cancel. The hint and the button now always agree on what C does.
