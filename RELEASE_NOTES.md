### Changed
- CrowPanel Advance 3.5: in portrait, the on-screen keyboard is now a fixed band along the bottom of the screen instead of filling most of it. The message composer sits right on top of it with a four-line wrapping message box, so the conversation stays visible above. The Wi-Fi password, node name, channel name, node search, onboarding and admin screens use the same keyboard. Landscape is unchanged.
- CrowPanel Advance 3.5: Tools now fills the screen between the header and the nav bar in both orientations, with larger buttons and text.
- CrowPanel Advance 3.5: in portrait, Home and the lock screen show all four widgets at once (channel utilisation chart, SNR/RSSI chart, recently heard nodes and longest silent nodes), two across, instead of a rotating carousel. The lock screen shows its recent messages above them. Landscape keeps the carousel.
- T-Display P4: the node map and Locate now open full screen. The title appears on the map, the close button sits in the top-right corner with the zoom controls below it, and Esc/Back still closes the map.
- T-Display P4 with the keyboard attached: Up and Down now move the cursor from the first node in the Nodes list up to Search, All and Map, and back down. The cursor only appears while the keyboard is attached.

### Fixed
- Nodes: the arrow keys no longer stop working when the list is empty. With no favorites, the cursor now goes to **All**. When a search finds nothing, it goes to **Clear search**. Enter presses the selected button. This applies to all boards with a keyboard.

Pull request: #117
