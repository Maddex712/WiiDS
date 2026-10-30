# Builds the Brawl Recompiled. launcher (launcher\) with a portable .NET 8 SDK kept in toolchain\dotnet
# (downloaded once with Microsoft's dotnet-install script, like build-brawl.ps1 fetches llvm-mingw).
# Output: launcher\publish\Brawl Recompiled.exe (self-contained single file, no .NET install needed).
[CmdletBinding()] param([switch]$SkipPublish)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sdk = Join-Path $root 'toolchain\dotnet'
$dotnet = Join-Path $sdk 'dotnet.exe'
if (-not (Test-Path -LiteralPath $dotnet)) {
    Write-Host 'Downloading the portable .NET 8 SDK into toolchain\dotnet ...'
    $script = Join-Path $env:TEMP 'dotnet-install.ps1'
    Invoke-WebRequest -UseBasicParsing -Uri 'https://dot.net/v1/dotnet-install.ps1' -OutFile $script
    & $script -Channel 8.0 -InstallDir $sdk -NoPath
    if (-not (Test-Path -LiteralPath $dotnet)) { throw 'dotnet SDK install failed' }
}
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$env:DOTNET_NOLOGO = '1'
$env:DOTNET_ROOT = $sdk
if ($SkipPublish) { exit 0 }
$proj = Join-Path $root 'launcher\BrawlRecompiled.Launcher.csproj'
& $dotnet publish $proj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -o (Join-Path $root 'launcher\publish')
if ($LASTEXITCODE -ne 0) { throw "dotnet publish failed ($LASTEXITCODE)" }
Write-Host "DONE: $root\launcher\publish\Brawl Recompiled.exe"
# The Linux launcher (launcher\linux: the same pages in Avalonia), packed by tools/package-linux.sh.
& $dotnet publish (Join-Path $root 'launcher\linux\BrawlRecompiled.Linux.csproj') -c Release -o (Join-Path $root 'launcher\linux\publish')
if ($LASTEXITCODE -ne 0) { throw "dotnet publish (Linux launcher) failed ($LASTEXITCODE)" }
Write-Host "DONE: $root\launcher\linux\publish\Brawl Recompiled"
