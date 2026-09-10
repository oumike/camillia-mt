### New
- Heltec V4 and V4-R8: screen orientation is now a setting instead of a separate firmware — Config → **Orientation** switches between landscape and portrait, asks to confirm, then reboots to apply it. New devices start in landscape.
- Heltec V4 and V4-R8: orientation can also be set from the web config page, and is included in exported/imported YAML config files.
- Heltec V4, V4-R8 and other touch-only boards: an unread direct message now blinks the **DM** button in the bottom nav bar amber, so the alert sits on the button that opens it.

### Changed
- Heltec V4 and V4-R8 ship a single firmware image each; there are no longer separate vertical downloads. **Devices currently running the old vertical firmware will not update over the air** — their update check looks for an asset that is no longer published. Reflash once over USB to rejoin the normal update path.

### Fixed
- Heltec V4: internal storage failed to mount on every boot, which silently broke chat transcripts, config export/import, saved map tiles and Discovery snapshots. Existing devices keep the old layout until they are reflashed over USB.
- Heltec V4-R8: the update check asked for Heltec V4 firmware, built for a different mainboard and carrier. It now asks for the correct V4-R8 images.
- Lock screen: message previews containing curly apostrophes and similar characters no longer show empty boxes, and long lines now wrap at the right place.
