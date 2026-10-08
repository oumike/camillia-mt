### New
- Touch boards with a lock screen: long-press a message preview on the lock screen to unlock, open that message's channel and bring up Message Actions for it. A short tap still dismisses the lock screen as before.
- T-Display P4 (landscape): the Home screen header now spans the full width. The node name and clock sit beside this node's ID, short name, firmware version and role. Current weather sits beside the fuller forecast details: feels-like, humidity, wind, location and how old the reading is.
- Heltec V4 and Wio Tracker L2 (portrait): Direct Messages and Nodes now stack their two panes top to bottom. The Nodes list and details use a larger font.

### Changed
- Direct Messages, Nodes, Configuration, Tools and Help now open with the same header bar as the chat screen. It shows the screen's name, the clock and the battery, plus GPS and Wi-Fi on boards whose chat header shows them. When you move between these screens, only the name changes.
- Screen titles now read "Direct Messages" and "Nodes (…)" instead of all capitals, in every translated language.
- The Nodes Filter button and the Configuration Info button are now in a row just below the header.
- Configuration no longer shows "Ready" in its header. An active search filter now appears after the screen name.
- Direct Messages and Nodes no longer draw boxes around their panes. A single divider line separates them, and the outer frame is gone so content gets more room. On Direct Messages, the active pane's title is underlined and highlighted. On Nodes, the active pane is lightly shaded.
- The header clock is now centred on the display instead of between the items on either side of it.
- T-Display P4, Heltec V4 and Wio Tracker L2 (portrait): the header clock is now on the right-hand side.
- Wio Tracker L2 (portrait): the header shows only the battery dot, without the percentage, to leave room for the clock and status icons.
- T-Display P4 (landscape): the Home date is centred, with GPS, Wi-Fi and battery grouped at the right.
- T-Display P4 (landscape): the node and channel lists on Home share one page again, using the full screen width.
- Touch boards in landscape: the lock screen uses the same header as Home, and its message cards sit in the same place as Home's widgets.
- Help uses larger, easier-to-read text, and its paragraphs wrap to fit the screen. The page scrolls under a fixed header. Cardputer keeps its small text.
- Help can now be scrolled from the keyboard on every board, using scroll, Page Up/Page Down or previous/next channel.
- Touch boards: Help no longer has a close (X) button. Leave it with the nav bar, as on other screens.

### Fixed
- T-Display P4 with the keyboard expansion attached: the battery percentage no longer drops too fast and then jumps. It is now based on the voltage of all connected cells, including the expansion's 21700 pack.
- Long channel names in the chat header now stay on one line and end with "…" instead of wrapping onto a second line.
- Pressing a message in the bubble or IRC chat views now uses that message's text when replying, instead of garbled text.
