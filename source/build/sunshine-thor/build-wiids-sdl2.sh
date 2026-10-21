#!/bin/sh
# Builds libSDL2.so from the WiiDS SDL2 copy (Java glue in org.libsdl.sdl2.app), with the same
# options as Build-Thor.ps1, in its own build folder.
set -e
W=C:/Users/super/Documents/ChatGPT/PORTING
NDK=$W/.tools/android-sdk/ndk/29.0.14206865
CM=$W/.tools/host-tools/cmake/data/bin/cmake.exe
export PATH="$W/.tools/host-tools/bin:/c/Windows/System32:/c/Windows:$PATH"
B=$W/build/sunshine-thor/android-native-wiids
"$CM" -S $W/reference/sms-thor -B $B -G Ninja -DCMAKE_MAKE_PROGRAM=$W/.tools/host-tools/bin/ninja.exe \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
  -DCMAKE_BUILD_TYPE=Release -DSMS_ARCH=64 -DSMS_GX_BUILD_TESTS=OFF -DSMS_ANDROID_COMPILE_JOBS=8 \
  -DSMS_SDL2_SOURCE=$W/build/sunshine-thor/deps/SDL2-2.32.10-wiids -DSMS_BUNDLE_DISC= \
  -DPython3_EXECUTABLE=C:/Users/super/AppData/Local/Programs/Python/Python312/python.exe > $B.configure.log 2>&1
"$CM" --build $B --target SDL2 --parallel 8 > $B.build.log 2>&1
find $B -name libSDL2.so
