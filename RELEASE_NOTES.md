### New
- **Flash and Customize** from the web flasher: right after flashing, the flasher can set the radio preset, the channel plan, a boot logo and your setup answers, so a fresh install starts straight in the app instead of on-device setup. This needs this release. With older firmware, the flasher reports that customization is unsupported. All boards.
- Setup answers sent from the flasher (long name, short name, region, role, Wi-Fi network and password) finish first-boot setup for you. A region is required. Blank names are filled from the node ID, for example "Camillia 1a2b" / "1a2b", and the role defaults to Client Mute. On a device that is already set up, only the fields you send change. All boards.
- A radio preset can be pinned to a specific frequency slot, for meshes that run a stock preset off its default slot (such as Medium Fast on slot 51, Taiwan on slot 1, or 433 MHz networks). A slot your region does not have is refused. All boards.
- Custom boot logo: a PNG of up to 8 KB, uploaded from the flasher, replaces the flower on the startup screen. Supported on the standard startup screen and on the T-Lora Pager and Cardputer layouts.
- CrowPanel Advance 3.5 now accepts serial commands over its native USB port as well as its UART port, so it works with Flash and Customize on either connection.

### Changed
- Upgrading keeps your existing settings, with no reconfiguration needed.
- Serial commands can now be up to 256 characters long. A command that is too long is rejected instead of being cut short and run. Replies go back to the port the command came in on. All boards.

### Fixed
- Replies over USB serial no longer arrive 5–14 seconds late. Affects boards that use the chip's built-in USB serial as their console, plus CrowPanel Advance 3.5.

Pull request: #122


### Update (v6.1.1)
### Changed
- Web Config and the MQTT Bridge can now be on at the same time, sharing your Wi-Fi network. While the bridge is on, Web Config no longer falls back to its own access point. If your network is out of range, the row shows "Web Config: On (waiting for WiFi)", and the page comes up once the device joins the network.

### Fixed
- ThinkNode M9: on the Locate map, the d-pad Left and Right buttons now move the map left and right. Before, they moved it up and down.
