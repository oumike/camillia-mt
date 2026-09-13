### New
- Weather and terrain line-of-sight are now available on the Cardputer, which previously had neither; both server addresses are set through the microSD `config.yaml` (`network: losElevServer:` / `weatherServer:`), and weather ships with a working default so it needs no setup at all.
- The sleep clock screen now shows current conditions and temperature beside the node name and time, using the last reading the Weather screen fetched; it stays blank rather than showing a stale reading after the device has been asleep for hours.
- Web config has an Idle Timeout setting (5 minutes through 1 hour, or Never) to control how long it stays up with no activity before closing itself and releasing WiFi — previously the 10-minute default could only be changed by hand-editing a config file.
- Config export now carries the line-of-sight elevation server and weather server addresses.

### Changed
- The sleep clock screen has a divider rule under the wordmark, separating the device name from the status below it.
- Cardputer: the weather screen uses a compact layout for its 240x135 panel — temperature and conditions share one line, and the details fold into three lines instead of four.
- Cardputer: the line-of-sight cross-section is drawn shorter so the verdict and numbers below it stay on screen.

### Fixed
- Cardputer: the `,` and `/` arrow keys now move left and right without holding Fn, matching the `;` and `.` keys — sliders and anything else using left/right can be driven from the bare arrow cluster.
- Restoring a config backup no longer comes back with weather and terrain line-of-sight switched off, since the backup now includes their server addresses.
- An out-of-range web config idle timeout imported from a config file is clamped to a usable value instead of closing the page before it can be read.


### Update (v5.1.0)
### New
- Home dashboard on every board with a display except the Cardputer: node name, clock, conditions, date and battery across the top, with live channel-utilisation and SNR/RSSI charts below. The device now boots onto it, and chat is one press away.
- Elecrow ThinkNode M9: the **Home** button opens the dashboard and **Messages** goes to the chat screen, with a second press of Messages opening the channel list.
- The bottom nav bar gains a **Chats** cell beside Home, so the dashboard and the messages each have a button of their own. Whichever of the two you are on is the lit cell.
- Unread channel messages are now marked as well as unread DMs. Boards with the nav bar blink the **Chats** cell; boards without one show a bell beside the DM envelope in the shortcut bar. Both blink on the same phase and clear when you open the conversation, and muted channels raise nothing.
- The compose window now names where the message is going — "New Message: LongFast" — so the channel is visible even when composing from the dashboard.
- The boot log now prints the exact ESP32-S3 variant, revision, flash size and PSRAM, so a board-specific fault can be told apart from a dead card or empty slot in someone else's log.

### Changed
- Keyboard boards (T-Deck, T-Deck Pro, T-Lora Pager TFT, Attaky Mesh Deck): **H** opens the home dashboard, **C** goes to chat (press again for the channel list) and **F** opens Config, which used to be C. D, N and L are unchanged.
- The **Alt+H / Sym+H** chord now opens the dashboard, and **Alt+C / Sym+C** jumps straight to chat without opening the channel list.
- T-Deck Pro: Config has no Alt chord, because Alt+F is already next-channel on that keyboard. Plain F and the nav bar still open it.
- Elecrow ThinkNode M9: the hardware buttons shift along one — **Messages** opens chat, **Home** the dashboard, the key below Home opens DMs and the key below Back opens Tools. Nodes loses its dedicated button; N on the keyboard still reaches it.
- T-Deck Pro: the dashboard's charts redraw once a minute rather than on every packet received, since each redraw is an e-paper refresh on a screen that opens itself.
- Space pressed on the home dashboard always starts a new message, rather than quietly replying to a message selected earlier on the chat screen.
- The on-device help screen lists the new key assignments.
- Seeed Wio Tracker L2 now has its own flash layout with 6 MB app slots. It keeps its files on the SD card, so the internal filesystem partition it could never mount has been given back to the firmware — the same image that filled 98.6% of the old slot now sits at about half.
- Seeed Wio Tracker L2, for units already carrying a pre-release build: flashing this layout over USB moves NVS, and the device comes up with settings, channels and the node database empty. Export your config to the SD card first (Config → Export). An OTA update does not rewrite the partition table, so such a unit keeps the old, nearly full slots until it is reflashed over USB once.

### Fixed
- Boards with an empty or unreadable SD slot no longer stall for about three seconds every time the card is re-probed, which was stuttering the screen and dropping keystrokes. The first probe still tries all three clock speeds; later ones try one per attempt, and inserting a card triggers a full re-scan.
- Announcing your position no longer freezes the screen, keyboard and web config for several seconds when more than one channel shares location — the packets now go out one per pass instead of all at once.
