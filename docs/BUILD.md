# Build and Flash

Two ways to get Camillia onto a device:

1. **[Flash a release](#1-flash-a-release-easiest)** — download a `.bin` from the
   Releases page and write it to the board. No build tools, no source code.
2. **[Build from source](#2-build-from-source)** — install PlatformIO and compile
   it yourself.

Before either one, check whether your board needs a
**[USB driver](#usb-drivers)** — the ThinkNode M9 does, and on macOS it will not
show up at all without it.

---

## Before you start

You need:

- A **USB data cable**. Many cheap cables are charge-only and will look like a
  dead board.
- **Python 3** (for `esptool`, and for PlatformIO if you build from source).
- The right **USB driver** for your board — see [USB drivers](#usb-drivers).

### Find your device's port

Plug the board in, then run one of these. The port name is what you pass to
every flash and monitor command below.

| OS | Command | Looks like |
|---|---|---|
| Windows (PowerShell) | `[System.IO.Ports.SerialPort]::GetPortNames()` | `COM3` |
| macOS | `ls /dev/cu.*` | `/dev/cu.usbmodem1101` or `/dev/cu.wchusbserial110` |
| Linux | `ls /dev/tty{USB,ACM}*` | `/dev/ttyACM0` or `/dev/ttyUSB0` |

If you have PlatformIO installed, `pio device list` works the same on all three.

Most boards use the ESP32-S3's built-in USB and appear as `usbmodem` / `ttyACM`.
The **ThinkNode M9** and **Attaky Mesh Deck** talk through a separate USB-to-serial
chip and appear as `wchusbserial` / `usbserial` / `ttyUSB` instead.

---

## USB drivers

### ThinkNode M9 on macOS — driver required

The M9 does **not** use the ESP32's native USB. It routes serial through a
**WCH CH34x** USB-to-serial chip, and macOS will not give you a usable port for it
out of the box. Symptom: you plug the board in and nothing new appears in
`ls /dev/cu.*`.

Install WCH's driver:

1. Get **CH34xVCPDriver** — free on the [Mac App Store](https://apps.apple.com/app/ch34xvcpdriver/id1335343771),
   or from [wch.cn](https://www.wch.cn/downloads/CH34XSER_MAC_ZIP.html).
2. Open the app once. It asks you to enable the driver extension.
3. Approve it in **System Settings → General → Login Items & Extensions →
   Driver Extensions**.
4. Unplug and replug the M9. It should now appear as `/dev/cu.wchusbserial*`.

Check it is active:

```bash
systemextensionsctl list | grep -i ch34
```

You want a line reading `cn.wch.CH34xVCPDriver ... [activated enabled]`.

### ThinkNode M9 on Windows

Install the **CH341SER** driver from
[wch.cn](https://www.wch-ic.com/downloads/CH341SER_EXE.html) if the board shows up
in Device Manager as an unknown device. Recent Windows versions often install it
via Windows Update on their own — check for a new `COM` port first.

### ThinkNode M9 on Linux

No driver install needed; the `ch341` module ships with the kernel. Two things do
bite people:

- **Permissions.** Add yourself to the serial group, then log out and back in:
  `sudo usermod -aG dialout $USER` (`uucp` on Arch).
- **brltty.** This braille-display service grabs CH340 devices and makes the port
  vanish a second after it appears. If that happens:
  `sudo apt remove brltty` (or mask the service).

### Every other board

No driver needed on any OS — they use native USB.

---

## 1. Flash a release (easiest)

### Get the files

From the [Releases page](https://github.com/oumike/camillia-mt/releases), download
the `.bin` for your device:

| Device | File |
|---|---|
| LilyGo T-Deck | `camillia-mt-tdeck-vX.Y.Z.bin` |
| LilyGo T-Deck Pro | `camillia-mt-tdeck-pro-vX.Y.Z.bin` |
| LilyGo T-Lora Pager TFT | `camillia-mt-tlora-pager-tft-vX.Y.Z.bin` |
| M5Stack Cardputer + Cap | `camillia-mt-cardputer-cap-vX.Y.Z.bin` |
| Heltec V4 | `camillia-mt-heltec-vX.Y.Z.bin` |
| Heltec V4 (vertical UI) | `camillia-mt-heltec-vertical-vX.Y.Z.bin` |
| Attaky Mesh Deck | `camillia-mt-mesh-deck-vX.Y.Z.bin` |
| Elecrow ThinkNode M9 | `camillia-mt-m9-vX.Y.Z.bin` |
| Seeed Wio Tracker L2 | `camillia-mt-wio-tracker-l2-vX.Y.Z.bin` |

These are full images — bootloader, partition table and app in one file, written
at address `0x0`.

Each target also publishes an **app-only OTA image** and a detached signature, for
updating over the air instead of over USB (for example
`camillia-mt-tdeck-pro-vX.Y.Z-ota.bin` and its `.sig`). Do not flash those over
USB with the commands below.

### Install esptool

Same on all three systems:

```
pip install esptool
```

If `esptool.py` is not found afterwards, use `python -m esptool` in its place
everywhere below.

### Flash it — macOS and Linux

Download `flash.sh` from the same release, then:

```bash
chmod +x flash.sh
./flash.sh camillia-mt-<device>-vX.Y.Z.bin <port>
```

Examples:

```bash
./flash.sh camillia-mt-m9-v4.8.4.bin /dev/cu.wchusbserial110   # macOS, M9
./flash.sh camillia-mt-tdeck-v4.8.4.bin /dev/cu.usbmodem1101   # macOS, T-Deck
./flash.sh camillia-mt-tdeck-v4.8.4.bin /dev/ttyACM0           # Linux, T-Deck
```

The port argument is optional and defaults to `/dev/ttyUSB0`, so on macOS you
almost always want to pass it.

### Flash it — Windows (PowerShell)

`flash.sh` is a shell script and does not run on Windows. Run esptool directly —
this is exactly what the script does:

```powershell
esptool.py --chip esp32s3 --port COM3 --baud 921600 `
    --before default_reset --after hard_reset `
    write_flash -z 0x0 camillia-mt-<device>-vX.Y.Z.bin
```

The backtick `` ` `` is PowerShell's line-continuation character. To keep it on one
line, drop the backticks and the line breaks.

Replace `COM3` with your port.

### Erasing first (optional, destructive)

A plain flash keeps your settings, channels and the node's identity key. Erasing
throws all of that away — your node gets a **new identity**, and peers that stored
your old public key will no longer match it. Only erase to test first-boot
behaviour, not to update a device.

macOS / Linux:

```bash
./flash.sh --erase camillia-mt-<device>-vX.Y.Z.bin <port>
```

Windows (PowerShell):

```powershell
esptool.py --chip esp32s3 --port COM3 --baud 921600 `
    --before default_reset --after no_reset erase_flash
```

...then run the normal flash command.

### Watch it boot

```
pio device monitor --port <port> --baud 115200
```

Or any serial terminal at **115200 baud** — PuTTY on Windows, `screen <port> 115200`
on macOS/Linux.

---

## 2. Build from source

### Install PlatformIO

You need Python 3 first, then PlatformIO Core.

**Windows (PowerShell)**

```powershell
winget install Python.Python.3.12
pip install -U platformio
```

If `pio` is not recognised afterwards, it is installed but not on your PATH.
Either use the full path:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e tdeck
```

...or add it permanently:

```powershell
[Environment]::SetEnvironmentVariable(
    "Path",
    "$env:Path;$env:USERPROFILE\.platformio\penv\Scripts",
    "User")
```

Then open a new PowerShell window.

**macOS**

```bash
brew install python
pip3 install -U platformio
```

**Linux**

```bash
sudo apt install python3 python3-pip python3-venv   # Debian/Ubuntu
pip3 install -U platformio
```

On any OS you can instead install the
[PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode) for
VS Code, which bundles Core and gives you build/upload buttons.

Everything else — toolchains, libraries, board files — PlatformIO downloads by
itself on the first build. Expect that first build to take a few minutes.

### Get the source

```
git clone https://github.com/oumike/camillia-mt.git
cd camillia-mt
```

### Build (no flashing)

Pick the environment for your board:

| Device | Environment |
|---|---|
| LilyGo T-Deck | `tdeck` |
| LilyGo T-Deck Pro | `tdeck-pro` |
| LilyGo T-Lora Pager TFT | `tlora-pager-tft` |
| M5Stack Cardputer + Cap | `cardputer-cap` |
| Heltec V4 | `heltec-v4` |
| Heltec V4 (vertical UI) | `heltec-v4-vertical` |
| Attaky Mesh Deck | `mesh-deck` |
| Elecrow ThinkNode M9 | `m9` |
| Seeed Wio Tracker L2 | `wio-tracker-l2` |

The command is identical on Windows, macOS and Linux:

```
pio run -e tdeck
```

### Build and flash

```
pio run -e tdeck -t upload
```

PlatformIO usually finds the port on its own. If it picks the wrong one:

```
pio run -e tdeck -t upload --upload-port COM3            # Windows
pio run -e tdeck -t upload --upload-port /dev/cu.usbmodem1101   # macOS
pio run -e tdeck -t upload --upload-port /dev/ttyACM0    # Linux
```

For the **M9**, erase and flash in a single step — use `upload_erase`, not the
separate `erase` target (see [ThinkNode M9](#thinknode-m9) below):

```
pio run -e m9 -t upload_erase
```

### Serial monitor

```
pio device monitor
```

Add `-e <env>` to use that environment's baud rate, or `--port` / `--baud` to set
them by hand. Exit with `Ctrl+C`.

### Helper script (macOS and Linux only)

`build-upload-monitor.sh` builds, uploads and opens the monitor in one go:

```bash
./build-upload-monitor.sh --tdeck
./build-upload-monitor.sh --m9 --erase
```

Run it with no flags to get a device picker.

| Flag | Environment |
|---|---|
| `--tdeck`, `-t` | `tdeck` |
| `--tdeck-pro`, `-p` | `tdeck-pro` |
| `--debug`, `-d` | `tdeck-debug` (no such env in `platformio.ini` today — this flag will fail) |
| `--cardputer`, `-C` | `cardputer-cap` |
| `--pager`, `-P` | `tlora-pager-tft` |
| `--heltec`, `-H` | `heltec-v4` |
| `--heltec-vertical`, `--vertical`, `-V` | `heltec-v4-vertical` |
| `--mesh-deck`, `--attaky`, `-M` | `mesh-deck` |
| `--m9`, `-9` | `m9` |
| `--wio-tracker-l2` | `wio-tracker-l2` |
| `--erase`, `-E` | erase flash before a clean build/upload (M9 uses `upload_erase`) |

Windows users: run the three `pio` commands above instead, or use WSL.

---

## Build settings

| Setting | Value |
|---|---|
| Platform | espressif32 7.0.1 |
| Framework | Arduino |
| Flash | 16 MB, dual-slot OTA partitions (8 MB on Cardputer). Mesh Deck and Heltec use `partitions_16mb_fs.csv`, which adds a 9.5 MB LittleFS partition after the app slots; the Wio Tracker L2 uses the standard table and stores files on SD_MMC |
| PSRAM | enabled (OPI; none on Cardputer) |
| Upload speed | 115200 |

---

## Troubleshooting

**No port appears at all.** Check the cable is a data cable, then check
[USB drivers](#usb-drivers) — on macOS with an M9 this is almost always the
missing CH34x driver.

**The board must be in download mode to flash.** Most boards are put there
automatically over USB. On the T-Deck you may need to hold the trackball button
while pressing reset. The Wio Tracker L2 uses native USB-CDC and may need its
DFU/download-mode gesture first.

**Upload fails partway, or the port disappears mid-flash.** Try a lower speed:
add `--upload-port` plus `--upload-speed 115200`, or a different USB port/cable.

**The device flashes fine but the serial monitor is silent.** Make sure you are on
the right port — boards with a USB-serial bridge (M9, Mesh Deck) expose a
different port than native-USB boards. Baud is 115200.

**Permission denied on the port (Linux).** You are not in the `dialout` group —
see [Linux drivers](#thinknode-m9-on-linux).

**`[patch_radiolib_lr11x0] NOT patched - run the build once more`.** Harmless on a
fresh checkout: the patch ran before PlatformIO had downloaded RadioLib. Build
again and it lands.

After flashing, the device boots straight into the firmware — there is nothing
else to install.

---

## Board-specific notes

### Patched libraries

Two `pre:` scripts rewrite third-party sources in `.pio/libdeps` before a build.
PlatformIO gives every environment its own copy, so a patch only reaches the env
that lists the script.

- `tools/patch_lgfx_dmadesc.py` — every display env. LovyanGFX and M5GFX (which
  vendors the same file) free the SPI DMA descriptor array before allocating its
  replacement, record the new size whether or not the allocation succeeded, and
  then dereference the result without a null check. Under memory pressure that
  turns a 12-24 byte allocation failure into `StoreProhibited` at `EXCVADDR
  0x00000004` inside `Bus_SPI::_setup_dma_desc_links`, permanently — the stale
  size stops it ever retrying. The patch allocates before freeing and bails out
  when there is nothing usable, so a failure drops a frame instead. See #49 for
  the memory budget that causes the failure in the first place.
- `tools/patch_radiolib_lr11x0.py` — `m9` only; see the ThinkNode M9 notes below.

Both are idempotent. The LovyanGFX patch fails the build if an existing
`Bus_SPI.cpp` no longer matches or is only partially patched; the RadioLib patch
still emits a warning on version drift. If you see `NOT patched - run the build
once more` on a fresh checkout, the library had not been fetched yet; build again.

### Heltec (heltec-v4, heltec-v4-vertical)

- These envs moved from `partitions.csv` to `partitions_16mb_fs.csv` to give the
  board a filesystem, since it has no SD slot. The app slots and NVS are at the
  same offsets in both tables, so an OTA between them is safe — but **OTA does
  not rewrite the partition table**, which lives outside both app slots. A device
  updated over the air keeps its old table, finds no `littlefs` partition, logs
  `[fs] internal flash mount FAILED (partition 'littlefs' missing?)` and carries
  on with chat history in RAM only.
- To actually get the partition, flash over USB once:
  `pio run -e heltec-v4 -t upload`. Nothing needs erasing first — the new table
  keeps NVS where it was, so settings, channels and the node identity survive.
- The env sets `board_upload.flash_size` and `board_upload.maximum_size`
  alongside `board_build.flash_size`, and all three are load-bearing. The
  `esp32-s3-devkitc-1` board definition declares an 8 MB part; that is the size
  the bootloader is built against, and it rejects a partition table reaching
  past it *before any app code runs*. The symptom is a boot loop showing nothing
  but the ROM banner and `entry 0x403c98d0`, over and over — no bootloader log,
  no panic, no app output. The same note is on the mesh-deck env, which hit it
  first.

### ThinkNode M9

- **The console is an external UART bridge, not native USB-CDC.** The `m9` env
  builds with `ARDUINO_USB_CDC_ON_BOOT=0` for that reason; a build with CDC on
  boot sends its logs to a `ttyACM` port this board does not expose and looks
  completely silent. This is also why the board needs the
  [CH34x driver on macOS](#thinknode-m9-on-macos--driver-required) and shows up as
  `/dev/cu.wchusbserial*` rather than `/dev/cu.usbmodem*`.
- Erase and flash in one esptool session with `pio run -e m9 -t upload_erase`
  (or `./build-upload-monitor.sh --m9 --erase`). The separate `erase` target
  needs a second port grab this board does not always give up cleanly.
- First build on a fresh checkout may print
  `[patch_radiolib_lr11x0] NOT patched - run the build once more`. That is the
  RadioLib old-firmware patch running before PlatformIO has fetched RadioLib —
  run the build again and it lands. Skipping it shows up as `[radio] init
  failed: -706` on preproduction units.
