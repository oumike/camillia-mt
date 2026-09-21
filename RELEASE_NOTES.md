### New
- **Keyboard Light** — a new Config and web config setting with Low, Medium, High and Off. On the **T-Deck** it sets the keyboard backlight's resting brightness, which until now was always dark except for a notification blink; on the **T-Deck Pro** it sets how bright **Alt+B** makes the keyboard, with the toggle still deciding lit or dark; on the **ThinkNode M9** it sets how brightly the keypad lights itself after a keypress, and **Off** there stops it lighting at all until the device is next power-cycled.
- **An emoji button beside the message box** — tap it on the **T-Deck**, **T-Deck Pro** and **Mesh Deck**, or on the **T-Lora Pager** roll the wheel past the last character of your message and press Enter.
- **T-Deck: the microphone key opens the emoji tray** while composing, with the glyph landing at the cursor. Nothing on that board records audio, so the key did nothing at all before.
- **ThinkNode M9: Ctrl opens the emoji tray** while composing. The key did nothing before.
- **Mesh Deck: the far-left key of the bottom row opens the emoji tray** while composing — it had no meaning before. Pressing the **symbol key** a second time does the same, swapping the symbol tray for emoji; a third press closes it.
- **P opens Help** on every keyboard build, with **Alt+P** as the chord that reaches it from anywhere, the **T-Deck Pro** included.

### Changed
- **Tools** and **Help** are full screens rather than popups, each carrying the bottom nav bar with its own cell lit and every other cell one tap away. Help now replaces the screen you were on instead of floating over it.
- Picking an emoji leaves the tray open so you can pick several, and a brief **😀 added** appears at the foot of the tray to confirm the glyph went into the message behind it.
- On the **T-Deck**, a notification blink now pulses away from your Keyboard Light level and settles back on it, instead of leaving the keyboard dark.
- On the boards with a home dashboard, the compose key no longer opens a message box from the dashboard — messages start from the chat screen, where you can see which conversation you are in.

### Fixed
- Closing the **Weather** screen could crash and reboot the device a moment later, on any board showing the bottom nav bar.
- On the **Cardputer**, **Alt+H** — that board's only "close everything and get back to chat" gesture — did nothing at all.
- Tapping a nav-bar cell while **Tools** was open closed Tools instead of going where the cell pointed.
- The **Help** key list was missing **A** (Channel Actions) entirely, and on the **Cardputer** it listed **H** as Help when H is the channel selector there.
- On the **Cardputer**, Help said "Backspace to close" when only **Esc** closes anything on that board.
- The **Help** key list is no longer squeezed into a fixed-height card, and the **Cardputer** no longer needs a scrollbar to reach the end of it.
- On the **T-Deck Pro**, the serial console was flooded with panel timing lines on every screen refresh.
