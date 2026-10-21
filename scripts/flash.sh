#!/bin/bash
# Flash Camillia-MT to a connected supported ESP32 board.
# Expects a merged factory image (bootloader + partitions + boot_app0 + app),
# such as the camillia-mt-*-vX.Y.Z.bin produced by the release workflow.
# That layout is written at 0x0; an app-only firmware.bin from .pio/build/<env>
# is NOT compatible with this script.
#
# Usage: ./flash.sh [--erase|-E] <firmware.bin | board> [port]
#   board         A release board name (e.g. tdeck, tdeck-pro, heltec-v4): flashes
#                 that board's factory image, camillia-mt-<board>-vX.Y.Z.bin, from
#                 the current directory. Never an -ota.bin or another board's.
#   --erase, -E   Erase the whole chip before writing.
# Default port: /dev/ttyUSB0 (Linux) — use /dev/cu.usbmodem* on macOS.
#
# Erase is opt-in, not the default: a plain flash leaves NVS alone, so settings
# and the node's Curve25519 identity survive an update. Erasing discards both,
# and a new identity means peers' stored public key for this node no longer
# matches. Use it to test first-boot behaviour, not to update a device.

set -e

ERASE=0
ARGS=()
for arg in "$@"; do
    case "$arg" in
        --erase|-E) ERASE=1 ;;
        *)          ARGS+=("$arg") ;;
    esac
done

TARGET=${ARGS[0]:-}
PORT=${ARGS[1]:-/dev/ttyUSB0}

usage() {
    echo "Usage: ./flash.sh [--erase|-E] <camillia-mt-<board>-vX.Y.Z.bin | board> [port]" >&2
}

if [[ -z "$TARGET" ]]; then
    usage
    exit 1
fi

if [[ -f "$TARGET" ]]; then
    FIRMWARE="$TARGET"
else
    # A board name: its factory image only, newest version first.
    BOARD="$TARGET"
    [[ "$BOARD" == "heltec-v4" ]] && BOARD="heltec"
    FIRMWARE=""
    for f in camillia-mt-"$BOARD"-v*.bin; do
        [[ -f "$f" ]] || continue
        case "$f" in *-ota.bin) continue ;; esac
        [[ "$f" =~ ^camillia-mt-${BOARD}-v[0-9]+\.[0-9]+\.[0-9]+(-alpha\.[0-9]+)?\.bin$ ]] || continue
        FIRMWARE="$(printf '%s\n%s\n' "$FIRMWARE" "$f" | sed '/^$/d' | sort -V | tail -1)"
    done
    if [[ -z "$FIRMWARE" ]]; then
        echo "No factory image for '$TARGET' here (camillia-mt-${BOARD}-vX.Y.Z.bin)." >&2
        usage
        exit 1
    fi
fi

# A factory image carries the partition table at 0x8000 (magic AA 50). An
# app-only image (-ota.bin, firmware.bin) does not, and written at 0x0 it
# replaces the bootloader and leaves the board unbootable.
PT_MAGIC="$(od -An -tx1 -j 32768 -N 2 "$FIRMWARE" 2>/dev/null | tr -d ' \n')"
if [[ "$PT_MAGIC" != "aa50" ]]; then
    echo "$FIRMWARE is not a merged factory image (no partition table at 0x8000)." >&2
    echo "Use camillia-mt-<board>-vX.Y.Z.bin, not the -ota.bin or a .pio firmware.bin." >&2
    exit 1
fi

if [[ "$ERASE" == "1" ]]; then
    echo "Erasing $PORT (settings and node identity will be lost)..."
    esptool.py --chip auto --port "$PORT" --baud 921600 \
        --before default_reset --after no_reset \
        erase_flash
fi

echo "Flashing $FIRMWARE to $PORT..."
esptool.py --chip auto --port "$PORT" --baud 921600 \
    --before default_reset --after hard_reset \
    write_flash -z 0x0 "$FIRMWARE"
