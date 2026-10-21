#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
f=libs/JSystem/src/J3D/J3DGraphBase/J3DTevs.cpp
mkdir -p "$test_dir/libs/dolphin/include/dolphin" "$(dirname "$test_dir/$f")"
cp decomp/libs/dolphin/include/dolphin/os.h "$test_dir/libs/dolphin/include/dolphin/os.h"
cp "decomp/$f" "$test_dir/$f"
patch -s -p1 -d "$test_dir" -i "$PWD/decomp-patches/android-02-mem1-addresses.patch"
opts=(-std=c++17 -O2 -DGEKKO -DTARGET_PC -DVERSION_GMSE01 -DBUILD_VERSION=2 -DNDEBUG=1 -ffunction-sections -fdata-sections -fpermissive -w
    -include "$PWD/src/port_compat.h"
    -Iplatform/gx/port_include -Iplatform/gx/include -Isrc -Isrc/port_include -I"$test_dir/libs/dolphin/include"
    -Idecomp/include -Idecomp/libs/dolphin/include -Idecomp/libs/JSystem/include)
build() {
    "${CXX:-g++}" "${opts[@]}" "$@" platform/tests/j3d_array_addresses_test.cpp \
        "$test_dir/$f" -Wl,--gc-sections -Wl,--unresolved-symbols=ignore-all -o "$test_dir/test"
}
build -D__ANDROID__
if "$test_dir/test" >/dev/null 2>&1; then
    echo 'ERROR: old Android vertex array writer unexpectedly passed relocation' >&2
    exit 1
fi
echo 'Confirmed the old Android vertex array writer fails the relocated-address fixture.'
patch -s -p1 -d "$test_dir" -i "$PWD/decomp-patches/android-04-j3d-array-addresses.patch"
build -D__ANDROID__
"$test_dir/test"
build
"$test_dir/test" desktop
