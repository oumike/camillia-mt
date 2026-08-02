### New
- Web Config's LoRa section now has a **Custom** modem preset with editable Bandwidth (62.5–500 kHz, plus 31.25 kHz on radios that support it), Spreading Factor (SF7–SF12), Coding Rate (4/5–4/8), and Frequency Slot; every node in your mesh must match all four to talk.
- Frequency Slot `0` derives the frequency from your primary channel's name like a preset does, while any other value pins a specific slot, and the Web Config readout shows the resulting frequency and how many slots your region has at that bandwidth.
- Custom modem settings import and export directly with `meshtastic --export-config` YAML dumps, under `config.lora` as `usePreset`, `bandwidth`, `spreadFactor`, `codingRate`, and `channelNum`.

### Changed
- On the T-Lora Pager TFT's LR1121 variant, 31.25 kHz bandwidth is omitted from the custom settings since that radio cannot produce it.
- An unnamed primary channel is shown as `Custom` while custom settings are active, and switching back to a preset restores the preset's channel name (a channel you renamed yourself is left alone).
- The device info screen shows the preset name (or "Custom") and now displays fractional bandwidths like 62.5 and 31.25 kHz correctly instead of rounding them to whole numbers.
