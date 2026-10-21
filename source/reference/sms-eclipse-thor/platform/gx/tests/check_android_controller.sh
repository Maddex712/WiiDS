#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Werror \
    -Iplatform/gx/src $(pkg-config --cflags sdl2) \
    platform/gx/tests/android_controller_test.cpp $(pkg-config --libs sdl2) \
    -o "$test_dir/android_controller"
"$test_dir/android_controller" "$test_dir/profiles.txt"
