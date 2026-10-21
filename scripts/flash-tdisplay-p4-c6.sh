#!/bin/bash
# Flash the T-Display P4 V1.0 ESP32-C6 wireless coprocessor through its
# dedicated 3.3 V UART connector. This does not flash the main ESP32-P4.
#
# Put the C6 in download mode first: with the board powered normally, hold the
# coprocessor BOOT button, tap its RESET button, then release BOOT.
#
# The ESP-Hosted image is an application image, not a merged one: it has no
# bootloader or partition table. It is written into the app partition named by
# the C6's own partition table (read from the chip first), and otadata is
# blanked so the bootloader boots it. The C6's bootloader and partition table
# are left alone. A merged image (partition table at 0x8000) is written at 0x0.
#
# Usage: ./scripts/flash-tdisplay-p4-c6.sh <port> [firmware.bin]

set -e

PORT=${1:-}
FIRMWARE=${2:-}

if [[ -z "$PORT" ]]; then
    echo "Usage: $0 <port> [esp32c6-v2.12.13.bin]" >&2
    exit 1
fi

if [[ -z "$FIRMWARE" ]]; then
    FIRMWARE=$(find dist \
        "$HOME/.platformio-p4/packages/framework-arduinoespressif32-libs/hosted" \
        "$HOME/.platformio/packages/framework-arduinoespressif32-libs/hosted" \
        -type f \( -name 'camillia-mt-tdisplay-p4-c6-esp-hosted-*.bin' \
                    -o -name 'esp32c6-v2.12.13.bin' \) \
        2>/dev/null | head -1)
fi

if [[ -z "$FIRMWARE" || ! -f "$FIRMWARE" ]]; then
    echo "ESP-Hosted C6 firmware not found." >&2
    echo "Run 'pio pkg install -e p4-amoled-sx1262' or pass the release asset path." >&2
    exit 1
fi

if [[ -x "$HOME/.platformio-p4/penv/bin/python" ]] \
    && "$HOME/.platformio-p4/penv/bin/python" -m esptool version >/dev/null 2>&1; then
    ESPTOOL=("$HOME/.platformio-p4/penv/bin/python" -m esptool)
elif python -m esptool version >/dev/null 2>&1; then
    ESPTOOL=(python -m esptool)
elif command -v esptool.py >/dev/null 2>&1; then
    ESPTOOL=(esptool.py)
else
    echo "esptool is required. Install it with: python -m pip install esptool" >&2
    exit 1
fi

PYTHON=""
for py in "$HOME/.platformio-p4/penv/bin/python" python3 python; do
    if command -v "$py" >/dev/null 2>&1; then PYTHON="$py"; break; fi
done
if [[ -z "$PYTHON" ]]; then
    echo "python3 is required." >&2
    exit 1
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/camillia-c6.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

# app: ESP image magic E9 with the app descriptor magic ABCD5432 at 0x20.
# merged: a partition table entry (AA 50) at 0x8000.
KIND="$("$PYTHON" - "$FIRMWARE" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read()
if len(d) > 0x8002 and d[0x8000:0x8002] == b'\xaa\x50':
    print('merged')
elif len(d) > 0x24 and d[0] == 0xE9 and d[0x20:0x24] == b'\x32\x54\xcd\xab':
    print('app')
else:
    print('unknown')
PY
)"

if [[ "$KIND" == "merged" ]]; then
    echo "Flashing merged ESP32-C6 image at 0x0: $FIRMWARE"
    "${ESPTOOL[@]}" --chip esp32c6 --port "$PORT" --baud 921600 \
        --before no_reset --after no_reset \
        write_flash 0x0 "$FIRMWARE"
    echo "C6 flash complete. Tap the coprocessor RESET button, then reboot the ESP32-P4."
    exit 0
fi
if [[ "$KIND" != "app" ]]; then
    echo "$FIRMWARE is neither an ESP application image nor a merged image." >&2
    exit 1
fi

# Read the partition table at 115200 so the flasher stub stays at that rate
# for the write below, which then reconnects to it.
echo "Reading the C6 partition table..."
"${ESPTOOL[@]}" --chip esp32c6 --port "$PORT" --baud 115200 \
    --before no_reset --after no_reset \
    read_flash 0x8000 0xC00 "$WORK/partitions.bin"

# Prints "<app offset> <otadata offset> <otadata size>" (otadata 0 0 if none).
LAYOUT="$("$PYTHON" - "$WORK/partitions.bin" "$FIRMWARE" "$WORK/otadata-blank.bin" <<'PY'
import os, struct, sys
pt = open(sys.argv[1], 'rb').read()
size = os.path.getsize(sys.argv[2])
parts = []
for i in range(0, len(pt) - 31, 32):
    e = pt[i:i + 32]
    if e[:2] != b'\xaa\x50':
        break
    ptype, sub, off, length = struct.unpack('<BBII', e[2:12])
    label = e[12:28].split(b'\0')[0].decode('ascii', 'replace')
    parts.append((ptype, sub, off, length, label))
if not parts:
    sys.exit('No valid partition table at 0x8000 on the C6. It needs a bootloader and '
             'partition table before an app image can be written; not flashing.')
apps = [p for p in parts if p[0] == 0]
factory = [p for p in apps if p[1] == 0x00]
ota0 = [p for p in apps if p[1] == 0x10]
target = (factory or ota0 or [None])[0]
if target is None:
    sys.exit('The C6 partition table has no factory or ota_0 app partition; not flashing.')
if size > target[3]:
    sys.exit('Image (%d bytes) does not fit app partition %s (%d bytes); not flashing.'
             % (size, target[4], target[3]))
otadata = [p for p in parts if p[0] == 1 and p[1] == 0x00]
if otadata:
    open(sys.argv[3], 'wb').write(b'\xff' * otadata[0][3])
    print('0x%x 0x%x %s' % (target[2], otadata[0][2], target[4]))
else:
    print('0x%x 0 %s' % (target[2], target[4]))
PY
)"
read -r APP_OFFSET OTADATA_OFFSET APP_LABEL <<< "$LAYOUT"
if [[ -z "$APP_OFFSET" || "$APP_OFFSET" == "0x0" ]]; then
    echo "Could not work out the C6 app partition; not flashing." >&2
    exit 1
fi

WRITES=("$APP_OFFSET" "$FIRMWARE")
if [[ "$OTADATA_OFFSET" != "0" ]]; then
    # Blank otadata so the bootloader boots the partition just written, not
    # whichever OTA slot it last selected.
    WRITES=("$OTADATA_OFFSET" "$WORK/otadata-blank.bin" "${WRITES[@]}")
fi

echo "Flashing ESP32-C6 coprocessor firmware to $APP_LABEL at $APP_OFFSET: $FIRMWARE"
"${ESPTOOL[@]}" --chip esp32c6 --port "$PORT" --baud 921600 \
    --before no_reset --after no_reset \
    write_flash "${WRITES[@]}"

echo "C6 flash complete. Tap the coprocessor RESET button, then reboot the ESP32-P4."
