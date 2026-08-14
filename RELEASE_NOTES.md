### New
- Build your own theme in Web Config: give it a name, pick its background, panel, panel alt and accent colors, choose Light or Dark, and save it. It shows up in the theme grid and in the on-device Theme picker alongside the built-ins.
- Up to 4 custom themes can be stored. Each saved theme's card has a pencil button to edit it and a red button to delete it; editing the theme you are currently wearing repaints the device as soon as you save.
- Custom themes can be moved between devices as a short share code — copy it out of a config backup or export, paste it into the builder's Import code field, and click Load to fill the form before saving.
- Custom themes are written into the SD config backup (`/camillia/config.yaml`), so they ride along with a config export and come back on a restore, into the same slots.
- The on-device Theme picker can be filtered: press Space, then type to narrow the list to themes whose names contain what you typed. The line under the title shows the filter and how many themes match; Backspace edits it, and an empty filter closes it.
- Web Config's theme grid has a matching Filter themes box with a count of visible cards; pressing Enter with a single match selects that theme.

### Changed
- Changing the font size in Web Config no longer reboots the device — chat and DMs re-render at the new size immediately. Any other setting still reboots as before.
- The on-device Theme picker wraps around: moving up from the first theme lands on the last, and down from the last returns to the first.
- T-Lora Pager TFT: scrolling chat with the wheel or j/k now moves one line of text per step instead of jumping from message to message, so a long message can be read all the way through. Enter still enters cursor mode for picking a reply target.

### Fixed
- Cardputer and T-Lora Pager TFT: the four chat font sizes are now actually distinct. Small and Medium previously rendered identically, and Extra Large was still small on these panels.
