### New
- **MQTT map reporting** — when the MQTT bridge is on and connected, the node now publishes a short self-description (long/short name, hardware model, firmware version, region, modem preset, whether the primary channel still uses the default key, how many nodes it has heard in the last two hours, and a position) to `<root>/2/map/` every 15 minutes. This is what puts a node on an MQTT-fed map; it goes to the broker only and never over LoRa. Enable it under MQTT in Web Config.
- Map reports are skipped entirely when there is no position to send, and the position they carry is coarsened by the same Location Precision as the one you broadcast on the mesh — a map report can never be more revealing than what you already transmit.
- **Heltec V4: chat and DM transcripts now survive a reboot.** The board has no card slot, so they go to a 9.5 MB filesystem partition in its own flash, which also backs config export and import. Devices already in the field need one USB flash to gain that partition — an OTA update does not rewrite the partition table, and an OTA-only device keeps history in RAM as before. Settings, channels and the node identity survive the USB flash.
- **Heltec V4 and ThinkNode M9: the Default, Chirpy and Bass alert sounds are now three different sounds.** These boards have a piezo buzzer rather than a speaker and previously played one identical beep for all three; the new patterns are pitched for what a piezo actually projects.
- Heltec V4: an **Actions** button under the chat, sharing that strip with New Message.
- Heltec V4: a **New Message** button under the DM thread, so a conversation can be answered from the screen you read it on.
- Heltec V4: **Close** buttons on every popup that keyboard builds dismiss with Backspace — Device Info, action results, the emoji tray, traceroute progress, the New DM node picker and the system-stats screen. Where tapping outside also worked, it still does.
- ThinkNode M9: the remote VNC viewer now works on this board.

### Changed
- Heltec V4: **a tap now both selects and runs.** Tapping a Config row runs that action, tapping a node opens its node actions, tapping a node in the New DM picker starts the DM. Dragging to scroll never runs anything.
- Heltec V4: the USER button is now the Enter key's stand-in everywhere and always matches what a tap does on that screen — compose on chat, run the highlighted Config row, open a node's actions, send the highlighted emoji, mute in Channel Actions.
- Heltec V4: **Actions left the bottom nav.** It acts on the channel you are reading, so it now lives only on the chat screen; from Nodes or Live it had been silently acting on a channel that was not on screen. The nav is Config, DM, Nodes, Live, Help on every board.
- Heltec V4: on-screen hints name the button to press instead of a key that does not exist on this hardware.
- Importing a Meshtastic config now recognises the newer regions: MY_919, SG_923, PH_433, PH_868, PH_915, ANZ_433, KZ_433, KZ_863, NP_865 and BR_902.

### Fixed
- **This node's own telemetry, position, node info and neighbor info now reach the MQTT broker.** They were marked as MQTT-permitted but nothing ever published them, because a LoRa radio never hears its own transmission — chat was the only thing about this node a broker saw. All of it still respects the channel's uplink flag. Discovery sweeps are deliberately excluded, so they cannot ask every MQTT-connected node to answer at once.
- Heltec V4: holding a chat message to open Message Actions now works consistently. Normal fingertip drift during the hold was being read as a list scroll, which cancelled the long press — it appeared to work only when there were too few messages to scroll.
- Heltec V4: tapping **New DM** did nothing; the row was being rebuilt out from under the tap.
- Heltec V4: a message arriving while your finger was on the conversation list no longer cancels the gesture, including a delete hold several seconds in.
- Heltec V4: the hidden system-stats screen could only be closed from the keyboard that opened it, stranding anyone standing at the device.
