# Configure the NSMBW runtime for Android arm64-v8a: the same pinned Dependencies tree as the Windows
# build (configure-native.ps1), the portable CMake/Ninja, and NDK 29 - the toolchain the pinned
# Android Dawn archive was built with, so its static libc++ code links cleanly. Produces libmain.so.
[CmdletBinding()]
param(
    [int]$TranslatedJobs = 6,
    [string]$BuildDirectory = 'E:\NSMBWPort\wiicompiled\android-build',
    [string]$LogPath = 'E:\NSMBWPort\build-nsmbw\logs\configure-android.log'
)

$ErrorActionPreference = 'Stop'
$workspace = 'E:\NSMBWPort\wiicompiled'
$toolchain = 'E:\NSMBWPort\toolchain'
$ndk = 'C:\Users\Wither\AppData\Local\Android\Sdk\ndk\29.0.14206865'
$dependencies = Join-Path $workspace 'Dependencies'

function ConvertTo-CMakePath([string]$Path) { [IO.Path]::GetFullPath($Path).Replace('\', '/') }

if (-not (Test-Path (Join-Path $ndk 'build\cmake\android.toolchain.cmake'))) { throw "NDK missing: $ndk" }
if (-not (Test-Path (Join-Path $dependencies 'dawn_prebuilt_android\lib\libwebgpu_dawn.a'))) {
    throw 'Dependencies\dawn_prebuilt_android is missing (pinned dawn-android-aarch64 v20260603.191052)'
}

# Keep the Windows compilers out of reach: only the NDK's clang may be found.
$env:PATH = "$toolchain\CMake\bin;$toolchain\Ninja;$env:SystemRoot\System32;$env:SystemRoot"

$configure = @(
    '-S', (ConvertTo-CMakePath (Join-Path $workspace 'runtime')),
    '-B', (ConvertTo-CMakePath $BuildDirectory),
    '-G', 'Ninja',
    "-DCMAKE_MAKE_PROGRAM=$(ConvertTo-CMakePath (Join-Path $toolchain 'Ninja\ninja.exe'))",
    "-DCMAKE_TOOLCHAIN_FILE=$(ConvertTo-CMakePath (Join-Path $ndk 'build\cmake\android.toolchain.cmake'))",
    '-DANDROID_ABI=arm64-v8a',
    # memfd_create (guest RAM) is in bionic from API 30.
    '-DANDROID_PLATFORM=android-30',
    # One native library holds everything (SDL, Dawn, the game), so libc++ is linked in statically.
    '-DANDROID_STL=c++_static',
    '-DCMAKE_BUILD_TYPE=Release',
    '-DAURORA_DAWN_PROVIDER=package', '-DAURORA_SDL3_PROVIDER=vendor',
    '-DCMAKE_POLICY_DEFAULT_CMP0168=NEW', '-DFETCHCONTENT_FULLY_DISCONNECTED=ON',
    "-DMKW_TRANSLATED_COMPILE_JOBS=$TranslatedJobs"
)
foreach ($directory in Get-ChildItem -LiteralPath $dependencies -Directory) {
    # Windows-only trees, and the Windows Dawn package (the Android one replaces it below).
    if ($directory.Name -in @('native_prebuilt', 'cppwinrt', 'dawn_prebuilt', 'dawn_prebuilt_android')) { continue }
    $configure += "-DFETCHCONTENT_SOURCE_DIR_$($directory.Name.ToUpperInvariant())=$(ConvertTo-CMakePath $directory.FullName)"
}
$configure += "-DFETCHCONTENT_SOURCE_DIR_DAWN_PREBUILT=$(ConvertTo-CMakePath (Join-Path $dependencies 'dawn_prebuilt_android'))"

# Windows PowerShell 5.1 turns a native program's stderr lines into error records; CMake prints
# ordinary status there, so run it with 'Continue' and keep the log as plain text.
$ErrorActionPreference = 'Continue'
& (Join-Path $toolchain 'CMake\bin\cmake.exe') @configure 2>&1 |
    ForEach-Object { "$_" } | Out-File -LiteralPath $LogPath -Encoding utf8
exit $LASTEXITCODE
