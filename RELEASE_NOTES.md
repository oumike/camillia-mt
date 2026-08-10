### New
- Light Timeout (T-Deck, T-Lora Pager, Mesh Deck): the notification LED or keyboard-backlight blink can be told to stop reminding after 30 sec, 1 min, 5 min or 30 min instead of repeating until the message is read. Each new message restarts the clock, reading the message still stops the light immediately, and a blink already in progress always finishes rather than being cut off. Defaults to Never, so an update changes nothing until a timeout is picked.
- Light Timeout is on the Config screen next to the other notification settings and under Notifications in web config, and it travels with exported and imported config files.
- The Config screen can be filtered: press Space to arm the filter and type to narrow the rows, the same way the Nodes screen works. The header shows what was typed and how many rows match, Backspace edits it and disarms once empty, and the filter clears when Config is closed. Keyboard builds only.

### Changed
- Location Precision and Light Timeout now open a slider whose stops are the available values, instead of a row that cycles through them one press at a time. Nothing is applied until saved, and Backspace/Esc cancels — on the Heltec, drag the slider and press Save.
- On the T-Deck and T-Lora Pager, the reminder window for a message that arrives while the screen is awake is now measured from when the message arrived, so if the screen sleeps after the timeout has expired the keyboard backlight stays dark instead of starting to blink hours later. Only applies once a Light Timeout is set.

### Fixed
- Moving up and down the Config screen was sluggish, worst on the T-Lora Pager, because every keypress rebuilt all the rows and, on the Pager, also refilled the info panel from the node list. The selection now just repaints the two rows whose highlight changed.
- Web config chat showed wrapped messages back to front: a long message started with its last line and ended with its first, with the timestamp stranded in the middle. Multi-line messages sent from the device also lost their "mine" styling and were wrongly offered Reply and tapback buttons.
