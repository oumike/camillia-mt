### New
- Channel messages you send now show a separate "ME (ACK)" tag when a node actually confirms delivery, so a real confirmation is no longer displayed the same way as a message that just timed out waiting.

### Changed
- Direct messages now wait a full 60 seconds (up from 30) before being marked failed, which matches how long a peer keeps retrying on a busy mesh and stops good messages from being written off early.

### Fixed
- Delivery confirmations for channel messages are no longer thrown away — the firmware keeps listening for up to 15 seconds after sending, long enough for an ACK to make the round trip on LongFast.
- Direct messages to a node that never answers now settle into a failed state instead of sitting at "ME" indefinitely, where they looked identical to a message still in flight.
- Sending several channel messages in quick succession no longer leaves the newest ones stuck at "ME" with no status ever appearing.
- Curly double quotes, the single-character ellipsis, non-breaking and other unusual spaces, and invisible formatting marks now display as plain text instead of empty boxes — messages typed on a phone or pasted from a desktop read correctly.
