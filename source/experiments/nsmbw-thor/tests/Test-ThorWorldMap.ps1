[CmdletBinding()]
param(
    [string]$WorkspaceRoot = (Join-Path $PSScriptRoot '..\..\..'),
    [string]$DeviceSerial = '192.168.15.84:5555',
    [string]$SourceCheckout = 'reference\nsmbw-android-e1',
    [string]$OutputDirectory = 'build\nsmbw-thor\e1\world-map-test'
)
# Executes the real snapshot C++ against a read-only mock Memory API. No game
# executable, APK install, input injection, or Activity launch occurs.
$ErrorActionPreference = 'Stop'
$WorkspaceRoot = [IO.Path]::GetFullPath($WorkspaceRoot)
$compiler = Join-Path $WorkspaceRoot '.tools\android-sdk\ndk\29.0.14206865\toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android30-clang++.cmd'
$adb = Join-Path $WorkspaceRoot '.tools\platform-tools\adb.exe'
$checkout = [IO.Path]::GetFullPath((Join-Path $WorkspaceRoot $SourceCheckout))
$source = Join-Path $checkout 'runtime\src\thor_world_map.cpp'
$include = Join-Path $checkout 'runtime\include'
$test = Join-Path $PSScriptRoot 'thor_world_map_test.cpp'
$mock = Join-Path $PSScriptRoot 'mock'
$folder = [IO.Path]::GetFullPath((Join-Path $WorkspaceRoot $OutputDirectory))
[IO.Directory]::CreateDirectory($folder) | Out-Null
$binary = Join-Path $folder 'thor_world_map_test'
$remote = '/data/local/tmp/thor-nsmb-world-map-test-' + [Guid]::NewGuid().ToString()
if ($remote -notmatch '^/data/local/tmp/thor-nsmb-world-map-test-[0-9a-f-]{36}$') { throw 'Unexpected test destination' }
& $compiler -std=c++20 -O2 -static-libstdc++ "-I$mock" "-I$include" $test $source -o $binary
if ($LASTEXITCODE -ne 0) { throw "World-map test compilation failed ($LASTEXITCODE)" }
$pushed = $false
$stdout = @()
$runCode = -1
$cleanupVerified = $false
try {
    & $adb -s $DeviceSerial push $binary $remote
    if ($LASTEXITCODE -ne 0) { throw 'Test push failed' }
    $pushed = $true
    & $adb -s $DeviceSerial shell chmod 700 $remote
    if ($LASTEXITCODE -ne 0) { throw 'Test chmod failed' }
    $stdout = @(& $adb -s $DeviceSerial shell $remote 2>&1 | ForEach-Object { "$_" })
    $runCode = $LASTEXITCODE
} finally {
    if ($pushed) {
        & $adb -s $DeviceSerial shell rm $remote
        if ($LASTEXITCODE -ne 0) { throw "Could not remove exact test file: $remote" }
        & $adb -s $DeviceSerial shell test '!' -e $remote
        $cleanupVerified = $LASTEXITCODE -eq 0
    }
}
$receipt = @{
    status = if ($runCode -eq 0 -and $cleanupVerified) { 'passed' } else { 'failed' }
    test_kind = 'actual_world_map_source_with_mock_guest_memory_on_arm64'
    source_checkout = $checkout
    source_sha256 = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
    test_sha256 = (Get-FileHash -LiteralPath $test -Algorithm SHA256).Hash.ToLowerInvariant()
    binary_sha256 = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant()
    exit_code = $runCode; stdout = $stdout; exact_remote_path = $remote
    cleanup_verified = $cleanupVerified
    limits = @('Mock-memory tests do not validate live game addresses or gameplay.', 'No foreground Activity operations were performed.')
}
$receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $folder 'verification.json') -Encoding utf8
$stdout | Write-Output
if ($runCode -ne 0 -or -not $cleanupVerified) { throw "World-map test failed; see $folder\verification.json" }

