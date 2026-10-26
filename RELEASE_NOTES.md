### New
- Your device now asks nearby unnamed nodes for their name, so fewer nodes appear as `!xxxxxxxx`. It sends a short NodeInfo request to a node within 3 hops that has no name yet. It asks each node at most once every 30 minutes, sends at most one request every 30 seconds, and stops asking when channel utilization is above 25%. All boards.
- T-Deck: while you type a message, the trackball moves the text cursor. Roll left or right to move one character, so you can insert or delete text in the middle. Roll up or down to move one line when the message wraps. This works with Invert Scroll on.

### Changed
- When the node table is full, the node heard least recently is now removed first, whether it has a name or not. New nodes keep coming in instead of pushing each other out before their names arrive. Old named nodes no longer stay forever. They go to the SD archive if it is turned on. Favorites are never removed, so favorite any node you want to keep. All boards.
- Clear Nodes (Keep Favorites) and Clear Nodes (All) are now one **Clear Nodes** action. Its confirmation has two options, both unticked each time it opens: **Include favorites**, and **Include archived (SD card)** on boards with an SD slot. On keyboard boards, press F and A to toggle them. On touch boards, tap them. The status message says whether the SD archive was cleared or that no card was found. Web Config's Danger Zone has the same single form with the same two options. All boards.
- Clear Nodes (All) used to delete the SD node archive too. To delete the archive now, tick **Include archived**.

Pull request: #128


### Update (v6.2.1)
### New
- Your device now asks nearby unnamed nodes for their name, so fewer nodes show up as `!xxxxxxxx`. It asks a node only if it is within 3 hops, asks each node at most once every 30 minutes, sends at most one request every 30 seconds, and stops asking when channel utilization is above 25%. All boards.
- T-Deck: while you type a message, the trackball moves the text cursor. Roll left or right to move one character, so you can insert or delete text in the middle of a message. Roll up or down to move one line when the message wraps. This also works with Invert Scroll on.

### Changed
- Mesh Deck: with the keyboard module attached, the message box now fills the screen between the title and the buttons, instead of showing a single line with empty space below it. Long messages wrap and stay visible while you type, and the D-pad moves the cursor up and down through them. Without the keyboard module, the on-screen keyboard still takes the lower half of the screen.
- When the node table is full, the node heard least recently is now removed first, whether it has a name or not. New nodes keep coming in instead of pushing each other out before their names arrive, and old named nodes no longer stay forever. Removed nodes go to the SD archive if it is turned on. Favorites are never removed, so favorite any node you want to keep. All boards.
- Clear Nodes (Keep Favorites) and Clear Nodes (All) are now one **Clear Nodes** action. Its confirmation has two options, both unticked each time it opens: **Include favorites**, and **Include archived (SD card)** on boards with an SD slot. On keyboard boards, press F and A to toggle them; on touch boards, tap them. The status message says whether the SD archive was cleared or that no card was found. Web config's Danger Zone has the same single form with the same two options. All boards.
- Clear Nodes (All) used to delete the SD node archive too. To delete the archive now, tick **Include archived**.

Pull request: #129
