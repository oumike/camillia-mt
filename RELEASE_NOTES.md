### New
- New **home dashboard**: node name, clock, conditions, date and battery over live charts of channel utilisation and SNR/RSSI — available on every board with a display except the Cardputer, whose panel is too small for it
- The device now boots onto the dashboard rather than the chat screen; chat is one press away underneath
- Reaching it: **H** or the Alt+H / Sym+H chord on keyboard boards, the **Home** button on the ThinkNode M9, and the nav bar's **Home** cell on the touch-only Heltec V4 and Wio Tracker L2 — where tapping Home a second time steps aside to the chat underneath

### Changed
- Keyboard boards (T-Deck, T-Deck Pro, T-Lora Pager, Mesh Deck): **C** is now the chat screen, pressed again to open the channel list, and **F** opens Config, which used to be C; D, N and L are unchanged
- T-Deck Pro: plain **F** opens Config as before, but there is no Alt chord for it, because Alt+F is already next-channel on that keyboard
- ThinkNode M9 hardware buttons shift up one: **Messages** is the chat screen (press it again for the channel list, which is what Home used to do), **Home** is the dashboard, and the key below Home is DMs — Nodes loses its dedicated button and is reached with **N** or from Tools
- T-Deck Pro: the dashboard's charts redraw once a minute instead of on every packet received, since each redraw is a full e-paper refresh on a screen that opens itself
- Wio Tracker L2 gets its own flash layout with **6 MB app slots** instead of 3.125 MB, reclaiming the file partition this board never used
- Wio Tracker L2 units already running a pre-release build: reflashing over USB lands on the new layout and comes up with **empty settings, channels and node database** — export your config to the SD card first (Config → Export). An OTA update keeps the old layout, so a USB reflash is needed once to get the extra space

### Fixed
- Announcing position no longer freezes the screen and keyboard for several seconds when multiple channels share location — the packets now go out one at a time with the interface responsive in between (most noticeable on the ThinkNode M9)
- Devices with an empty SD slot no longer stall for about three seconds every time they retry looking for a card; a card inserted later is still found
