### New
- T-Display P4: New messages now vibrate. Channel messages buzz once and DMs buzz twice. Choose the style in Config → Notifications → Vibration: Off, Pulse, Gentle, Long or Heartbeat. Each step plays a sample buzz. Vibration works even when Notification Sound is Off.
- T-Display P4: Vibration is **on by default** (Pulse) after updating. Set it to Off in Config → Notifications → Vibration to turn it off.
- T-Display P4: You can also set the vibration style in the web config's Notifications section, or with `vibration:` in a YAML config backup (`off`, `pulse`, `gentle`, `long` or `heartbeat`).
- T-Display P4: Your device gives a short tap when one of your channel or direct messages is first acknowledged. Later acknowledgements for the same message don't tap. The tap is off when Vibration is Off.
- T-Display P4: The new Vibration settings text (Vibration, Pulse, Gentle, Heartbeat) is not translated yet and shows in English.

### Changed
- T-Display P4: In the Config settings grid, Up/Down now move one row and Left/Right move one button. Before, Up/Down stepped sideways through the buttons and Left/Right did nothing.

Pull request: #139
