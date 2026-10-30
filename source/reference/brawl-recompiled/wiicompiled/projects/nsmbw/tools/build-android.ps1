# Build targets of the Android (arm64-v8a) native build configured by configure-android.ps1.
[CmdletBinding()]
param(
    [string[]]$Target = @('WiiCompiled'),
    [int]$Jobs = 10,
    [string]$BuildDirectory = 'E:\NSMBWPort\wiicompiled\android-build',
    [string]$LogPath = 'E:\NSMBWPort\build-nsmbw\logs\build-android.log',
    [switch]$KeepGoing
)

$ErrorActionPreference = 'Stop'
$toolchain = 'E:\NSMBWPort\toolchain'
$env:PATH = "$toolchain\CMake\bin;$toolchain\Ninja;$env:SystemRoot\System32;$env:SystemRoot"

$arguments = @('--build', $BuildDirectory)
foreach ($t in $Target) { $arguments += @('--target', $t) }
$arguments += @('--parallel', $Jobs)
if ($KeepGoing) { $arguments += @('--', '-k', '0') }

$ErrorActionPreference = 'Continue'
& (Join-Path $toolchain 'CMake\bin\cmake.exe') @arguments 2>&1 |
    ForEach-Object { "$_" } | Out-File -LiteralPath $LogPath -Encoding utf8
exit $LASTEXITCODE
