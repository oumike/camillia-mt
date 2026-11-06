### New
- **Mesh Beacon broadcast:** your device can now send its own Mesh Beacon on a preset you choose, every 1, 2, 3, 6, 12 or 24 hours. The beacon advertises your primary channel, preset and region, with an optional message of up to 60 bytes. Set it up from the Mesh Beacons popup on the Config screen or in web config. The Beacons screen shows when your next beacon is due and why one is being held. In EU_433 and EU_868 it waits while your airtime use is high. All boards.
- **Mesh Beacon "Include key" option:** when it is on, the beacon carries your primary channel's key, so anyone who hears it can join and read that channel. It is off by default.
- **Licensed (amateur) mode**, for licensed radio amateurs, set from the Config screen or web config. Your call sign becomes your long name and the header shows HAM and your call sign. Every channel key is removed. DMs go as signed plaintext. Remote admin, MQTT downlink, the Mesh Beacon and the chat server client are turned off. Relaying is local-only, licensed and unlicensed nodes are kept apart, and your station ID goes out every 10 minutes. All boards except Cardputer.
- **Licensed mode adds the 70 cm (ITU1, ITU2, ITU3) and 1.25 m (ITU2) amateur regions.** They are only offered while licensed mode is on. The 2 m regions stay hidden because no current board's radio reaches them.
- **Licensed mode can be tried with the call sign N0CALL.** The node runs licensed but never transmits.
- **Channel Panel setting** (Settings > Display and web config): keep the channel list docked beside the chat (the default), or use it as a sliding drawer. Tanmatsu, and T-Display P4 in landscape.
- **The arrow keys dock and undock the channel list** when you are not typing.
  - Docked: Left (or C) moves into the list, and Up/Down and Enter switch channel. Right returns to the chat, and Right again undocks and hides the list.
  - Undocked: Left opens the drawer and Left again docks it. Enter or Right picks a channel and closes the drawer.
  - Tanmatsu, and T-Display P4 in landscape with the keyboard expansion.
- **Keyboard Light setting** (Settings > Display and web config): Low, Medium (the default), High or Off. The light goes dark while the screen sleeps. Tanmatsu only.
- **Web config can export your configuration to the SD card** on boards with a card slot.
- **Two small camellia flowers** now sit either side of "Camillia" on the home screen. All boards.

### Changed
- **The Tanmatsu now draws its screen at the panel's full 800x480 resolution**, so text is sharper and screens hold more.
- **More keyboard control on the Tanmatsu:** the arrow keys move the cursor while you write a message and move the focus in popups and grids. Onboarding buttons can be used from the keyboard, with W and F shortcuts, and Up/Down work while the chat cursor is active.
- **The Tanmatsu's transmit power is now capped at 20 dBm**, even if a higher value is set. At 22 dBm it stopped receiving.
- **The screen header is shorter**, which leaves more room for content. All boards, most noticeable on the T-Display P4, Tanmatsu and T-Lora Pager.
- **Chat now shows senders' long names by default.** This only applies to new configurations; existing settings are kept.
- **Settings from earlier versions carry over**, and the new settings start at their defaults. Configuration exports gain the keys `channelPanelLocked`, `owner_licensed` and `owner_call_sign`, plus the new beacon keys under `module_config: mesh_beacon`.
- **Turning on licensed mode is your legal responsibility:** Camillia does not verify your licence.
  - Turning it on removes every channel key, and turning it off does not bring them back.
  - After you turn it off, relaying stays local-only until you change it back in Device, Rebroadcast in web config.
  - If you turn it off while on an amateur region, the region returns to the board's default and transmit stays off until you choose "Resume transmitting".
- **Licensed-mode traffic is sent unencrypted.** An unlicensed device can only read it on a channel with the same name and no key.

### Fixed
- **The Tanmatsu now receives mesh messages.** Before, it discarded incoming packets.
- **Messages no longer land in the wrong channel.** A channel without a key could pick up unencrypted messages meant for another channel. All boards.
- **The Tanmatsu's battery percentage is steadier and more accurate.**

Pull request: #144
