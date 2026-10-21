#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
# The fixture needs Linux's MAP_32BIT for the port's low-address SDK stacks.
if [[ $(uname -s) != Linux || $(uname -m) != x86_64 ]]; then
    echo "This host scheduler fixture requires Linux x86_64." >&2
    exit 1
fi
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT
flags=(-std=c++17 -D__ANDROID__ -Isrc -Isrc/port_include -Iplatform -Iplatform/os
       -Idecomp/libs/dolphin/include -Wall -Wextra -Werror -pthread)
"${CXX:-g++}" "${flags[@]}" platform/os/os_thread.cpp platform/os/tests/background_yield_test.cpp \
    -o "$build_dir/scheduler"
timeout 10s "$build_dir/scheduler"
# vi.cpp has existing unused SDK parameters. Discard unrelated VI functions
# so this test links the real pacing path without graphics/ROM dependencies.
"${CXX:-g++}" "${flags[@]}" -Wno-unused-parameter -DSMS_TEST_FRAME_WAIT \
    -ffunction-sections -fdata-sections platform/os/os_thread.cpp platform/vi/vi.cpp \
    platform/os/tests/background_yield_test.cpp -Wl,--gc-sections -o "$build_dir/frame-wait"
timeout 10s "$build_dir/frame-wait"
