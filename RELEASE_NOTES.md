### New
- Admin peers now find themselves: favourite nodes that carry this device's public key are probed once per boot, and the ones that accept administration appear on their own under web config → Utilities → Remote Admin.
- **Scan favourites** button in Remote Admin re-runs that sweep on demand, so a node that was off or out of range at boot can be picked up without a reboot (available once every 30 seconds).
- The Remote Admin peer list now shows each peer by name, a confirmed/unconfirmed badge, when it last answered, and a **Verify** button that re-asks a single node.
- An **Admin** button on each node's card in the web config Nodes tab opens the terminal, alongside the existing Terminal button in Utilities.
- Typing `help` in the browser terminal — or pressing the new **Help** button — opens the command table in a panel beside the transcript instead of scrolling it past you.
- Admin terminal replies are now printed field by field with names and values — firmware version, role, channel name, uplink/downlink and so on — instead of a byte count; fields this firmware does not recognise are still listed by number.
- `set owner long <text>` and `set owner short <text>` rename a remote node from the terminal.
- `fixedpos <lat> <lon> [alt]` sets a remote node's fixed position, and `fixedpos clear` removes it.
- `peers` lists the admin peers this device knows and marks the one the session is pointed at.
- Module config blocks are read by name — `module telemetry`, `module mqtt` — rather than by number.
- If a background probe discovers mid-session that the remote has stopped accepting administration, the terminal says so in place rather than leaving you to find out from the next failed write.
- T-Deck Pro: the admin terminal gains on-screen scroll arrows, a rule between the transcript and the input line, and per-line markers (`-`, `+`, `!`) so replies, confirmations and errors are tellable apart on a one-bit panel.

### Changed
- Where the bottom nav bar is a setting — T-Deck, T-Deck Pro and Mesh Deck — the unread marks now follow the setting: turn the bar on and the alert moves onto the **Chats** and **DM** cells instead of staying in the footer corner.
- The nav bar's status cluster is now just GPS and Wi-Fi; the envelope and bell have gone, since the cells say the same thing, and the buttons grow into the width they were holding.
- T-Deck Pro: an unread nav cell is marked with a heavier border and held until the conversation is opened, rather than blinking — every flip was a full e-paper refresh.
- The nav bar takes its colours from the active theme in dark mode instead of staying navy under a red or green theme.
- The admin terminal's title shows the node's name where one is known, rather than its hex id.
- The terminal is offered on any listed peer the remote has not refused, not only proved ones, so it stops looking broken for the first minute after every boot; peers that have actually refused are dropped from the list.
- The MQTT Bridge confirmation now reads "Turn MQTT Bridge on and reboot?" rather than quoting the row's current state, and names the reboot it causes.

### Fixed
- T-Deck Pro: the keyboard backlight setting survives a reboot and a flash. Alt+B is saved the moment you press it, so a keyboard you turned dark comes back dark; the setting also travels with an exported config.
- The Remote Admin terminal would not appear at all when opened from web config's Utilities tab.
- **Verify** in web config never worked on the peers that needed it, because it refused any node that was not already confirmed.
- Setting text fields over the admin terminal failed with "string fields are not spliced yet"; they now write like any other field.
- Confirmation dialogs no longer run their buttons off the edge on 240-pixel panels (Cardputer, T-Deck Pro), and an over-long third button shortens instead of pushing the row off screen.
