[CmdletBinding()]
param([ValidateRange(1,8)][int]$Jobs = 8, [switch]$PackageOnly)
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$source = Join-Path $workspace 'reference/sms-eclipse-thor'
$output = Join-Path $workspace 'build/eclipse-thor'
$ndk = Join-Path $workspace '.tools/android-sdk/ndk/29.0.14206865'
$cmake = Join-Path $workspace '.tools/host-tools/cmake/data/bin/cmake.exe'
$ninja = Join-Path $workspace '.tools/host-tools/bin/ninja.exe'
$sdl = Join-Path $workspace 'build/sunshine-thor/deps/SDL2-2.32.10'
$python = 'C:/Users/super/AppData/Local/Programs/Python/Python312/python.exe'
$env:PATH = "$ndk/toolchains/llvm/prebuilt/windows-x86_64/bin;C:/Program Files/Git/usr/bin;" + $env:PATH
New-Item -ItemType Directory -Force $output | Out-Null
if (!$PackageOnly) {
    & $cmake -S $source -B "$output/android-native" -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_TOOLCHAIN_FILE=$ndk/build/cmake/android.toolchain.cmake" '-DANDROID_ABI=arm64-v8a' '-DANDROID_PLATFORM=android-29' '-DCMAKE_BUILD_TYPE=RelWithDebInfo' '-DSMS_ARCH=64' '-DSMS_GX_BUILD_TESTS=OFF' '-DSMS_ECLIPSE=ON' "-DSMS_ANDROID_COMPILE_JOBS=$Jobs" "-DSMS_SDL2_SOURCE=$sdl" '-DSMS_BUNDLE_DISC=' "-DPython3_EXECUTABLE=$python" *> "$output/native-configure.log"
    if ($LASTEXITCODE) { throw "Native configure failed; see $output/native-configure.log" }
    & $cmake --build "$output/android-native" --target sms smsbootstrap SDL2 sms_abi_check --parallel $Jobs *> "$output/native-build.log"
    if ($LASTEXITCODE) { throw "Native build failed; see $output/native-build.log" }
}
if (!(Test-Path "$output/android-native/sms_abi_check.stamp")) { throw 'Eclipse ABI check has not passed.' }
# Upstream resolves dwarfdump explicitly but nm through PATH. Require real
# direct-call coverage as well as virtual/patch checks; never accept a skipped check.
$env:SMS_LLVM_DWARFDUMP = "$ndk/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-dwarfdump.exe"
& $python "$source/tools/mods/abi_check.py" --by-unit "$output/android-native/libsms.so" "$output/android-native/libsms_eclipse.a" *> "$output/native-abi-full.log"
if ($LASTEXITCODE) { throw 'The complete Eclipse ABI check failed.' }
$abiResult = Get-Content -Raw "$output/native-abi-full.log"
if ($abiResult -notmatch '\([1-9][0-9]* game functions the mods call') { throw 'ABI direct-call coverage was skipped.' }
$jni = Join-Path $output 'jniLibs/arm64-v8a'
New-Item -ItemType Directory -Force $jni | Out-Null
foreach ($lib in @('libsms.so','libsmsbootstrap.so','libSDL2.so')) {
    $found = @(Get-ChildItem "$output/android-native" -Filter $lib -Recurse -File)
    if ($found.Count -ne 1) { throw "Expected one $lib, found $($found.Count)." }
    Copy-Item -LiteralPath $found[0].FullName -Destination (Join-Path $jni $lib) -Force
    & "$ndk/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe" --strip-debug (Join-Path $jni $lib)
    if ($LASTEXITCODE) { throw "Failed stripping $lib" }
}
Copy-Item "$ndk/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" $jni -Force
Get-ChildItem $jni -Filter '*.so' | Get-FileHash -Algorithm SHA256
