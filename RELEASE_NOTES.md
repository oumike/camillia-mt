### New
- **Tanmatsu:** Each nav bar button now shows a small coloured shape matching its function key: red ✕ Home (F1), orange △ Chats (F2), yellow □ DMs (F3), green ○ Nodes (F4), blue ♣ Tools (F5) and purple ◇ Config (F6). The shape sits beside the icon in the bottom bar and under it in the side column.

### Changed
- **Tanmatsu:** The message box for channels and DMs now has two lines that wrap, and the popup grows to fit, so you can read your whole message as you type it. Enter still sends.

### Fixed
- **Elecrow ThinkNode M9:** On the Config section grid, Up and Down now move up or down a row instead of left or right. Left and Right move one button.
- When you leave a channel, it is now marked read straight away. Before, a channel that got a new message while you were in it stayed marked unread if you switched away within 15 seconds. This applies to all boards.

Pull request: #145


### Update (v6.4.2)
### Fixed
- The Chats alert in the nav bar (and the bell beside the envelope on boards without a nav bar) no longer keeps blinking for the channel you have open. If messages arrive while the screen is asleep or you're on the lock screen or Home dashboard, opening that channel stops the blink right away. Unread messages on other channels still make it blink. All boards.

Pull request: #146
