### New
- **Config sections on touch boards:** Config now opens on a two-column grid of large buttons: Radio & Identity, Connections, Location, Channels & Security, Display, Chat, Sound & Alerts, Mesh Services, Device & Firmware, and Data & Reset. Tap one to see its settings, and use **‹ Back** to return to the grid. When you close a picker, you land back in the section you opened it from. Info shows on the grid only. Sections with no settings on your board are left out. On boards that also have a keyboard, the keys move around the grid, Enter opens a section, Backspace goes back, and typing still searches every setting. Applies to T-Deck, T-Deck Pro, Heltec V4 / V4-R8, Attaky Mesh Deck, Wio Tracker L2, T-Display P4 and CrowPanel. Cardputer, T-Lora Pager and ThinkNode M9 are unchanged.
- **Section names are in English for now:** the new section names show in English until they are translated.
- **New-message dots:** a newly received message now shows a small dot in the theme's accent colour until you have read the conversation. A conversation counts as read after 15 seconds on screen, or when you leave it. In the Classic and IRC styles the dot sits between the time and the sender's name. In Bubbles and Outline it sits to the left of the bubble. Works in channels and DMs, and on the T-Deck Pro the dot is black.

### Changed
- **Message Blink removed:** new messages no longer blink, and the Message Blink setting is gone from the device, the web config page and the YAML export. There is nothing to migrate. Backups that include the old setting still import. Notification lights (keyboard blink, notification LED and light timeouts) work as before.
- **One tap runs a Config setting on T-Deck and T-Deck Pro:** before, a tap only moved the highlight and you had to press Enter. Enter still works.

### Fixed
- **Home dashboard:** the SNR/RSSI card no longer blinks along with the Chats indicator. This was seen on the T-Deck and could happen on any board with the nav bar.

Pull request: #134
