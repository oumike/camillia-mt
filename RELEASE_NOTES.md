### New
- Config now has a **Preset** row that changes the device's modem preset on the device itself — no more needing a second machine on the same network to use the web form.
- The preset picker lists each preset with its on-air channel name, spreading factor and bandwidth, marks the one currently in use, and warns that only nodes on the same preset can hear each other. Choosing a preset applies it and reboots; picking the one already in use just closes the picker.
- Presets the radio cannot actually run are left out of the list, so a device can't be set to a configuration that would silently leave it off the air.
- The Config row shows the preset in force at a glance, and reads "Custom" when custom bandwidth/spreading-factor/coding-rate settings are active. Picking a preset from the list is also how to leave those custom settings behind.
- Keyboard devices navigate the picker with up/down, Enter to apply and the usual close key to cancel; touch-only devices tap a preset to apply and tap outside to cancel.
