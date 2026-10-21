param([string]$BuildDirectory = 'build/android-native')
$ErrorActionPreference = 'Stop'
$projectRoot = (Split-Path -Parent $PSScriptRoot).Replace('\','/')
$cmake = "$projectRoot/.tools/host-tools/cmake/data/bin/cmake.exe"
$ninja = "$projectRoot/.tools/host-tools/bin/ninja.exe"
$toolchain = "$projectRoot/.tools/android-sdk/ndk/27.2.12479018/build/cmake/android.toolchain.cmake"
foreach ($required in @($cmake, $ninja, $toolchain)) {
    if (!(Test-Path -LiteralPath $required)) { throw "Missing tool: $required" }
}
$buildPath = if ([System.IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { "$projectRoot/$BuildDirectory" }
& $cmake -S "$projectRoot/reference/driftdroid/runtime" -B $buildPath -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release `
    -DANDROID_STL=c++_shared -DMKW_ANDROID_CUSTOM_GPU_DRIVERS=OFF `
    "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,-z,max-page-size=16384" -DWIIDS_RUNTIME_GAME_DATA=ON
if ($LASTEXITCODE -ne 0) { throw 'Native configuration incomplete; inspect the reported error. Do not substitute game stubs.' }
