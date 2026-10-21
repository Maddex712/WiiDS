#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
f=src/System/DrawSyncManager.cpp
mkdir -p "$(dirname "$test_dir/$f")"
cp "decomp/$f" "$test_dir/$f"
opts=(-std=c++17 -O2 -DGEKKO -DTARGET_PC -DVERSION_GMSE01 -DBUILD_VERSION=2 -DNDEBUG=1 -fpermissive -w
    -ffunction-sections -fdata-sections -include "$PWD/src/port_compat.h"
    -Iplatform/gx/port_include -Iplatform/gx/include -Isrc -Isrc/port_include
    -Idecomp/include -Idecomp/libs/dolphin/include -Idecomp/libs/JSystem/include)
build() {
    "${CXX:-g++}" "${opts[@]}" "$@" "-DDRAW_SYNC_SOURCE=\"$test_dir/$f\"" platform/tests/draw_sync_pointers_test.cpp \
        -Wl,--gc-sections -Wl,--unresolved-symbols=ignore-all -o "$test_dir/test"
}
build -D__ANDROID__
if "$test_dir/test" >/dev/null 2>&1; then
    echo 'ERROR: old Android draw sync thread unexpectedly passed relocation' >&2
    exit 1
fi
echo 'Confirmed the old Android draw sync thread fails the relocated-address fixture.'
patch -s -p1 -d "$test_dir" -i "$PWD/decomp-patches/android-05-draw-sync-pointers.patch"
build -D__ANDROID__
"$test_dir/test"
build
"$test_dir/test" desktop
