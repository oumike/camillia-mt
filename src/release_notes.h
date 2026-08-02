// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- Optional GPS power saving: the receiver can park in standby between position reads instead of tracking continuously, cutting idle drain on screen-off devices. Off by default; enable it in settings (`gpsDutyCycle`).
- Web config now shuts itself off automatically after 10 minutes with no activity, so leaving it on no longer quietly drains the battery. First-time setup is exempt, and the timeout is adjustable.
- LoRa RX boosted gain is now a setting (`loraRxBoostedGain`): leave it on for maximum range, or turn it off to trade a little sensitivity for battery.
- The CPU slows down while the screen is off, and the backlight dims for the last few seconds before the screen sleeps as a warning.

Changed
- Lower default screen brightness on Heltec V4 and Cardputer to save battery; you can raise it again in settings.
- Turning GPS off now fully powers the module down instead of leaving it drawing current with nobody listening; a device booted with GPS disabled no longer keeps it running all session.
- Wi-Fi now backs off retries up to 5 minutes when it can't reach a saved network, instead of retrying every 10 seconds forever; it reconnects promptly once back in range.
- T-Deck: only the trackball click wakes the screen now - the keyboard and touch panel no longer wake it from inside a pocket or bag.
- Pager: the unused haptic and NFC rails are no longer powered, saving current the whole time the device is on.
- Web config: the GPS Poll and Broadcast interval fields now sit together with a clearer explanation of the difference.

Fixed
- Configured LoRa transmit power is now actually applied; previously some setups could transmit at full power regardless of the configured level.
- RX boosted gain no longer comes back disabled on devices with saved settings, restoring the intended receive range.
- The screen now truly sleeps the display panel instead of just blanking the backlight, saving power and preventing a brief stale image when it wakes.)CAMNOTES";
