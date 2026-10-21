#!/bin/sh
# Relinks both NSMB products with WiiDS library names (libnsmbw.so, libmsmbwii.so).
set -e
W=C:/Users/super/Documents/ChatGPT/PORTING
CM=$W/.tools/host-tools/cmake/data/bin/cmake.exe
export PATH="$W/.tools/host-tools/cmake/data/bin:$W/.tools/host-tools/bin:/c/Windows/System32:/c/Windows:$PATH"
for pair in "e1 nsmbw" "msmbwii msmbwii"; do
  set -- $pair
  B=$W/build/nsmbw-thor/$1/android-native
  echo "== $1 -> lib$2.so $(date +%T)"
  "$CM" -DMKW_ANDROID_LIBRARY_NAME=$2 "$B" > "$B/wiids-reconfigure.log" 2>&1
  "$CM" --build "$B" --target WiiCompiled --parallel 8 > "$B/wiids-build.log" 2>&1
  ls -la "$B/lib$2.so"
done
echo "done $(date +%T)"
