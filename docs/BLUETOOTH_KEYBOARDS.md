# Bluetooth Keyboards

> **Status: not implemented.** No Camillia build can pair a Bluetooth keyboard today.
> This document exists so that anyone buying hardware in anticipation buys something
> that *can* work. Design and progress are tracked in
> [issue #40](https://github.com/oumike/camillia-mt/issues/40).

## The one rule

**The keyboard must be a Bluetooth *Low Energy* (BLE) keyboard.** A Bluetooth
Classic keyboard cannot be made to work by any firmware change.

Every board Camillia supports — T-Deck, T-LoRa Pager, Cardputer, Heltec V3/V4,
Attaky Mesh Deck, Elecrow ThinkNode M9 — is built on an **ESP32-S3**, and the S3
has no Bluetooth Classic (BR/EDR) radio at all. Only the original ESP32 in
Espressif's lineup has one. BLE and Classic are separate protocols on shared
spectrum: a BLE-only device and a Classic-only device
[cannot communicate](https://www.ezurio.com/resources/blog/bluetooth-low-energy-vs-bluetooth-classic-what-s-the-difference),
and no amount of software bridges them.

So the question for any keyboard is only ever: *is it BLE?*

## Why the box won't tell you

Both protocols are sold as "Bluetooth," and manufacturers of finished keyboards
almost never publish which one they use. While writing this document we checked
Logitech's own product pages and support articles for the K380, K480 and K780:
none of them state "Low Energy," "BLE," or "Bluetooth Smart" anywhere. They just
say "Bluetooth."

Weak signals, in rough order of usefulness:

| Signal | What it means |
| --- | --- |
| "Bluetooth 3.0" or older, or sold before ~2014 | **Classic. Will not work.** BLE did not exist before Bluetooth 4.0. |
| "Bluetooth 4.0 / 4.2 / 5.x" | *Can* be BLE — but 4.0+ chips may still run Classic HID. Not a guarantee. |
| Multi-year battery life on a coin cell or AAAs | Strong hint at BLE; a Classic radio cannot idle that cheaply. |
| "Bluetooth Smart" / "BLE" printed anywhere | Reliable — this is the old marketing name for BLE. |
| Requires a USB dongle | **Not Bluetooth at all.** Logitech Unifying and similar are proprietary 2.4 GHz. Irrelevant here even if the same keyboard also has a Bluetooth mode. |

## How to test a keyboard you already own

This is definitive, takes two minutes, and needs no Camillia hardware. A BLE
keyboard identifies itself by advertising **HID Service UUID `0x1812`** (HID over
GATT, "HOGP"), usually alongside appearance code `0x03C1` (Keyboard).

1. Install a BLE scanner on a phone — **nRF Connect** (Nordic) or **LightBlue** both work.
2. Put the keyboard into pairing mode (usually holding a Bluetooth or channel key until an LED flashes).
3. Scan, and look for the keyboard by name.

Reading the result:

- **Appears, and lists service `0x1812` / "Human Interface Device"** → it is a BLE HID keyboard. This is what we need.
- **Appears, but advertises no HID service** → uncertain; some keyboards advertise HID only while actively pairing. Rescan during pairing mode before concluding anything.
- **Never appears in a BLE scan at all, though it pairs fine with a laptop** → it is Classic-only. It will never work with Camillia.

That last case is the one worth knowing before you spend money.

## Keyboards by confidence

These tiers are about *how well established the BLE support is*, not about
keyboard quality. Nothing here has been tested against Camillia, because the
feature does not exist yet.

### Tier 1 — Certain, by silicon

Keyboards built on **Nordic nRF52840 / nRF52832** controllers. Those chips have no
Classic radio, so BLE is not a marketing claim but the only thing they can do.

- Anything running **[ZMK firmware](https://zmk.dev/docs/hardware)** — the mainstream wireless custom/split keyboard ecosystem.
- Controller boards: **nice!nano v2**, **Seeed XIAO nRF52840 (XIAO BLE)**, **BlueMicro840**, **SuperMini nRF52840**.
- Commercial ZMK/nRF52 keyboards from the ergo and split world.

There is direct third-party evidence for this class: the
[esp32c3 HID proxy project](https://github.com/anisehid/hid-proxy-for-ble-keyboard)
is an ESP32 acting as a BLE HID host, and it was developed and tested against
ZMK keyboards specifically.

The tradeoff is that these are enthusiast products — often kits, often split,
rarely cheap. For a field-carried mesh handheld they may be less practical than
a folding travel keyboard.

### Tier 2 — Confirmed working with an ESP32 BLE HID host

- **Microsoft Designer Compact Keyboard** — appears in Espressif's own
  `esp_hid_host` example output, enumerated as UUID `0x1812` with keyboard
  appearance `0x03C1`. That is an ESP32 successfully discovering it as a BLE HID
  device, which is the exact operation Camillia would perform.

Microsoft's recent Bluetooth keyboards generally target BLE, so others in that
line are plausible — but only the Designer Compact has published evidence behind it.

### Tier 3 — Probably BLE, unverified

Reasonable bets that we could not confirm from vendor documentation. **Test with
a BLE scanner before relying on any of these.**

- **Logitech K380 / K480 / K780 / MX Keys** — the multi-device, multi-year-battery
  class. The power budget strongly implies BLE, and these are among the most
  widely owned Bluetooth keyboards. Logitech does not state it either way.
- **Modern folding travel keyboards** advertising Bluetooth 5.x — Targus KeyFold,
  iClever BK09, ProtoArc, Nillkin Cube Pocket and similar. The form factor suits a
  handheld mesh device better than anything else on this page, but these are
  mostly rebadged designs whose internals vary between production runs, so the
  brand name is a weak guarantee. Scan before trusting.

### Tier 4 — Known problems

- **Keychron, non-Pro models.** The obvious brand to reach for, and a trap. Many
  models use Broadcom's **BCM20730**, a 2011-era chipset
  [reported to be incapable of BLE](https://deskthority.net/viewtopic.php?t=28411)
  — Classic-only, permanently incompatible. Their newer **Pro** line (K8 Pro and
  similar) does support a BLE mode. With this brand the specific model decides,
  not the badge.
- **Any keyboard from roughly 2010–2014**, or marked Bluetooth 3.0 or lower.
  Predates BLE entirely.
- **Apple Magic Keyboard** — deliberately unrated. We could not establish from
  published sources whether it presents as BLE HID to a non-Apple host, and Apple
  peripherals carry extra pairing behavior. Scan it yourself before assuming.

## Even a BLE keyboard may not work on day one

Advertising `0x1812` means "worth trying," not "guaranteed." The planned first
implementation uses HID **Boot Protocol** — it asks the keyboard to switch to a
fixed 8-byte report format, which avoids parsing each keyboard's custom report
descriptor.

Boot protocol is mandatory only for devices that claim the boot keyboard role.
A keyboard can be entirely legitimate BLE HID and still not offer it, in which
case it needs the report-protocol fallback, planned as a later phase. So expect
some BLE keyboards to work immediately, and some to need additional firmware work.

Two other things to expect when pairing:

- **Passkey entry.** Many BLE keyboards require a 6-digit passkey to be typed *on
  the keyboard* to complete pairing. That is normal and will be part of the pairing screen.
- **Media and function keys.** Boot protocol exposes only the standard keyboard.
  Volume keys, media controls and trackpads on combo devices will not pass through initially.

## Practical notes for a handheld

- **Power.** A connected BLE keyboard means the BLE stack stays resident, costing
  roughly 30–40 KB of internal RAM plus radio time shared with Wi-Fi. Expect the
  feature to be off by default and switchable, rather than always on.
- **Multi-device keyboards** with channel keys (the Logitech and Keychron style)
  are convenient here: one channel for your laptop, one for the mesh device.
- **LoRa is unaffected.** It is a separate radio on its own SPI bus; only Wi-Fi
  shares spectrum with Bluetooth.

## Sources

- [Espressif FAQ — Classic Bluetooth support by chip](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/bt/br-edr.html)
- [Ezurio — BLE vs Bluetooth Classic](https://www.ezurio.com/resources/blog/bluetooth-low-energy-vs-bluetooth-classic-what-s-the-difference)
- [ZMK Firmware — supported hardware](https://zmk.dev/docs/hardware)
- [anisehid/hid-proxy-for-ble-keyboard — ESP32-C3 BLE HID host tested with ZMK keyboards](https://github.com/anisehid/hid-proxy-for-ble-keyboard)
- [Espressif — Bluetooth HID Host API (`esp_hidh`)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/esp_hidh.html)
- [deskthority — Keychron BLE module discussion](https://deskthority.net/viewtopic.php?t=28411)
- [Silicon Labs — BLE HID keyboard, boot protocol characteristics](https://docs.silabs.com/bluetooth/2.13/bluetooth-code-examples-applications/ble-hid-keyboard)
