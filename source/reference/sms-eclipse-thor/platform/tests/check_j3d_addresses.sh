#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
mkdir -p "$test_dir/libs/dolphin/include/dolphin" "$test_dir/libs/JSystem/src"
cp decomp/libs/dolphin/include/dolphin/os.h "$test_dir/libs/dolphin/include/dolphin/os.h"
cp decomp/libs/JSystem/src/JRenderer.cpp "$test_dir/libs/JSystem/src/JRenderer.cpp"
patch -s -p1 -d "$test_dir" -i "$PWD/decomp-patches/android-02-mem1-addresses.patch"
opts=(-std=c++17 -O2 -DTARGET_PC -ffunction-sections -fdata-sections -fpermissive
    -include "$PWD/src/port_compat.h"
    -Isrc -Isrc/port_include -I"$test_dir/libs/dolphin/include"
    -Idecomp/include -Idecomp/libs/dolphin/include -Idecomp/libs/JSystem/include)
build() {
    "${CXX:-g++}" "${opts[@]}" "$@" platform/tests/j3d_addresses_test.cpp \
        "$test_dir/libs/JSystem/src/JRenderer.cpp" -Wl,--gc-sections -o "$test_dir/test"
}
build -D__ANDROID__
if "$test_dir/test"; then
    echo 'ERROR: old Android texture writers unexpectedly passed relocation' >&2
    exit 1
fi
echo 'Confirmed old Android writers fail the relocated-address fixture.'
patch -s -p1 -d "$test_dir" -i "$PWD/decomp-patches/android-03-j3d-texture-addresses.patch"
build -D__ANDROID__
"$test_dir/test"
build
"$test_dir/test" desktop
