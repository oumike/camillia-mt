### New
- **Mesh Beacon broadcast:** your device can now advertise your own mesh to nodes on another preset. You choose the preset, an interval from 1 to 24 hours, an optional message, and whether to include your primary channel's key. The radio retunes for a second or two, then returns to your own settings. Sending waits while a Discovery scan is running, and in EU_433 and EU_868 it also waits while airtime use is high. Set it up from the Mesh Beacons popup on the Config screen or in web config. All boards.
- **Beacons screen:** a new "Your beacon" line shows when your next beacon is due, when the last one went, or why it is being held. Received offers now show their frequency slot when the sender is on a non-default one.
- **Licensed (amateur) mode:** licensed operators can run under amateur rules and join Meshtastic ham meshes. Turn it on from the Config screen or web config. All boards except the Cardputer.
  - Your call sign becomes your identity, and the header shows `HAM` and your call sign.
  - Every channel key is removed, and DMs are sent as signed plaintext. PKI, remote admin and MQTT downlink are refused.
  - Relaying is local-only, and licensed and unlicensed nodes are kept separate. Your node identifies itself every 10 minutes.
  - Mesh Beacon broadcast and the chat server client are off while licensed. Listening for beacons still works.
  - The 70 cm (ITU1/2/3) and 1.25 m (ITU2) regions become selectable.
  - The call sign `N0CALL` lets you try the mode without transmitting.
- **Licensed mode is your legal responsibility.** Camillia does not check that you hold a licence. Make sure your antenna and RF front end suit the amateur band before you transmit.
- **Channel Panel setting** (Settings > Display and web config): keep the channel list docked beside the chat, or use it as a sliding drawer. When docked, the arrow keys or C switch channels. Tanmatsu, and T-Display P4 in landscape.
- **Arrow keys dock and undock the channel list** from the chat screen: Left opens or docks it, Right returns to the chat or hides it. Tanmatsu, and T-Display P4 in landscape.
- **Arrow keys move the text cursor** while you type a message. Tanmatsu only.
- **Keyboard navigation:** arrow keys move through popups and grids, onboarding buttons take keyboard focus (with W/F shortcuts), and Up/Down work in the chat. Tanmatsu only.
- **Keyboard Light setting** (Low, Medium, High or Off), on the device and in web config. Medium is the default, and the light goes dark while the screen sleeps. Tanmatsu only.
- **Web config can export your config to the SD card** on boards with a card slot.
- **Two small camellia flowers** now sit either side of "Camillia" on the home screen. All boards.
- **New config file keys:** `channelPanelLocked`, the Mesh Beacon broadcast keys under `module_config: mesh_beacon:`, `owner_licensed` and `owner_call_sign`.

### Changed
- **Native 800x480 display:** the Tanmatsu no longer doubles its pixels, so text is sharper and screens have more room. Tanmatsu only.
- **Shorter screen header** with less padding, most noticeable on the T-Display P4, Tanmatsu and T-Lora Pager. All boards.
- **Long sender names are now the default chat style** for new configs. Existing configs keep their current setting.
- **Tanmatsu transmit power is capped at 20 dBm,** even if set higher, because at 22 dBm the radio stopped receiving. Tanmatsu only.
- **Your settings carry over.** New settings start at their defaults when you upgrade.

### Fixed
- **The Tanmatsu now receives mesh messages.** Packets were being dropped before they could be processed. Tanmatsu only.
- **Messages are no longer shown on the wrong channel.** A channel without a key could pick up cleartext meant for a different channel, which showed up with licensed-mode traffic. All boards.
- **Steadier, more accurate battery percentage.** Tanmatsu only.

### Notes
- **Turning on licensed mode removes every channel key, and turning it off does not restore them.** Re-enter or re-import your keys afterwards.
- **After you leave licensed mode, relaying stays local-only.** To relay as before, set Rebroadcast back under Device in web config.
- **Leaving licensed mode while on a ham region** moves the device back to its default region. It will not transmit until you choose "Resume transmitting".
- **Licensed-mode traffic is unencrypted.** An unlicensed device can read it only on a channel with the same name and no key.

Pull request: #144
