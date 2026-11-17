#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build the headless CardputerZero binary in a Debian trixie arm64 container.
#   linux/build.sh            -> linux/build/camillia-czero
#   linux/build.sh --deploy mike@host   (also scp it to ~/camillia/ on the device)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
IMAGE=camillia-czero-build
docker build --platform linux/arm64 -q -t "$IMAGE" "$HERE" >/dev/null
docker run --rm --platform linux/arm64 -v "$ROOT":/src -w /src/linux "$IMAGE" \
  bash -c 'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build build -j"$(nproc)"'
echo "built: $HERE/build/camillia-czero"
if [[ "${1:-}" == "--deploy" && -n "${2:-}" ]]; then
  ssh "$2" 'mkdir -p ~/camillia'
  scp -q "$HERE/build/camillia-czero" "$2":camillia/
  echo "deployed to $2:~/camillia/camillia-czero"
fi
