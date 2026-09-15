### New
- The charts band on the home screen is now a swipeable carousel: swipe right or left to move between the charts and two new node pages, wrapping around in both directions.
- New `RECENTLY HEARD` page lists the nodes heard from most recently, newest first, with how long ago each was heard (`45s`, `12m`, `3h`, `2d`).
- New `LONGEST SILENT` page lists the nodes that have been quiet the longest, oldest first.
- The carousel also turns with the hardware controls — the T-Lora Pager's rotary wheel and left/right keys, the ThinkNode M9 and Attaky Mesh Deck d-pads, the T-Deck trackball, and the Cardputer arrow keys.
- Wide screens show both node lists on a single page, side by side, so twice as many nodes fit at once — T-Deck, Mesh Deck, M9, Wio Tracker L2 and the Heltec boards in landscape; the T-Deck Pro and any board in portrait give each list its own page.
- Small `<` and `>` markers at the edges of the band show that it turns.
- The page you were last looking at is remembered until the device restarts, so pressing Home brings you back to it instead of always to the charts.
- The number of node rows adapts to the screen, so taller panels such as the Pager show several more nodes than a portrait T-Deck Pro.

### Changed
- Only nodes actually heard from since boot appear in the node lists — entries restored from flash at startup stay out until a packet arrives, and a page with nothing to show says "Nothing heard yet".
- Pages slide in the direction you swiped on all boards except the T-Deck Pro, which swaps instantly because every animated frame there is a full e-paper refresh.

### Fixed
- On the T-Lora Pager, turning the wheel on the home screen quietly scrolled the chat screen hidden underneath; it now turns the carousel instead.
