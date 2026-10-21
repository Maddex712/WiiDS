[CmdletBinding()]
param([int]$Jobs = 8, [switch]$PackageOnly)
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$nativeRoot = Join-Path $workspace 'reference/sms-thor'
$appRoot = Join-Path $workspace 'reference/sms-launcher-thor/android'
$output = Join-Path $workspace 'build/sunshine-thor'
$ndk = Join-Path $workspace '.tools/android-sdk/ndk/29.0.14206865'
$cmake = Join-Path $workspace '.tools/host-tools/cmake/data/bin/cmake.exe'
$ninja = Join-Path $workspace '.tools/host-tools/bin/ninja.exe'
$sdl = Join-Path $output 'deps/SDL2-2.32.10'
$python = 'C:/Users/super/AppData/Local/Programs/Python/Python312/python.exe'
$gradle = 'C:/Users/super/.gradle/wrapper/dists/gradle-8.8-bin/dl7vupf4psengwqhwktix4v1/gradle-8.8/bin/gradle.bat'
$env:JAVA_HOME = 'C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot'
$env:ANDROID_HOME = Join-Path $workspace '.tools/android-sdk'
$env:PATH = 'C:/Program Files/Git/usr/bin;' + $env:PATH
New-Item -ItemType Directory -Force $output | Out-Null
if (!(Test-Path (Join-Path $sdl 'CMakeLists.txt'))) { throw 'Prepare the SHA-256 verified official SDL 2.32.10 source under build/sunshine-thor/deps first.' }
if (!$PackageOnly) {
    & $cmake -S $nativeRoot -B "$output/android-native" -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_TOOLCHAIN_FILE=$ndk/build/cmake/android.toolchain.cmake" '-DANDROID_ABI=arm64-v8a' '-DANDROID_PLATFORM=android-29' '-DCMAKE_BUILD_TYPE=Release' '-DSMS_ARCH=64' '-DSMS_GX_BUILD_TESTS=OFF' "-DSMS_ANDROID_COMPILE_JOBS=$Jobs" "-DSMS_SDL2_SOURCE=$sdl" '-DSMS_BUNDLE_DISC=' "-DPython3_EXECUTABLE=$python" *> "$output/native-configure.log"
    if ($LASTEXITCODE) { throw "CMake configure failed; see $output/native-configure.log" }
    & $cmake --build "$output/android-native" --target sms smsbootstrap SDL2 --parallel $Jobs *> "$output/native-build.log"
    if ($LASTEXITCODE) { throw "Native build failed; see $output/native-build.log" }
}
$jni = Join-Path $output 'jniLibs/arm64-v8a'
New-Item -ItemType Directory -Force $jni | Out-Null
foreach ($lib in @('libsms.so','libsmsbootstrap.so','libSDL2.so')) {
    $match = @(Get-ChildItem "$output/android-native" -Filter $lib -Recurse -File)
    if ($match.Count -ne 1) { throw "Expected one native $lib, found $($match.Count)." }
    Copy-Item -LiteralPath $match[0].FullName -Destination (Join-Path $jni $lib) -Force
    & "$ndk/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe" --strip-debug (Join-Path $jni $lib)
    if ($LASTEXITCODE) { throw "Failed to strip $lib" }
}
Copy-Item "$ndk/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" $jni -Force
& $gradle -p $appRoot --offline "-PsmsPortRoot=$nativeRoot" "-PsmsSdlRoot=$sdl" "-PsmsNativeLibs=$output/jniLibs" :app:assembleRelease *> "$output/apk-build.log"
if ($LASTEXITCODE) { throw "APK packaging failed; see $output/apk-build.log" }
$tools = Join-Path $env:ANDROID_HOME 'build-tools/34.0.0'
& "$tools/zipalign.exe" -f -p 4 "$appRoot/app/build/outputs/apk/release/app-release-unsigned.apk" "$output/Sunshine-Thor-aligned.apk"
if ($LASTEXITCODE) { throw 'zipalign failed.' }
& "$tools/apksigner.bat" sign --ks "$env:USERPROFILE/.android/debug.keystore" --ks-key-alias androiddebugkey --ks-pass pass:android --key-pass pass:android --out "$output/Sunshine-Thor.apk" "$output/Sunshine-Thor-aligned.apk"
if ($LASTEXITCODE) { throw 'APK signing failed.' }
& "$tools/apksigner.bat" verify --verbose "$output/Sunshine-Thor.apk" *> "$output/apk-signature.txt"
if ($LASTEXITCODE) { throw 'APK signature verification failed.' }
Get-FileHash "$output/Sunshine-Thor.apk" -Algorithm SHA256
