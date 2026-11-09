[CmdletBinding()]
param(
    [string]$SourceRoot = 'M:\WiiDS-brawl\src',
    [string]$BuildDirectory = 'M:\WiiDS-brawl\android-native',
    [string]$ToolsRoot = 'C:\Users\super\Documents\ChatGPT\PORTING\.tools',
    [int]$TranslatedJobs = 8,
    [int]$BuildJobs = 8,
    [switch]$Build
)
# Android arm64 build of Brawl Recompiled's runtime + translation (libmain.so exporting SDL_main), adapted from
# PORTING\experiments\nsmbw-thor\Configure-AndroidNative.ps1. Private owner build; no device installation.
$ErrorActionPreference = 'Stop'
$RuntimeRoot = Join-Path $SourceRoot 'wiicompiled\runtime'
$NdkRoot = Join-Path $ToolsRoot 'android-sdk\ndk\29.0.14206865'
$CMakePath = Join-Path $ToolsRoot 'host-tools\cmake\data\bin\cmake.exe'
$NinjaPath = Join-Path $ToolsRoot 'host-tools\bin\ninja.exe'
$LogPath = Join-Path $BuildDirectory 'configure.log'
function AsCMake([string]$Path) { [IO.Path]::GetFullPath($Path).Replace('\', '/') }
foreach ($file in @($CMakePath, $NinjaPath, (Join-Path $NdkRoot 'build\cmake\android.toolchain.cmake'), (Join-Path $RuntimeRoot 'CMakeLists.txt'),
                    (Join-Path $SourceRoot 'wiicompiled\generated\build_shards\shards.cmake'))) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Build prerequisite missing: $file" }
}
[IO.Directory]::CreateDirectory($BuildDirectory) | Out-Null
$flags = @(
    '-S', (AsCMake $RuntimeRoot), '-B', (AsCMake $BuildDirectory), '-G', 'Ninja',
    "-DCMAKE_MAKE_PROGRAM=$(AsCMake $NinjaPath)",
    "-DCMAKE_TOOLCHAIN_FILE=$(AsCMake (Join-Path $NdkRoot 'build\cmake\android.toolchain.cmake'))",
    '-DANDROID_ABI=arm64-v8a', '-DANDROID_PLATFORM=android-30', '-DANDROID_STL=c++_static',
    '-DCMAKE_BUILD_TYPE=Release', '-DAURORA_DAWN_PROVIDER=package', '-DAURORA_SDL3_PROVIDER=vendor',
    '-DAURORA_DAWN_VERSION=v20260603.191052',
    '-DAURORA_DAWN_PACKAGE_URL=https://github.com/theofficialgman/dawn-build/releases/download/v20260603.191052/dawn-android-aarch64.tar.gz',
    '-DAURORA_DAWN_PACKAGE_URL_HASH=SHA256=0e63e8cbf53551f703f582d1306f4257c0380353f66b53369d96952ce6d9f934',
    "-DMKW_TRANSLATED_COMPILE_JOBS=$TranslatedJobs", '-DMKW_BUILD_PRODUCTS=ON'
)
$savedPath = $env:PATH
try {
    $env:PATH = "$(Split-Path -Parent $CMakePath);$(Split-Path -Parent $NinjaPath);$env:SystemRoot\System32;$env:SystemRoot"
    $ErrorActionPreference = 'Continue'
    & $CMakePath @flags 2>&1 | ForEach-Object { "$_" } | Out-File -FilePath $LogPath -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw "Native configure failed ($LASTEXITCODE), see $LogPath" }
    # Same Dawn export fix as NSMB: the pinned package leaks the Linux CI host's absolute liblog path.
    $dawnTargets = Join-Path $BuildDirectory '_deps\dawn_prebuilt-src\lib\cmake\Dawn\DawnTargets.cmake'
    $ciLog = '/usr/local/lib/android/sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/28/liblog.so'
    if ((Test-Path -LiteralPath $dawnTargets) -and ([IO.File]::ReadAllText($dawnTargets)).Contains($ciLog)) {
        [IO.File]::WriteAllText($dawnTargets, ([IO.File]::ReadAllText($dawnTargets)).Replace($ciLog, 'log'), [Text.UTF8Encoding]::new($false))
        & $CMakePath @flags 2>&1 | ForEach-Object { "$_" } | Out-File -FilePath $LogPath -Append -Encoding utf8
        if ($LASTEXITCODE -ne 0) { throw "Native reconfigure failed ($LASTEXITCODE), see $LogPath" }
    }
    if ($Build) {
        & $CMakePath --build (AsCMake $BuildDirectory) --parallel $BuildJobs 2>&1 | ForEach-Object { "$_" } |
            Out-File -FilePath (Join-Path $BuildDirectory 'build.log') -Encoding utf8
        if ($LASTEXITCODE -ne 0) { throw "Native build failed ($LASTEXITCODE), see $BuildDirectory\build.log" }
        $library = Join-Path $BuildDirectory 'libmain.so'
        $reader = Join-Path $NdkRoot 'toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-readelf.exe'
        $header = (& $reader --file-header $library 2>&1 | Out-String)
        if ($header -notmatch 'Machine:\s+AArch64') { throw 'libmain.so is not AArch64' }
        if ((& $reader --dyn-syms $library 2>&1 | Out-String) -notmatch '\bSDL_main\b') { throw 'libmain.so lacks SDL_main' }
        "built $library $((Get-Item $library).Length) bytes sha256 $((Get-FileHash $library -Algorithm SHA256).Hash.ToLowerInvariant())"
    }
} finally {
    $env:PATH = $savedPath
}
