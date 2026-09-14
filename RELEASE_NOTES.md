### New
- The home dashboard's footer becomes a notification ticker, listing the messages that arrived while you were away — newest first, scrolling if they don't fit, and **No new messages** when there's nothing waiting (Elecrow ThinkNode M9, T-Lora Pager, and T-Deck / Attaky Mesh Deck with the nav bar switched off).
- The lock screen now shows GPS and Wi-Fi icons next to the battery: satellite count and green with a fix, Wi-Fi struck through when disconnected, an upload arrow while the device is serving its own access point. They update the moment the state changes (on the T-Deck Pro they move with the clock, since every repaint is an e-paper refresh).
- The home dashboard carries the same GPS/Wi-Fi pair on Heltec V4 and Seeed Wio Tracker L2, whose bottom bar has no status icons of its own.
- Device info now reports storage: with a card mounted it shows the card's class, size and the clock speed it answered at; with no card it shows how many probes have failed, whether the last one tried every speed or just one, and when the next retry is due. Boards without a slot show their internal flash instead.

### Changed
- The home dashboard follows your chosen UI theme — background, headings and chart cards from the palette, node name in the accent colour — rather than always being a black slab. The lock screen keeps its fixed look on purpose, and the T-Deck Pro stays black-on-white whatever the theme.
- Picking a theme now reboots the device, the way Orientation already does: the row reads `Theme: <name> - rebooting...` and the whole interface comes back in the new colours a few seconds later.
- Choosing **AP** in the WiFi picker now actually brings the access point up — it switches the MQTT bridge off, Web Config on, and reports the address to browse to. With the master WiFi row off it changes nothing and says *enable WiFi first*.
- The nav bar's unread alert lights the whole **Chats** or **DM** cell instead of just the small icon inside it.
- On boards that can pair a Bluetooth keyboard, the **Web Config** row now sits directly under **Choose WiFi**, with the Bluetooth keyboard rows after it.
- T-Deck Pro: the nav bar's **DM** cell is an outline envelope and **Chats** gets its speech balloon back, instead of channels showing a bare `#`.

### Fixed
- Messages arriving on the channel chat has selected are no longer silently marked read while you're sitting on the home dashboard — they now raise the unread mark and reach the lock screen previews and the ticker like any other traffic.
- Elecrow ThinkNode M9, T-Deck and Attaky Mesh Deck: the chat screen's key-hint line no longer wraps onto a second row the footer can't show; it now reads `(H)ome (C)hat C(f)g (D)M (N)odes Too(l)s`, with **A** still opening Actions.
- The chat footer's key hints no longer run underneath the status icons when a channel is unread.
- T-Lora Pager: the footer bar spans the full width, so the bottom of the channel list no longer shows through as a strip beside it, and the hint line uses the same short wording as the other keyboard boards.
