#!/usr/bin/env bash
# Host tests. No hardware, no PlatformIO -- these compile with the system
# compiler, which is the point: the modules under test are kept free of Arduino
# headers so their logic can be checked without a device in the loop.
#
# Each test compiles ../src/<name>.cpp for a test_<name>.cpp, plus anything it
# names in a "// test-deps:" line. Declaring them beats globbing src/: most of
# this firmware needs Arduino, and a test that quietly pulled in a file which
# does would fail to link for a reason that has nothing to do with the test.
#
# A "// test-cflags:" line adds compiler flags, paths relative to tests/. It is
# how a module that includes <Arduino.h> or mbedtls builds against the minimal
# stand-ins in host/.
# Pass suite names to select tests. SANITIZE=1 enables ASan and fatal UBSan.
set -euo pipefail
cd "$(dirname "$0")"
shopt -s nullglob

CXX="${CXX:-c++}"
flags=(-std=c++17 -Wall -Wextra -Werror -O1)
case "${SANITIZE:-0}" in
    0) ;;
    1) flags+=(-g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer) ;;
    *) echo "SANITIZE must be 0 or 1" >&2; exit 2 ;;
esac

all_tests=(test_*.cpp)
if [ "${#all_tests[@]}" -eq 0 ]; then
    echo "No host test suites found" >&2
    exit 1
fi

fail=0
for t in "${all_tests[@]}"; do
    name="${t%.cpp}"
    if [ ! -f "native/$name/test_main.cpp" ]; then
        echo "$name: no native/$name/test_main.cpp for pio test -e native" >&2
        fail=1
    fi
done
for wrapper in native/test_*/test_main.cpp; do
    name="${wrapper#native/}"
    name="${name%/test_main.cpp}"
    if [ ! -f "$name.cpp" ]; then
        echo "$name: native suite has no matching host test $name.cpp" >&2
        fail=1
    fi
done
[ "$fail" -eq 0 ] || exit 1

tests=("${all_tests[@]}")
if [ "$#" -gt 0 ]; then
    tests=()
    for name in "$@"; do
        name="${name%.cpp}"
        if [[ "$name" != test_* || "$name" == */* || ! -f "$name.cpp" ]]; then
            echo "Unknown test suite: $name" >&2
            exit 2
        fi
        tests+=("$name.cpp")
    done
fi

OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

for t in "${tests[@]}"; do
    name="${t%.cpp}"
    srcs=("$t")

    mod="../src/${name#test_}.cpp"
    [ -f "$mod" ] && srcs+=("$mod")

    deps="$(sed -n 's|^// *test-deps: *||p' "$t" | head -1 || true)"
    for d in $deps; do
        [ -f "../src/$d" ] || { echo "$name: no such dep ../src/$d"; exit 1; }
        srcs+=("../src/$d")
    done

    cflags="$(sed -n 's|^// *test-cflags: *||p' "$t" | head -1 || true)"

    printf '%-28s ' "$name"
    # shellcheck disable=SC2086
    if ! "$CXX" "${flags[@]}" $cflags -o "$OUT/$name" "${srcs[@]}" 2> "$OUT/$name.log"; then
        echo "BUILD FAILED"
        cat "$OUT/$name.log"
        fail=$((fail + 1))
        continue
    fi
    if ! "$OUT/$name"; then
        echo "$name: FAILED"
        fail=$((fail + 1))
    fi
done

printf '\n%d suite(s), %d failed\n' "${#tests[@]}" "$fail"
[ "$fail" -eq 0 ]
