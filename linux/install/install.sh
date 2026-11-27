#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Installs Camillia-MT on a CardputerZero. Run on the device, from the folder
# linux/build.sh --deploy copied it to:
#   sudo ~/camillia/install.sh            install or update
#   sudo ~/camillia/install.sh --remove   remove the app, keep its data
# The app runs as whoever starts it from the launcher; that user needs the
# spi, gpio, i2c, dialout (GPS), video (screen) and input (keyboard) groups.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
[[ $EUID -eq 0 ]] || { echo "run with sudo" >&2; exit 1; }
RUN_AS="${CAMILLIA_USER:-${SUDO_USER:-}}"
LAUNCH=/usr/share/APPLaunch

# Earlier builds were a radio daemon plus a separate screen program; the app
# replaces both. Its settings in ~/.config/camillia are left alone.
remove_old_daemon() {
  if [[ -f /etc/systemd/system/camillia-czero.service ]]; then
    systemctl disable --now camillia-czero 2>/dev/null || true
    rm -f /etc/systemd/system/camillia-czero.service
    systemctl daemon-reload
  fi
  rm -f /usr/local/bin/camillia-czero "$LAUNCH/bin/camillia-czero-ui"
}

restart_launcher() {
  # The launcher reads its app list at start; it is a user service.
  [[ -n "$RUN_AS" && "$RUN_AS" != root ]] || return 0
  systemctl --user -M "$RUN_AS@" try-restart APPLaunch.service 2>/dev/null || true
}

if [[ "${1:-}" == "--remove" ]]; then
  pkill -x camillia 2>/dev/null || true
  pkill -f "$LAUNCH/bin/camillia-desktop" 2>/dev/null || true
  remove_old_daemon
  rm -f "$LAUNCH/bin/camillia" "$LAUNCH/bin/camillia-desktop" "$LAUNCH/bin/camillia-launch" \
        "$LAUNCH/applications/camillia.desktop" "$LAUNCH/share/images/camillia.png"
  rm -rf /usr/share/camillia
  restart_launcher
  echo "removed (data kept in ~/.config/camillia-app)"
  exit 0
fi

# The stock LoRa apps drive the same radio; two programs on one SX1262 make
# neither work. Say so rather than fight over it.
for other in Cap-LoRa-1262 meshterm; do
  if pgrep -f "$other" >/dev/null; then echo "warning: $other is running and uses the same radio" >&2; fi
done
if pgrep -x camillia >/dev/null || pgrep -f "$LAUNCH/bin/camillia-desktop" >/dev/null; then
  echo "note: Camillia is running; the update takes effect when it is next started" >&2
fi

remove_old_daemon
install -d "$LAUNCH/bin" "$LAUNCH/applications" "$LAUNCH/share/images"
# Install beside and rename over, so a running copy keeps its file.
install -m 0755 "$HERE/camillia" "$LAUNCH/bin/.camillia.new"
mv -f "$LAUNCH/bin/.camillia.new" "$LAUNCH/bin/camillia"
install -m 0755 "$HERE/camillia-desktop" "$LAUNCH/bin/.camillia-desktop.new"
mv -f "$LAUNCH/bin/.camillia-desktop.new" "$LAUNCH/bin/camillia-desktop"
install -m 0755 "$HERE/camillia-launch" "$LAUNCH/bin/camillia-launch"
install -m 0644 "$HERE/camillia.png" "$LAUNCH/share/images/camillia.png"

# Icon paths in the stock entries are relative to $LAUNCH; follow whichever
# form they use.
icon="share/images/camillia.png"
if grep -qh '^Icon=/' "$LAUNCH"/applications/*.desktop 2>/dev/null; then icon="$LAUNCH/share/images/camillia.png"; fi
sed "s|@ICON@|$icon|" "$HERE/camillia.desktop" > "$LAUNCH/applications/camillia.desktop"
chmod 0644 "$LAUNCH/applications/camillia.desktop"
# The desktop menu entry (Internet), which the app copies into the user's
# applications folder when it starts.
install -d /usr/share/camillia
install -m 0644 "$HERE/camillia-desktop.desktop" /usr/share/camillia/camillia.desktop
install -m 0644 "$HERE/camillia.png" /usr/share/camillia/camillia.png

if [[ -n "$RUN_AS" && "$RUN_AS" != root ]]; then
  missing=""
  for g in spi gpio i2c dialout video input; do
    id -nG "$RUN_AS" | tr ' ' '\n' | grep -qx "$g" || missing+=" $g"
  done
  [[ -z "$missing" ]] || echo "warning: $RUN_AS is not in:$missing (sudo usermod -aG <group> $RUN_AS)" >&2
fi
restart_launcher
echo "installed; start Camillia from the launcher (log: ~/.config/camillia-app/app.log)"
