#!/bin/bash
# Flash the T-Display P4 V1.0 ESP32-C6 wireless coprocessor through its
# dedicated 3.3 V UART connector. This does not flash the main ESP32-P4.
#
# Put the C6 in download mode first: with the board powered normally, hold the
# coprocessor BOOT button, tap its RESET button, then release BOOT.
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

echo "Flashing ESP32-C6 coprocessor firmware: $FIRMWARE"
"${ESPTOOL[@]}" --chip esp32c6 --port "$PORT" --baud 921600 \
    --before no_reset --after no_reset \
    write_flash 0x0 "$FIRMWARE"

echo "C6 flash complete. Tap the coprocessor RESET button, then reboot the ESP32-P4."
