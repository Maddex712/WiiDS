# Build targets of the NSMBW native build with the portable toolchain on a scrubbed PATH.
[CmdletBinding()]
param(
    [string[]]$Target = @('WiiCompiled'),
    [int]$Jobs = 10,
    [string]$BuildDirectory = 'E:\NSMBWPort\wiicompiled\native-build',
    [string]$LogPath = 'E:\NSMBWPort\build-nsmbw\logs\build.log',
    [switch]$KeepGoing
)

$ErrorActionPreference = 'Stop'
$toolchain = 'E:\NSMBWPort\toolchain'
. 'E:\NSMBWPort\wiicompiled\Launcher\NativeBuildFlags.ps1'
$env:PATH = Get-MkwToolchainPath $toolchain

$arguments = @('--build', $BuildDirectory)
foreach ($t in $Target) { $arguments += @('--target', $t) }
$arguments += @('--parallel', $Jobs)
if ($KeepGoing) { $arguments += @('--', '-k', '0') }

$ErrorActionPreference = 'Continue'
& (Join-Path $toolchain 'CMake\bin\cmake.exe') @arguments 2>&1 |
    ForEach-Object { "$_" } | Out-File -LiteralPath $LogPath -Encoding utf8
exit $LASTEXITCODE
