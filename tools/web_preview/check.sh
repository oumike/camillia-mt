#!/usr/bin/env bash
# Runs checks.html against a preview theme in headless Chrome and prints the
# results. Needs a server on 127.0.0.1:8765 serving .superpowers/preview, e.g.
#   (cd .superpowers/preview && python3 -m http.server 8765 --bind 127.0.0.1)
# Usage: tools/web_preview/check.sh <theme>   (exit 1 on any failure)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
theme="${1:-camD}"
"$here/run.sh" "$theme" >/dev/null
cp "$here/checks.html" "$root/.superpowers/preview/"
res="$("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new --disable-gpu \
  --user-data-dir="$(mktemp -d)" --window-size=1400,1000 --virtual-time-budget=10000 \
  --dump-dom "http://127.0.0.1:8765/checks.html?t=$theme" 2>/dev/null \
  | sed -n '/<pre id="out">/,/<\/pre>/p' | sed 's/<[^>]*>//g')"
echo "$res"
grep -q "ALL OK" <<<"$res"
