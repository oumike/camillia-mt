### New
- The home dashboard's footer becomes a notification ticker — the messages that arrived while you were away, newest first, scrolling if they don't fit, and **No new messages** when there are none (ThinkNode M9, T-Lora Pager, and T-Deck / Mesh Deck with the nav bar switched off).
- The lock screen now shows GPS and Wi-Fi icons beside the battery, reading the same way they do on the chat screen: satellite count and green for a fix, Wi-Fi struck through when disconnected, an upload arrow while the device is serving its own access point. They follow the state as it changes rather than waiting for the next minute (on the T-Deck Pro they update with the clock, since every repaint is an e-paper refresh).
- The home dashboard shows the same GPS/Wi-Fi pair on Heltec V4 and Wio Tracker L2, whose bottom bar has no status icons of its own.
- The device info panel reports storage: the card's class, size and the clock it mounted at when one is present, and when it isn't, how many attempts have failed, whether the last probe tried every speed or just one, and when the next retry is due. Boards without a slot show their internal flash instead.

### Changed
- The home dashboard follows the UI theme — background, headings and chart cards come from the palette you picked, with the node name in the theme's accent — instead of always being a black slab. The lock screen keeps its fixed palette on purpose, and the T-Deck Pro stays black-on-white whatever the theme says.
- Choosing a theme now reboots the device, the way Orientation already does: the picker says `Theme: <name> - rebooting...` and the whole interface comes back in the new colours a few seconds later.
- Choosing **AP** in the WiFi picker now actually raises the access point: it switches the MQTT bridge off and Web Config on, then reports the address to browse to. With the master WiFi row off it changes nothing and says *enable WiFi first*.
- The nav bar's unread flash lights the whole **Chats** or **DM** cell rather than just the small icon inside it.
- On boards that can pair a Bluetooth keyboard, the **Web Config** row now sits directly under **Choose WiFi**, with the BT keyboard rows after it.
- T-Deck Pro: the nav bar's **DM** cell is now an outline envelope and **Chats** gets its speech balloon back, instead of channels showing a bare `#`.

### Fixed
- Messages arriving on the channel the chat screen has selected are no longer silently marked read while the home dashboard is up — they now raise the unread mark and reach the lock screen previews and the ticker like any other traffic.
- ThinkNode M9, T-Deck and Mesh Deck: the chat screen's key-hint line no longer wraps onto a second row the footer can't show; it now reads `(H)ome (C)hat C(f)g (D)M (N)odes Too(l)s`, with **A** still opening Actions.
- The chat footer's key hints no longer run underneath the status icons when a channel is unread.
- T-Lora Pager: the footer bar now spans the full width, so the bottom of the channel list no longer shows through as a strip beside it, and the hint line uses the same short wording as the other keyboard boards.
