#!/usr/bin/env bash
# Linux build of Brawl Recompiled. Run on Linux or in WSL, after tools\build-brawl.ps1 has translated the game on
# Windows: this reuses that output (wiicompiled\generated) and only compiles it with clang.
#   bash tools/build-linux.sh [--jobs N]
# Sources are copied to ~/brawl-linux first (WSL compiles far faster off /mnt/c). Output: ~/brawl-linux/usa/build/WiiCompiled
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
jobs=6   # translated shards need ~1 GB each; WSL gets half the PC's memory
while [[ $# -gt 0 ]]; do
    case "$1" in
        --jobs) jobs=$2; shift ;;
        *) echo "unknown argument $1" >&2; exit 1 ;;
    esac
    shift
done

src=$root/wiicompiled
work=$HOME/brawl-linux/usa
mkdir -p "$work"

# Clang + lld: the portable toolchain WiiCompiled's own prepare-portable-tools.sh fetches.
tools=$HOME/brawl-linux/portable-tools
if [[ ! -x $tools/toolchain-x86_64/bin/clang++ ]]; then
    bash "$root/wiicompiled/Launcher/prepare-portable-tools.sh" --arch x86_64 --destination "$tools"
fi
bin=$tools/toolchain-x86_64/bin

echo "copying sources to $work"
rsync -a --delete "$src/runtime/" "$work/runtime/"
rsync -a --delete "$root/wiicompiled/aurora-main/" "$work/aurora-main/"
rsync -a --delete --exclude functions "$src/generated/" "$work/generated/"
# The translator wrote absolute Windows paths into the shard list and the data blobs' .incbin lines.
win=$(wslpath -m "$src")
grep -rlF "$win" "$work/generated" --include='*.cmake' --include='*.S' --include='*.cpp' --include='*.h' |
    xargs -r sed -i "s|$win|$work|g"

cmake -S "$work/runtime" -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$bin/clang" -DCMAKE_CXX_COMPILER="$bin/clang++" -DCMAKE_ASM_COMPILER="$bin/clang" \
    -DCMAKE_AR="$bin/llvm-ar" -DCMAKE_RANLIB="$bin/llvm-ranlib" -DCMAKE_LINKER="$bin/ld.lld" \
    -DMKW_TRANSLATED_COMPILE_JOBS="$jobs"
cmake --build "$work/build" --target WiiCompiled --parallel "$jobs"
echo "DONE: $work/build/WiiCompiled"

# The launcher's controller list (launcher/Gamepads.cs): SDL as a shared library, from the SDL sources the game uses.
sdl=$HOME/brawl-linux/sdl-shared
if [[ ! -f $sdl/libSDL3.so.0 ]]; then
    cmake -S "$work/build/_deps/sdl-src" -B "$sdl" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="$bin/clang" \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
    cmake --build "$sdl" --parallel "$jobs"
    echo "DONE: $sdl/libSDL3.so.0"
fi
