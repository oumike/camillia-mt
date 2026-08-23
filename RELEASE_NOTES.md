### New
- Heltec V4: the on-board environment sensors are now found and read — temperature, humidity and pressure are reported over the mesh.
- Added support for Sensirion SHT4x, SHT3x/GXHT30 and SHTC3 temperature/humidity parts alongside the existing BME280, BMP280 and AHT20.
- Device Info now has an Environment column listing every detected sensor by name and address with its current readings.
- Web Config's Environment Sensor Telemetry section says which sensor was detected and on which bus, or reports that none was found; the setting can now be switched on ahead of fitting a sensor instead of being greyed out.

### Changed
- Heltec V4: Locate and the state map download section are gone — that board has no card slot and too little spare memory to decode a map.
- Sensor detection now happens once at boot, on the buses the board actually declares, and stops after two attempts instead of re-scanning every half minute for the rest of the session.
- The serial `i2c scan` and `i2c scan all` commands check more addresses and both SDA/SCL orderings, so a mis-wired sensor shows up instead of reading as "nothing there".

### Fixed
- Heltec V4: touch dropouts and second-long freezes caused by sensor scanning reconfiguring the display's clock and reset pins while the panel was running.
- A radio that cannot transmit no longer leaves the device permanently sluggish — failed announcements now back off from 5 seconds up to a few minutes instead of blocking the interface every 5 seconds forever.
- Opening Web Config no longer stalls the screen for over half a second while it checks which state maps are stored.
- Boards that keep chat history in internal flash boot faster.
- ThinkNode M9: the Maps Download section now reaches storage correctly.
