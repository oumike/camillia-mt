### New
- **Web Config (all boards):** Node Identity now shows which hardware model the device announces to other nodes, for example "THINKNODE_M9 (131)". On boards Meshtastic has no ID for, it also says that other nodes show the board as unknown hardware. This line also appears on Web Config Lite.
- **CardputerZero:** dropped nodes can now be archived to the device's own storage. The archive is off by default. Turn it on in Web Config under Node Management (*Archive dropped nodes to storage*) or in Device Config. Web Config shows where the archive file is kept, and Clear Nodes can include archived nodes.
- **CardputerZero:** when you turn on Web Config from the Config screen, a notice shows the full address, port included (`http://<ip>:18765/`). The Config row keeps showing that address.

### Changed
- **Cardputer-Adv and CardputerZero:** while the message cursor is on in a chat, Up and Down move it to the previous or next message, so Reply and Message Actions can target any message. Press Esc or Backspace to turn the cursor off; Up and Down then scroll one line at a time as before.
- **CardputerZero:** Wi-Fi is now managed in the system's network settings. Web Config's WiFi section only shows the network the system is connected to. It no longer has SSID or Password fields or a Saved Networks list. The idle-timeout setting stays.
- **CardputerZero:** config exports no longer contain Wi-Fi networks or passwords. When you import a file from another board, its Wi-Fi networks are skipped and a warning says so. The Wi-Fi on/off setting is still exported and restored.
- Settings and node identity carry over unchanged. No migration is needed.

### Fixed
- **Heltec V4-R8:** other nodes now show it as "Heltec V4 R8" instead of "Heltec V4". Meshtastic apps and firmware older than 2.7.24 will show it as unknown until they update.
- **Elecrow ThinkNode M9:** other nodes now show it as "Elecrow ThinkNode M9" instead of unknown hardware. Meshtastic apps and firmware older than 2.7.22 still show it as unknown.
- **Cardputer-Adv:** other nodes now show it as "Cardputer Mesh Kit" instead of unknown hardware. This build is for the Cardputer-Adv with the Cap LoRa-1262. It does not run on the original Cardputer.
- New hardware names appear on other nodes after this device's next NodeInfo broadcast.

Pull request: #164
