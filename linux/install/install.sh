#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Installs Camillia-MT on a CardputerZero. Run on the device, from the folder
# linux/build.sh --deploy copied it to:
#   sudo ~/camillia/install.sh            install or update, start the service
#   sudo ~/camillia/install.sh --remove   stop and remove everything but ~/.config/camillia
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
[[ $EUID -eq 0 ]] || { echo "run with sudo" >&2; exit 1; }
RUN_AS="${CAMILLIA_USER:-${SUDO_USER:-}}"
if [[ -z "$RUN_AS" || "$RUN_AS" == root ]]; then
  echo "run with sudo from the user the service should run as, or set CAMILLIA_USER" >&2; exit 1
fi
id -u "$RUN_AS" >/dev/null 2>&1 || { echo "no such user: $RUN_AS" >&2; exit 1; }
LAUNCH=/usr/share/APPLaunch
UNIT=/etc/systemd/system/camillia-czero.service

if [[ "${1:-}" == "--remove" ]]; then
  systemctl disable --now camillia-czero 2>/dev/null || true
  rm -f "$UNIT" /usr/local/bin/camillia-czero "$LAUNCH/bin/camillia-czero-ui" \
        "$LAUNCH/applications/camillia.desktop" "$LAUNCH/share/images/camillia.png"
  systemctl daemon-reload
  echo "removed (settings and history kept in ~$RUN_AS/.config/camillia)"
  exit 0
fi

# The stock LoRa apps drive the same radio; two programs on one SX1262 make
# neither work. Say so rather than fight over it.
for other in Cap-LoRa-1262 meshterm; do
  if pgrep -f "$other" >/dev/null; then echo "warning: $other is running and uses the same radio" >&2; fi
done

install -m 0755 "$HERE/camillia-czero" /usr/local/bin/camillia-czero
install -d "$LAUNCH/bin" "$LAUNCH/applications" "$LAUNCH/share/images"
install -m 0755 "$HERE/camillia-czero-ui" "$LAUNCH/bin/camillia-czero-ui"
install -m 0644 "$HERE/camillia.png" "$LAUNCH/share/images/camillia.png"

# Icon paths in the stock entries are relative to $LAUNCH; follow whichever
# form they use.
icon="share/images/camillia.png"
if grep -qh '^Icon=/' "$LAUNCH"/applications/*.desktop 2>/dev/null; then icon="$LAUNCH/share/images/camillia.png"; fi
sed "s|@ICON@|$icon|" "$HERE/camillia.desktop" > "$LAUNCH/applications/camillia.desktop"
chmod 0644 "$LAUNCH/applications/camillia.desktop"

sed "s|@USER@|$RUN_AS|g" "$HERE/camillia-czero.service" > "$UNIT"
systemctl daemon-reload
systemctl enable camillia-czero >/dev/null
systemctl restart camillia-czero
# The launcher reads its app list at start; it is a user service of RUN_AS.
systemctl --user -M "$RUN_AS@" try-restart APPLaunch.service 2>/dev/null || true
sleep 2
systemctl --no-pager --lines=12 status camillia-czero || true
echo "installed; settings: ~$RUN_AS/.config/camillia/config"
