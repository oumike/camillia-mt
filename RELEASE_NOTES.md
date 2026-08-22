### New
- **Locate**: highlight a node, press **A** for its actions menu, and choose **(L)ocate** to see its last reported position pinned on a map of the US state it falls in, along with the state name and coordinates — no Wi-Fi, no phone, radio off. Nodes that have never reported a position show the row greyed out rather than hiding it. Not available on the Cardputer.
- **Web Config → Utilities → Maps Download** puts those maps on the card. The device has no way to download them itself, so the browser fetches the tiles and uploads the finished image; only the states your known nodes are actually in get downloaded, progress is reported per state and per tile, and each map is saved as it finishes so stopping partway keeps what's done. Requires the device joined to your Wi-Fi (not its own access point) and a card inserted — the section is hidden otherwise.
- **Web Config → Utilities → Danger Zone → Clear Maps** deletes every saved state map and reports how many it removed. No node data, messages, config, or other files on the card are touched, and re-running Maps Download puts them back.
- A node positioned outside the United States still opens Locate and shows its coordinates, with a note that there's no regional map for it.
- New [Maps guide](docs/MAPS.md) covering how maps get onto the device, where the files live, and what to check when one is missing.

### Changed
- State maps are now saved at 384x256 instead of 240x160, so the Locate map is sharper on larger panels. Maps already on the card keep working and are listed for refresh in Maps Download rather than having to be cleared first.
- The Nodes screen key hint now matches the keys that actually work and spells out Backspace as Edit/Exit.

### Fixed
- Emoji and other text could silently fail to draw on busy screens when the interface ran out of working memory. The pool is much larger now — 2 MB on T-Deck, T-Lora Pager TFT, ThinkNode M9 and Mesh Deck, 768 KB on Heltec V4.
