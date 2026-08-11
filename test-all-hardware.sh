#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if ! command -v pio >/dev/null 2>&1; then
  echo "PlatformIO CLI not found. Install it or run this from an environment with 'pio'."
  exit 1
fi

# Required test order:
# 1) tdeck
# 2) pager
# 3) heltec
# 4) heltec-vertical
# 5) cardputer
# 6) mesh-deck
TARGET_LABELS=(
  "tdeck"
  "pager"
  "heltec"
  "heltec-vertical"
  "cardputer"
  "mesh-deck"
)

TARGET_ENVS=(
  "tdeck"
  "tlora-pager-tft"
  "heltec-v4"
  "heltec-v4-vertical"
  "cardputer-cap"
  "mesh-deck"
)

has_env() {
  local env_name="$1"
  grep -q "^\[env:${env_name}\]" platformio.ini
}

echo "[TEST] Checking required environments in platformio.ini..."
for env_name in "${TARGET_ENVS[@]}"; do
  if ! has_env "$env_name"; then
    echo "[TEST] Missing required environment: $env_name"
    exit 1
  fi
done

echo "[TEST] Build phase: compiling all targets (no uploads)..."
for i in "${!TARGET_ENVS[@]}"; do
  label="${TARGET_LABELS[$i]}"
  env_name="${TARGET_ENVS[$i]}"
  echo ""
  echo "[BUILD] ${label} (${env_name})"
  pio run -e "$env_name"
done

echo ""
echo "[TEST] Build phase complete. Starting upload test loop."

for i in "${!TARGET_ENVS[@]}"; do
  label="${TARGET_LABELS[$i]}"
  env_name="${TARGET_ENVS[$i]}"

  echo ""
  echo "[UPLOAD] Next target: ${label} (${env_name})"
  while true; do
    echo "  1) Erase + Upload"
    echo "  2) Skip"
    read -r -p "Choose [1-2]: " choice
    case "$choice" in
      1)
        echo "[ERASE] Erasing flash on ${label} (${env_name})..."
        pio run -e "$env_name" -t erase
        pio run -e "$env_name" -t upload
        break
        ;;
      2)
        echo "[UPLOAD] Skipped ${label} (${env_name})."
        break
        ;;
      *)
        echo "Invalid choice. Enter 1 to erase and upload, or 2 to skip."
        ;;
    esac
  done
done

echo ""
echo "[TEST] Completed upload testing for all targets."
