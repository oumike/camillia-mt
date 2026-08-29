### New
- Node Actions menu gained a **Delete** entry (shortcut `E`) that forgets a node's record from the node list after a confirmation prompt; the row is greyed out when the node isn't in the table. Deleting is not the same as Ignore — the node reappears the next time it's heard.

### Changed
- The web chat tab is noticeably lighter on the browser: the message feed is only redrawn when new messages actually arrive instead of every two seconds.
- The recipient dropdown in web chat no longer rebuilds itself on every refresh, so it stays open and keeps its scroll position while you're picking a target on a phone.

### Fixed
- The web config page no longer fails to load or hangs while the device is transmitting. Scheduled NodeInfo, telemetry and neighbour announces are held back while a browser is actively using the page (up to two minutes), so a page load doesn't land in the middle of seconds of blocking radio airtime.
- Messages sent from the web chat no longer appear two or three times in the feed, or get glued together into one repeated bubble.
- Switching conversations in web chat no longer splices the previous conversation's messages into the new one.
- The web server responds faster overall: a per-loop idle delay was removed, cutting latency before an incoming request is noticed.
- Safari's automatic touch-icon requests are answered cleanly and cached instead of generating four errors in the serial log on every page load.
- The serial log now says when a browser hangs up mid-response and reports how long the main loop was busy, making a stalled web page diagnosable instead of silent.
