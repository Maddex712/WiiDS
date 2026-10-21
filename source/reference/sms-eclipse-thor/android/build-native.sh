#!/usr/bin/env bash
set -euo pipefail
port_root=$(cd "$(dirname "$0")/.." && pwd)
sdk_root=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}
ndk_root=${ANDROID_NDK_HOME:-${sdk_root:+$sdk_root/ndk/28.2.13676358}}
[[ -f "$ndk_root/build/cmake/android.toolchain.cmake" ]] || { echo 'Set ANDROID_NDK_HOME or ANDROID_SDK_ROOT (NDK 28.2.13676358).' >&2; exit 1; }
sdl_root="$port_root/build/deps/SDL2-2.32.10"
mkdir -p "$port_root/build/deps"
if [[ ! -f "$sdl_root/CMakeLists.txt" ]]; then
  archive="$port_root/build/deps/SDL2-2.32.10.tar.gz"
  curl -fL --retry 3 -o "$archive.part" 'https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz'
  printf '%s  %s\n' '5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165' "$archive.part" | sha256sum -c -
  mv "$archive.part" "$archive"
  tar -xzf "$archive" -C "$port_root/build/deps"
fi
git -C "$port_root" submodule update --init decomp
if [[ ${1:-} == --prepare ]]; then exit 0; fi
cmake -S "$port_root" -B "$port_root/build/android-arm64" \
  -DCMAKE_TOOLCHAIN_FILE="$ndk_root/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
  -DSMS_ARCH=64 -DSMS_GX_BUILD_TESTS=OFF -DSMS_SDL2_SOURCE="$sdl_root" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build "$port_root/build/android-arm64" --target sms smsbootstrap --parallel "${JOBS:-2}"
