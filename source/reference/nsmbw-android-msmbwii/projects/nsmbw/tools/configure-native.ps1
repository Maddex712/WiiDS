# Configure the NSMBW native build exactly like WiiCompiled's Launcher/LocalBuild.ps1 does:
# same canonical flags (NativeBuildFlags.ps1), the pinned Dependencies tree as FetchContent sources,
# the portable llvm-mingw/CMake/Ninja toolchain, and a scrubbed PATH so no other compiler leaks in.
[CmdletBinding()]
param(
    [int]$TranslatedJobs = 6,
    [string]$BuildDirectory = 'E:\NSMBWPort\wiicompiled\native-build',
    [string]$LogPath = 'E:\NSMBWPort\build-nsmbw\logs\configure.log'
)

$ErrorActionPreference = 'Stop'
$workspace = 'E:\NSMBWPort\wiicompiled'
$toolchain = 'E:\NSMBWPort\toolchain'
. (Join-Path $workspace 'Launcher\NativeBuildFlags.ps1')

$bin = Join-Path $toolchain 'llvm-mingw\bin'
$env:PATH = Get-MkwToolchainPath $toolchain

$configure = Get-MkwNativeConfigureArguments `
    -SourceDirectory (Join-Path $workspace 'runtime') -BuildDirectory $BuildDirectory `
    -Ninja (Join-Path $toolchain 'Ninja\ninja.exe') `
    -CCompiler (Join-Path $bin 'x86_64-w64-mingw32-clang.exe') `
    -CxxCompiler (Join-Path $bin 'x86_64-w64-mingw32-clang++.exe') `
    -ResourceCompiler (Join-Path $bin 'x86_64-w64-mingw32-windres.exe') `
    -DependenciesDirectory (Join-Path $workspace 'Dependencies') `
    -AdditionalArguments @("-DMKW_TRANSLATED_COMPILE_JOBS=$TranslatedJobs")

# Windows PowerShell 5.1 turns a native program's stderr lines into error records; CMake prints
# ordinary status there, so run it with 'Continue' and keep the log as plain text.
$ErrorActionPreference = 'Continue'
& (Join-Path $toolchain 'CMake\bin\cmake.exe') @configure 2>&1 |
    ForEach-Object { "$_" } | Out-File -LiteralPath $LogPath -Encoding utf8
exit $LASTEXITCODE
