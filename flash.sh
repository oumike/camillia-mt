#!/bin/bash
# Flash Camillia-MT to a connected ESP32-S3 board.
# Expects a merged factory image (bootloader + partitions + boot_app0 + app),
# such as the camillia-mt-*-vX.Y.Z.bin produced by .github/workflows/build.yml.
# That layout is written at 0x0; an app-only firmware.bin from .pio/build/<env>
# is NOT compatible with this script.
#
# Usage: ./flash.sh [--erase|-E] <firmware.bin> [port]
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

FIRMWARE=${ARGS[0]:-}
PORT=${ARGS[1]:-/dev/ttyUSB0}

if [[ -z "$FIRMWARE" ]]; then
    # Fall back to any matching binary in the current directory
    FIRMWARE=$(ls camillia-mt-*.bin 2>/dev/null | sort -V | tail -1)
fi

if [[ -z "$FIRMWARE" || ! -f "$FIRMWARE" ]]; then
    echo "Usage: ./flash.sh [--erase|-E] <camillia-mt-vX.Y.Z.bin> [port]"
    exit 1
fi

if [[ "$ERASE" == "1" ]]; then
    echo "Erasing $PORT (settings and node identity will be lost)..."
    esptool.py --chip esp32s3 --port "$PORT" --baud 921600 \
        --before default_reset --after no_reset \
        erase_flash
fi

echo "Flashing $FIRMWARE to $PORT..."
esptool.py --chip esp32s3 --port "$PORT" --baud 921600 \
    --before default_reset --after hard_reset \
    write_flash -z 0x0 "$FIRMWARE"
