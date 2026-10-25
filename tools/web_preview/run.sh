#!/usr/bin/env bash
# Builds a static preview of the web config page from the firmware's own CSS,
# JS and theme code, with the system compiler (no PlatformIO, no device).
# Usage: tools/web_preview/run.sh <camD|evrL|solD|eink>
# Writes .superpowers/preview/<theme>.html (git-ignored scratch).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
theme="${1:-camD}"
out="$root/.superpowers/preview"
mkdir -p "$out"
bin="$(mktemp -d)/preview"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -o "$bin" "$here/preview.cpp" "$root/src/web_theme.cpp"
"$bin" "$theme" < "$here/sample_body.html" > "$out/$theme.html"
echo "$out/$theme.html"
