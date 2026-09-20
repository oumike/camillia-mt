#!/usr/bin/env bash
# Host tests. No hardware, no PlatformIO -- these compile with the system
# compiler, which is the point: the modules under test are kept free of Arduino
# headers so their logic can be checked without a device in the loop.
#
# Each test compiles ../src/<name>.cpp for a test_<name>.cpp, plus anything it
# names in a "// test-deps:" line. Declaring them beats globbing src/: most of
# this firmware needs Arduino, and a test that quietly pulled in a file which
# does would fail to link for a reason that has nothing to do with the test.
set -euo pipefail
cd "$(dirname "$0")"

CXX="${CXX:-c++}"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

fail=0
for t in test_*.cpp; do
    name="${t%.cpp}"
    srcs=("$t")

    mod="../src/${name#test_}.cpp"
    [ -f "$mod" ] && srcs+=("$mod")

    deps="$(sed -n 's|^// *test-deps: *||p' "$t" | head -1 || true)"
    for d in $deps; do
        [ -f "../src/$d" ] || { echo "$name: no such dep ../src/$d"; exit 1; }
        srcs+=("../src/$d")
    done

    printf '%-28s ' "$name"
    if ! "$CXX" -std=c++17 -Wall -Wextra -Werror -O1 -o "$OUT/$name" "${srcs[@]}" 2> "$OUT/$name.log"; then
        echo "BUILD FAILED"
        cat "$OUT/$name.log"
        fail=1
        continue
    fi
    "$OUT/$name" || fail=1
done

exit $fail
