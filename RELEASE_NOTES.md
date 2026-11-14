### New
- Replies in channel chat now show a one-line quote above them, with a colour bar, the original sender's name and a short snippet of the message being answered. If the original has scrolled out of history, the quote reads "earlier message". This works in every chat style: Classic, IRC, Bubbles and Outline.
- On M5Stack Cardputer, Heltec V4 (expansion), Heltec R8 and Elecrow ThinkNode M9, a reply shows a compact `↪@Name` tag after the sender instead of a quote line, to save screen space.
- The web UI chat now shows reply quotes and reactions too. Tapping a quote scrolls to the original message and briefly highlights it.
- Reply links are saved with chat history, so quotes and reactions are still there after a reboot. Older saved history still loads.

### Changed
- In channel chat, emoji reactions no longer appear as their own chat lines. They are grouped under the message they react to, with a count when the same emoji is used more than once (e.g. `👍 2 ❤️`). This applies in every chat style.
- An incoming reaction still counts as unread and alerts like a message.

Pull request: #152
