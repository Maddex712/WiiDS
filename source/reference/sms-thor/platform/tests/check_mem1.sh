#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if [[ $(uname -s) != Linux || $(uname -m) != x86_64 ]]; then
    echo "The address-collision host fixture requires Linux x86_64." >&2
    exit 1
fi
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
mkdir -p "$test_dir/libs/dolphin/include/dolphin"
cp decomp/libs/dolphin/include/dolphin/os.h "$test_dir/libs/dolphin/include/dolphin/os.h"
patch -s -p1 -d "$test_dir" -i "$PWD/decomp-patches/android-02-mem1-addresses.patch"
"${CXX:-g++}" -std=c++17 -D__ANDROID__ -ffunction-sections -fdata-sections -fpermissive \
    -I"$test_dir/libs/dolphin/include" -Isrc -Isrc/port_include -Iplatform -Iplatform/mods/include \
    -Idecomp/include -Idecomp/libs/dolphin/include -Idecomp/libs/JSystem/include \
    platform/tests/mem1_test.cpp -pthread -ldl -Wl,--gc-sections -o "$test_dir/mem1"
timeout 15s "$test_dir/mem1"
