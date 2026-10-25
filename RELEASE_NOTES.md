### New
- Web config has a new layout. On wide screens a side rail lists the pages, with Config's sections nested under it. On phones a bottom tab bar holds Config, Live, Chat, Nodes and More, and a "Sections" sheet lets you jump around Config. This applies to all boards except the Cardputer.
- Web config now uses the colours of the theme active on the device, including presets and custom themes. On e-ink boards the page is black on white. A light device theme gives a light web page. This applies to all boards except the Cardputer.
- Boards with PSRAM now serve the full web config page on the device's own access point, instead of web config lite. The page includes the quick WiFi setup form, and a note explains that the node map, map downloads and update checks only work once the device is on your WiFi.
- Changed fields in web config are now outlined, and a save bar counts your unsaved changes. It offers **Discard** and **Save and restart**, and the browser warns you before you leave the page with unsaved changes. This applies to all boards except the Cardputer.
- Web config remembers which tab and section you were on, including after the restart that follows a save.
- On phones, the device's public key can be copied from the More sheet.

### Changed
- Saving from the full web config page now needs JavaScript, because it is done from the save bar. The Save All button at the bottom of the page is gone. Web config lite keeps its plain Save button.
- All Config sections in web config now stay open on one page, instead of folding away, and the side rail highlights the section you are viewing.
- Buttons, text and fonts in web config now look the same on every tab, and destructive actions such as reboot, clearing data and factory reset are shown in red.
- The web config login page uses the new look, as a centred column.
- Pages in web config load faster after the first visit, because the browser caches the stylesheet. After a firmware update the browser always gets the new stylesheet.
- On boards without PSRAM, and on the Cardputer, the device's own access point still serves web config lite.

### Fixed
- If a custom theme's text colour is hard to read against its background, web config now uses Camillia's standard text colours instead. Status colours are also adjusted so they stay readable on light themes.
- The public key chip in the web config header now shows its intended text colour.

Pull request: #127
