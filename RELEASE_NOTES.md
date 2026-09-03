### Changed
- The board previously known only by its codename "Square" is now identified as the Seeed Wio Tracker L2 throughout the firmware, docs, and downloads.
- Wio Tracker L2 release downloads are now named `camillia-mt-wio-tracker-l2-vX.Y.Z.bin` instead of `camillia-mt-square-vX.Y.Z.bin`.
- Wio Tracker L2: over-the-air updates now look for the renamed firmware file, so a device still running an older "square" build needs one manual flash of v4.7.11 before OTA updates resume.
- Building from source for the Wio Tracker L2 now uses `--wio-tracker-l2` (environment `wio-tracker-l2`) in place of the old `--square` flag, and the device picker lists it as "Seeed Wio Tracker L2".
