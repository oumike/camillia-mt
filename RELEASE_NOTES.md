### New
- Serial console command `gps` (also `gps status`) prints a GPS diagnostic: which pins and baud rate are in use, how many bytes and valid NMEA sentences have arrived, fix and satellite counts, the live state of the enable/reset lines, and a plain-language verdict telling you whether the module is dead, mis-wired, or simply needs a better view of the sky.

### Changed
- Heltec V4 and ThinkNode M9: when GPS duty cycling parks the receiver, the module's enable line is now switched off as well as being asked to sleep, so a module that ignores the standby request no longer keeps drawing current.

### Fixed
- Heltec V4: GPS receive and transmit pins were swapped, so the device listened on a silent pin and never saw the module. GPS now works.
- Heltec V4: the GPS module's enable and reset lines are now driven at startup and after wake, instead of the module only being heard if it happened to power up already enabled.
- ThinkNode M9: GPS receive and transmit pins were swapped, which left the receiver reading as completely silent at every baud rate. GPS now works.
- Heltec V4: `env scan all` no longer probes GPIO 41/42, which was driving the GPS module's reset line during an I2C scan.
- Heltec V4: GPS pin probing no longer touches GPIO 43/44, which was reconfiguring the touch controller's reset line while searching for the receiver.
