### New
- Low-battery protection: the device now warns as the pack runs down, drops Wi-Fi, the web config and GPS to stretch the remaining charge, then saves everything and switches off cleanly instead of brown-out looping until the cell is over-discharged.
- The device will not shut itself down while USB or a charger is connected, and after a low-battery shutdown it says on the next boot that an empty battery was the reason.
- A battery that is already flat at power-on now stops the boot instead of restarting over and over; plug in USB and it starts normally.
- On T-Lora Pager TFT and T-Deck Pro the low-battery shutdown genuinely disconnects the battery and comes back when USB is plugged in; other boards go into deep sleep and wake on the user button.
- Nodes screen can now list nodes that have been dropped from the live table and archived to SD, shown dimmed and marked `~` below the live ones, searchable by name, with a details panel giving the archived date, when the node was last heard, and the position that was saved (boards with an SD card slot).
- Archived nodes can be restored to the live list from the node action key, bringing back their name, position and public key; the confirmation warns that a full table archives its oldest non-favorite to make room.
- New Config rows "Archive Dropped Nodes" and "Show Archived Nodes" on boards with an SD slot, also settable from the web config; both show "(no card)" when the slot is empty.
- Alt+H (home), Alt+D (direct messages), Alt+N (nodes), Alt+L (live) and Alt+C (config) now jump straight to a screen from anywhere on T-Deck, T-Deck Pro and Attaky Mesh Deck — previously only Alt+H — and all five are new on the M5Stack Cardputer.

### Changed
- Switching the release channel now checks that channel for an update straight away: the Config row shows "checking…" and then the result, whether or not "Check for Updates on Boot" is on.
- Changing the release channel from the web config runs that check after the reboot and offers any update it finds on the device's own screen.
- The Nodes header count includes archived nodes when they are shown, with a trailing "+" when the archive holds more than the screen lists.
- "Clear Nodes (All)" now also deletes the archived-node file from the SD card, both on the device and from the web config, and says so when it does.

### Fixed
- An update found by the boot check is no longer lost when you open Config: the Firmware Update row keeps offering "Install <tag>" until it is installed or the channel changes.


### Update (v4.8.1)
### Changed
- Settings screen reordered into task-based groups: identity, radios, the services those radios carry, appearance, alerts, mesh modules, and the things you set once or rarely.
- Web Config, MQTT, and (where available) VNC Host now sit directly under the radio rows they depend on.
- Theme now heads the appearance settings instead of sitting further down the list.
- Chat Colors, your own message color, and Reset Chat Colors are now three consecutive rows; Reset Chat Colors is no longer down with the maintenance actions.
- Nav Bar now sits with Brightness and the other display settings.
- Startup Melody moved up to join the other sound settings; the LED alert row follows the sound rows it accompanies.
- Announce and Telemetry moved down with the other occasional settings rather than heading the mesh module group.
- Export and Import Config moved down to the maintenance group on boards that have them (SD-card boards, plus the Attaky Mesh Deck's internal-flash path).
- Check for Updates, Update Channel, and Release Notes now sit at the very bottom of the settings list; the Cardputer still shows Release Notes without the update rows.
