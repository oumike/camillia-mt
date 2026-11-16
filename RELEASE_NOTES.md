### New
- About 45 settings that used to be web-config-only can now be changed on the device, each in its own Config section: Radio & Identity, Connections, Location, Mesh Services, and Device & Firmware. This covers region, TX power, hop limit, device role, custom modem, RX boosted gain, MQTT broker and options, position and GPS intervals, fixed position, telemetry, neighborhood info, auto-favorite, LOS and weather servers, time zone, NTP server, power saving, update checks and serial debug logging. A choice opens a picker and a text setting opens a text box. Region, Custom Modem, RX Boosted Gain, Node ID from Key and Node ID Override reboot to apply, and the on/off ones ask first. Not on the Cardputer, whose web config has all of these settings.
- Node ID Override can be set for the first time, on the device and in web config. It fixes this node's ID (e.g. `!1a2b3c4d`) and takes priority over both the default ID and Node ID from Key. Leave it blank to turn it off. Changing it reboots the device. A warning comes first: other nodes will see a new node, existing DMs and favorites stay with the old ID, and two nodes with the same ID disrupt each other on the mesh. On the Cardputer it is in web config only.
- Several config backups can be kept on the card. When the device knows the date and time, Export asks whether to replace `config.yaml` or write a new dated backup beside it (e.g. `config-20261115-143205.yaml`). If the time isn't set, Export writes `config.yaml` as before. Applies to boards with SD or internal storage.
- When there is more than one backup on the card, Import lists them (`config.yaml` first, then the dated ones newest first) and asks again by file name before importing and rebooting.
- Web config gains GPS standby, RX boosted gain, battery calibration, chat colors and my message color, on all boards. It also gains a keyboard light on/off switch on the T-Deck Pro and T-Display P4.
- The Cardputer's web config now covers every setting, including Node ID from Key, signed packets and serial debug logging.

### Changed
- Config backups now also carry the Node ID Override. Restoring a backup that has one onto a second device gives both devices the same ID, the same caution that already applies to the exported private key.
- New exports include the WiFi on/off switch, a custom My Message Color, the Node ID Override and the stored custom modem settings. Older firmware ignores these additions, so a new export still imports on an older build.
- Changing the web config idle timeout on the device now applies to the web config session already running.
- The LOS elevation server and weather server can now be set on the device (Config → Mesh Services) or in web config under Device, on both the full and the lite page.
- Unused settings have been removed: Bluetooth phone pairing (enabled/mode/PIN), compass north-up, flip screen, canned messages, chat spacing, light-sleep/minimum-wake seconds, MQTT TLS and the notify LED master switch. The light-sleep/minimum-wake fields, MQTT TLS and chat spacing no longer appear in web config.
- Older config exports still import. Removed settings are ignored, except `notifyLed: false`, which still turns both notify LED colors off.
- No config migration is needed. Existing devices keep all their settings after updating.

### Fixed
- Restoring a backup now restores the WiFi on/off switch. Before, WiFi could stay off after a restore, and with it any MQTT setup from the backup.
- A custom My Message Color is now kept through export and import. Before, it came back as the default color.
- Exporting while a modem preset was active no longer overwrites your saved custom modem settings when you import the backup.

Pull request: #154
