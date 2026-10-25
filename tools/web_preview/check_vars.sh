#!/usr/bin/env bash
# The theme picker previews a theme by writing CSS variables on <html>. Every
# variable the stylesheet reads for its colours must be among them, or a preview
# leaves part of the page in the old theme (dark inputs under light text).
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
apply="$(sed -n '/"function apply(){"/,/^        "}"/p' "$root/src/web_config.cpp")"
fail=0
for v in --bg --panel --panel-2 --panel-3 --line --line-hi --input --text --dim --accent --on-accent; do
  grep -q "setProperty('$v'" <<<"$apply" || { echo "apply() does not set $v"; fail=1; }
done
[ $fail = 0 ] && echo "theme preview sets every colour variable"
exit $fail
