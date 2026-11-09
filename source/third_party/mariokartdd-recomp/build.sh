#!/bin/sh
# Configure (first run) and build. Extra arguments are passed to ninja.
set -e
cd "$(dirname "$0")"
# The shared recompiler and runtime are a submodule.
if [ ! -f gcn-recomp/CMakeLists.txt ]; then
  git submodule update --init
fi
# On macOS, prefer the full Xcode toolchain when installed: some Command Line Tools
# installs ship an incomplete libc++ header set.
if [ "$(uname -s)" = "Darwin" ] && [ -d /Applications/Xcode.app ]; then
  export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
  EXTRA="-DCMAKE_C_COMPILER=$(xcrun -f clang) -DCMAKE_CXX_COMPILER=$(xcrun -f clang++) -DCMAKE_OSX_SYSROOT=$(xcrun --show-sdk-path)"
fi
if [ ! -f build/build.ninja ]; then
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo $EXTRA
fi
ninja -C build "$@"
