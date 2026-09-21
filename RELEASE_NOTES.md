### New
- The bottom nav bar is now available on the keyboard boards with no touch panel — **Cardputer**, **ThinkNode M9** and **T-Lora Pager**. It is off after a fresh install there, so those boards look exactly as they did, and Config → **Nav Bar** (**Bottom Nav Bar** in web config) turns it on immediately, with no reboot.
- **Screen Timeout** gains **30 min** and **60 min** stops, between 10 min and Never, for a device sitting on a bench rather than riding in a pocket — bear in mind an hour of lit backlight costs real battery.
- A full Remote Administration Terminal guide ships with the firmware docs (`docs/ADMIN_TERMINAL.md`, linked from the README): how a remote node is granted admin, how peers are discovered, the command table, and what each error line means.

### Changed
- Unread marks now follow the Nav Bar setting on every board with a keyboard: switch the bar on and the alert moves onto the **Chats** and **DM** cells instead of staying in the footer corner.
- On the **M9** and **T-Lora Pager**, switching the bar on replaces the key-hint strip, and with it the dashboard's scrolling notification ticker — the hints and the ticker share that row.
- On the **M9** and **T-Lora Pager** the nav bar's cells are clickable from the browser Remote, even though neither device has a touch panel.
- **Cardputer**, **M9** and **T-Lora Pager** upgrading from an earlier release come up with the key-hint strip they already had; if you had set `navBar: true` in an imported config on one of those boards, switch the bar back on once after the update.

### Fixed
- A screen timeout set from web config or imported from YAML is no longer shortened by opening the on-device slider — a stored 30 minutes opened on **10 min** and was saved back as 10, even if you had only come to look.


### Update (v5.3.1)
### New
- The bottom nav bar is now available on the keyboard boards with no touch panel — **Cardputer**, **ThinkNode M9** and **T-Lora Pager**. It is off after a fresh install there, so those boards look exactly as they did; Config → **Nav Bar** (**Bottom Nav Bar** in web config) turns it on immediately, with no reboot.
- **Screen Timeout** gains **30 min** and **60 min** stops between 10 min and Never, for a device sitting on a bench rather than riding in a pocket — bear in mind an hour of lit backlight costs real battery.
- A full Remote Administration Terminal guide ships with the firmware docs (`docs/ADMIN_TERMINAL.md`, linked from the README): how a remote node is granted admin, how peers are discovered, the command table, and what each error line means.

### Changed
- Unread marks now follow the Nav Bar setting on every board with a keyboard: switch the bar on and the alert moves onto the **Chats** and **DM** cells instead of staying in the footer corner.
- On the **M9** and **T-Lora Pager**, switching the bar on replaces the key-hint strip, and with it the dashboard's scrolling notification ticker — the hints and the ticker share that row. The **Help** screen still lists every shortcut.
- On the **M9** and **T-Lora Pager** the nav bar's cells are clickable from the browser Remote, even though neither device has a touch panel.
- **Cardputer**, **M9** and **T-Lora Pager** upgrading from an earlier release come up with the key-hint strip they already had; if you had set `navBar: true` in an imported config on one of those boards, switch the bar back on once after the update.

### Fixed
- A screen timeout set from web config or imported from YAML is no longer shortened by opening the on-device slider — a stored 30 minutes opened on **10 min** and was saved back as 10, even if you had only come to look.
