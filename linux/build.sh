#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build the CardputerZero daemon and UI in a Debian trixie arm64 container.
#   linux/build.sh            -> linux/build/camillia-czero, camillia-czero-ui
#   linux/build.sh --deploy mike@host            copy both and the installer to ~/camillia/
#   linux/build.sh --deploy mike@host --install  ...and run it (sudo on the device)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
IMAGE=camillia-czero-build
docker build --platform linux/arm64 -q -t "$IMAGE" "$HERE" >/dev/null
docker run --rm --platform linux/arm64 -v "$ROOT":/src -w /src/linux "$IMAGE" \
  bash -c 'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build build -j"$(nproc)" && build/camillia-czero-tests'
echo "built: $HERE/build/camillia-czero $HERE/build/camillia-czero-ui"
if [[ "${1:-}" == "--deploy" && -n "${2:-}" ]]; then
  ssh "$2" 'mkdir -p ~/camillia'
  scp -q "$HERE/build/camillia-czero" "$HERE/build/camillia-czero-ui" "$HERE"/install/* "$2":camillia/
  echo "deployed to $2:~/camillia/"
  if [[ "${3:-}" == "--install" ]]; then
    ssh -t "$2" 'sudo ~/camillia/install.sh'
  else
    echo "install with: ssh -t $2 sudo ~/camillia/install.sh"
  fi
fi
