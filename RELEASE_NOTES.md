### New
- Added a `square` build target for an unreleased 320×240 touch board (SX1262 LoRa, GNSS, microSD, browser Host/Remote control). It is a bring-up target with hardware still unverified, so no `square` binary is attached to this release — build it yourself with `./build-upload-monitor.sh --square`.

### Changed
- Heltec V4 (both orientations): in the Live view the Tools button moved to the left of the header and the filter chip is now pinned to the right edge, so long filter names like "Filter: Telemetry" stay fully readable; the "LIVE" title is now centered in the vertical layout as well.

---

Not part of the notes: `platformio.ini:254` switches **heltec-v4** (landscape only) from `partitions_16mb_fs.csv` to `partitions.csv`, which drops the 9.5 MB LittleFS partition that board uses for config export/import and the node archive — while the new `square` env takes `partitions_16mb_fs.csv` even though `docs/HARDWARE.md`/`BUILD.md` in this same diff say Heltec keeps the FS table and Square uses the standard one. It reads like the two values landed in the wrong env blocks; worth checking before you tag v4.6.1.
