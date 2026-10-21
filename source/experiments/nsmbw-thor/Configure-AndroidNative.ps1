[CmdletBinding()]
param(
    [string]$WorkspaceRoot = (Join-Path $PSScriptRoot '..\..'),
    [string]$RuntimeRoot,
    [string]$BuildDirectory,
    [string]$PreparedDirectory,
    [string]$NdkRoot,
    [string]$CMakePath,
    [string]$NinjaPath,
    [int]$TranslatedJobs = 4,
    [ValidateSet('E1','E2')][string]$DiscRevision = 'E2',
    [switch]$RuntimeOnly,
    [switch]$Build,
    [int]$BuildJobs = 8,
    [string]$Target = '',
    [string]$LogPath
)
# Configures private owner-derived builds. No device installation or game download.
$ErrorActionPreference = 'Stop'
$WorkspaceRoot = [IO.Path]::GetFullPath($WorkspaceRoot)
if (-not $RuntimeRoot) { $RuntimeRoot = Join-Path $WorkspaceRoot 'reference\nsmbw-android\runtime' }
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $WorkspaceRoot 'build\nsmbw-thor\android-native' }
if (-not $PreparedDirectory) { $PreparedDirectory = Join-Path $WorkspaceRoot "build\nsmbw-thor\$($DiscRevision.ToLowerInvariant())-prepared" }
$PreparedDirectory = [IO.Path]::GetFullPath($PreparedDirectory)
if (-not $NdkRoot) { $NdkRoot = Join-Path $WorkspaceRoot '.tools\android-sdk\ndk\29.0.14206865' }
if (-not $CMakePath) { $CMakePath = Join-Path $WorkspaceRoot '.tools\host-tools\cmake\data\bin\cmake.exe' }
if (-not $NinjaPath) { $NinjaPath = Join-Path $WorkspaceRoot '.tools\host-tools\bin\ninja.exe' }
if (-not $LogPath) { $LogPath = Join-Path $BuildDirectory 'configure-thor.log' }
function AsCMake([string]$Path) { [IO.Path]::GetFullPath($Path).Replace('\', '/') }
foreach ($file in @($CMakePath, $NinjaPath, (Join-Path $NdkRoot 'build\cmake\android.toolchain.cmake'), (Join-Path $RuntimeRoot 'CMakeLists.txt'))) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Build prerequisite missing: $file" }
}
if (-not ([IO.File]::ReadAllText((Join-Path $NdkRoot 'source.properties')) -match 'Pkg.Revision\s*=\s*29\.0\.14206865')) {
    throw 'This build pins NDK 29.0.14206865 to match upstream Android Dawn.'
}
[IO.Directory]::CreateDirectory([IO.Path]::GetFullPath($BuildDirectory)) | Out-Null
[IO.Directory]::CreateDirectory((Split-Path -Parent ([IO.Path]::GetFullPath($LogPath)))) | Out-Null
$flags = @(
    '-S', (AsCMake $RuntimeRoot), '-B', (AsCMake $BuildDirectory), '-G', 'Ninja',
    "-DCMAKE_MAKE_PROGRAM=$(AsCMake $NinjaPath)",
    "-DCMAKE_TOOLCHAIN_FILE=$(AsCMake (Join-Path $NdkRoot 'build\cmake\android.toolchain.cmake'))",
    '-DANDROID_ABI=arm64-v8a', '-DANDROID_PLATFORM=android-30', '-DANDROID_STL=c++_static',
    '-DCMAKE_BUILD_TYPE=Release', '-DAURORA_DAWN_PROVIDER=package', '-DAURORA_SDL3_PROVIDER=vendor',
    '-DAURORA_DAWN_VERSION=v20260603.191052',
    '-DAURORA_DAWN_PACKAGE_URL=https://github.com/theofficialgman/dawn-build/releases/download/v20260603.191052/dawn-android-aarch64.tar.gz',
    '-DAURORA_DAWN_PACKAGE_URL_HASH=SHA256=0e63e8cbf53551f703f582d1306f4257c0380353f66b53369d96952ce6d9f934',
    "-DMKW_TRANSLATED_COMPILE_JOBS=$TranslatedJobs", "-DMKW_BUILD_PRODUCTS=$(@{ $true = 'OFF'; $false = 'ON' }[[bool]$RuntimeOnly])"
)
if ($DiscRevision -eq 'E2') { $flags += '-DCMAKE_CXX_FLAGS=-DNSMBW_DISC_REVISION_E2=1' }
else { $flags += '-DCMAKE_CXX_FLAGS=' }
$savedPath = $env:PATH
$gitPath = (Get-Command git -ErrorAction Stop).Source
$sourceCommit = (& $gitPath -C (Split-Path -Parent $RuntimeRoot) rev-parse HEAD | Out-String).Trim()
try {
    $env:PATH = "$(Split-Path -Parent $CMakePath);$(Split-Path -Parent $NinjaPath);$env:SystemRoot\System32;$env:SystemRoot"
    $ErrorActionPreference = 'Continue'
    & $CMakePath @flags 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $LogPath
    $result = $LASTEXITCODE
    if ($result -ne 0) { throw "Native configure failed ($result), see $LogPath" }
    # The pinned Dawn export leaks the Linux CI host's absolute NDK liblog path.
    # Normalize only that imported system-library entry in this private build tree.
    # The downloaded archive and compiled Dawn library remain byte-for-byte intact.
    $dawnTargets = Join-Path $BuildDirectory '_deps\dawn_prebuilt-src\lib\cmake\Dawn\DawnTargets.cmake'
    $ciLog = '/usr/local/lib/android/sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/28/liblog.so'
    if (Test-Path -LiteralPath $dawnTargets) {
        $export = [IO.File]::ReadAllText($dawnTargets)
        if ($export.Contains($ciLog)) {
            $beforeHash = (Get-FileHash -LiteralPath $dawnTargets -Algorithm SHA256).Hash.ToLowerInvariant()
            [IO.File]::WriteAllText($dawnTargets, $export.Replace($ciLog, 'log'), [Text.UTF8Encoding]::new($false))
            [ordered]@{ adaptation = 'Dawn imported Android liblog path: Linux CI absolute path to portable log';
                file = [IO.Path]::GetFullPath($dawnTargets); before_sha256 = $beforeHash;
                after_sha256 = (Get-FileHash -LiteralPath $dawnTargets -Algorithm SHA256).Hash.ToLowerInvariant();
                archive_unchanged = $true; native_library_unchanged = $true } |
                ConvertTo-Json | Set-Content -LiteralPath (Join-Path $BuildDirectory 'dawn-export-normalization.json') -Encoding utf8
            & $CMakePath @flags 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $LogPath -Append
            $result = $LASTEXITCODE
            if ($result -ne 0) { throw "Native reconfigure failed ($result), see $LogPath" }
        }
    }
    if ($Build) {
        $buildFlags = @('--build', (AsCMake $BuildDirectory), '--parallel', "$BuildJobs")
        if ($Target) { $buildFlags += @('--target', $Target) }
        & $CMakePath @buildFlags 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath ($LogPath + '.build.log')
        $result = $LASTEXITCODE
        if ($result -ne 0) { throw "Native build failed ($result), see $LogPath.build.log" }
        $library = Join-Path $BuildDirectory 'libmain.so'
        if (-not $RuntimeOnly -and (Test-Path -LiteralPath $library -PathType Leaf)) {
            $reader = Join-Path $NdkRoot 'toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-readelf.exe'
            $elfHeader = (& $reader --file-header $library 2>&1 | Out-String)
            if ($LASTEXITCODE -ne 0 -or $elfHeader -notmatch 'Machine:\s+AArch64' -or $elfHeader -notmatch 'Class:\s+ELF64') {
                throw 'Native product ELF64/AArch64 validation failed'
            }
            $exports = (& $reader --dyn-syms $library 2>&1 | Out-String)
            if ($LASTEXITCODE -ne 0 -or $exports -notmatch '\bSDL_main\b') { throw 'Native product SDL_main export missing' }
            $cache = [IO.File]::ReadAllText((Join-Path $BuildDirectory 'CMakeCache.txt'))
            $e2FlagVerified = $cache -match '(?m)^CMAKE_CXX_FLAGS:STRING=.*-DNSMBW_DISC_REVISION_E2=1'
            if ($DiscRevision -eq 'E2' -and -not $e2FlagVerified) { throw 'E2 compile flag missing from configured cache' }
            if ($DiscRevision -eq 'E1' -and $e2FlagVerified) { throw 'E1 build unexpectedly enables the E2 compile flag' }
            $dol = Join-Path $PreparedDirectory 'game\main.dol'
            if (-not (Test-Path -LiteralPath $dol -PathType Leaf)) { throw "Prepared DOL missing: $dol" }
            $graph = Join-Path (Split-Path -Parent $RuntimeRoot) 'generated\build_shards\shards.cmake'
            [ordered]@{ schema = 1; status = 'built'; library_path = [IO.Path]::GetFullPath($library);
                library_sha256 = (Get-FileHash -LiteralPath $library -Algorithm SHA256).Hash.ToLowerInvariant();
                bytes = (Get-Item -LiteralPath $library).Length; architecture = 'AArch64'; elf_class = 'ELF64';
                source_commit = $sourceCommit; disc_id = 'SMNE01'; disc_revision = $(if ($DiscRevision -eq 'E2') {2} else {1});
                dol_sha256 = (Get-FileHash -LiteralPath $dol -Algorithm SHA256).Hash.ToLowerInvariant();
                prepared_directory = $PreparedDirectory;
                translated_graph_sha256 = (Get-FileHash -LiteralPath $graph -Algorithm SHA256).Hash.ToLowerInvariant();
                e2_compile_flag_verified = $e2FlagVerified;
                sdl_main_export_verified = $true; ndk = '29.0.14206865';
                android_api = 30; configure_log = [IO.Path]::GetFullPath($LogPath);
                build_log = [IO.Path]::GetFullPath($LogPath + '.build.log');
                validation = 'Host native build and ELF/export checks only; no device boot or gameplay claim.' } |
                ConvertTo-Json | Set-Content -LiteralPath (Join-Path $BuildDirectory 'native-build-receipt.json') -Encoding utf8
        }
    }
} finally {
    $env:PATH = $savedPath
}
