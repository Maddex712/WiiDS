param(
    [string]$NdkVersion = '27.2.12479018'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$ndkRoot = Join-Path $projectRoot ".tools/android-sdk/ndk/$NdkVersion"
$toolchain = Join-Path $ndkRoot 'toolchains/llvm/prebuilt/windows-x86_64'
$compiler = Join-Path $toolchain 'bin/clang++.exe'
$nm = Join-Path $toolchain 'bin/llvm-nm.exe'
$sdlInclude = Join-Path $projectRoot 'build/android-native/_deps/sdl-src/include'
$auroraInclude = Join-Path $projectRoot 'reference/driftdroid/aurora-main/include'
$runtime = Join-Path $projectRoot 'build/android-native/libWiiCompiled.so'
$out = Join-Path $projectRoot 'build/game-driver'
$source = Join-Path $projectRoot 'diagnostics/driver/Probe.cpp'
$library = Join-Path $out 'libthorprobe.so'
foreach ($required in @($compiler, $nm, $source, $runtime, "$sdlInclude/SDL3/SDL.h", "$auroraInclude/dolphin/pad.h")) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing probe prerequisite: $required" }
}
New-Item -ItemType Directory -Force -Path $out | Out-Null
$runtimeExports = & $nm -D --defined-only $runtime
if ($LASTEXITCODE -ne 0) { throw 'Could not read runtime dynamic symbols' }
$requiredSymbols = [regex]::Matches((Get-Content -Raw -LiteralPath $source), 'LOAD\(((?:SDL_|PAD)\w+)\);')
foreach ($symbol in $requiredSymbols) {
    $name = $symbol.Groups[1].Value
    if (-not ($runtimeExports -match " $name`$")) { throw "Runtime does not export probe dependency: $name" }
}
# The PAL race-mode probe requires bounded read-only scalar access. Read8 is
# optional (the probe can extract a byte from an aligned big-endian Read32).
foreach ($name in @('_ZN6Memory6Read32Ej','_ZN6Memory8ContainsEjm',
    'aurora_get_present_timing','aurora_get_frame_interpolation_diagnostics','SDL_GetGamepadFromPlayerIndex')) {
    if (-not ($runtimeExports -match " $name`$")) { throw "Runtime does not export read-only probe dependency: $name" }
}
& $compiler '--target=aarch64-linux-android29' "--sysroot=$toolchain/sysroot" `
    -shared -fPIC -O2 -std=c++20 -fno-exceptions -fno-rtti -nostdlib++ `
    -Wall -Wextra -Werror -DTARGET_PC=1 "-I$sdlInclude" "-I$auroraInclude" $source -ldl `
    '-Wl,--no-undefined' '-Wl,-soname,libthorprobe.so' -o $library
if ($LASTEXITCODE -ne 0) { throw 'Driver native probe compilation failed' }
$exports = & $nm -D --defined-only $library
if ($LASTEXITCODE -ne 0 -or -not ($exports -match 'Java_dev_thor_mkw_driver_Driver_nativeProbe$') -or
    -not ($exports -match 'Java_dev_thor_mkw_driver_Driver_nativeProbeStage$')) {
    throw 'Driver native probe JNI export missing'
}
Write-Output $library
