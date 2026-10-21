[CmdletBinding()]
param(
    [string]$WorkspaceRoot = (Join-Path $PSScriptRoot '..\..\..'),
    [string]$DeviceSerial = '192.168.15.84:5555'
)
# Runs actual IOS async entry points and the actual NAND callback dispatcher
# against a mock backend. No Activity, game data, save, config or inputs touched.
$ErrorActionPreference = 'Stop'
$WorkspaceRoot = [IO.Path]::GetFullPath($WorkspaceRoot)
$compiler = Join-Path $WorkspaceRoot '.tools\android-sdk\ndk\29.0.14206865\toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android30-clang++.cmd'
$adb = Join-Path $WorkspaceRoot '.tools\platform-tools\adb.exe'
$source = Join-Path $WorkspaceRoot 'reference\nsmbw-android\runtime\src\hle\ios.cpp'
$queueSource = Join-Path $WorkspaceRoot 'reference\nsmbw-android\runtime\src\hle\storage\nand_async.cpp'
$test = Join-Path $PSScriptRoot 'ios_async_test.cpp'
$mock = Join-Path $PSScriptRoot 'ios-mock'
$folder = Join-Path $WorkspaceRoot 'build\nsmbw-thor\ios-bootstrap-test'
[IO.Directory]::CreateDirectory($folder) | Out-Null
$queueText = [IO.File]::ReadAllText($queueSource)
$marker = '// Async NAND entry points run the sync call, queue the guest completion callback, and return.'
$cut = $queueText.IndexOf($marker, [StringComparison]::Ordinal)
if ($cut -lt 0 -or $queueText.IndexOf($marker,$cut+1,[StringComparison]::Ordinal) -ge 0) { throw 'Actual-source queue boundary changed' }
$queuePrefix = Join-Path $folder 'actual_nand_callback_prefix.cpp'
[IO.File]::WriteAllText($queuePrefix, $queueText.Substring(0,$cut), [Text.UTF8Encoding]::new($false))
$binary = Join-Path $folder 'ios_bootstrap_test'
$remote = '/data/local/tmp/thor-nsmb-ios-test-' + [Guid]::NewGuid().ToString()
if ($remote -notmatch '^/data/local/tmp/thor-nsmb-ios-test-[0-9a-f-]{36}$') { throw 'Unexpected test destination' }
& $compiler -std=c++20 -O2 -static-libstdc++ "-I$mock" $test $source $queuePrefix -o $binary
if ($LASTEXITCODE -ne 0) { throw "IOS bootstrap test compilation failed ($LASTEXITCODE)" }
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
    test_kind = 'actual_ios_source_and_actual_nand_callback_dispatch_with_mock_backend_on_arm64'
    source_sha256 = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
    queue_source_sha256 = (Get-FileHash -LiteralPath $queueSource -Algorithm SHA256).Hash.ToLowerInvariant()
    compiled_queue_prefix_sha256 = (Get-FileHash -LiteralPath $queuePrefix -Algorithm SHA256).Hash.ToLowerInvariant()
    queue_prefix_ends_before = $marker
    test_sha256 = (Get-FileHash -LiteralPath $test -Algorithm SHA256).Hash.ToLowerInvariant()
    binary_sha256 = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant()
    exit_code = $runCode; stdout = $stdout; exact_remote_path = $remote
    cleanup_verified = $cleanupVerified
    limits = @('Mock backends do not validate optical-drive metadata, game checks, boot, rendering or gameplay.', 'Fiber lifecycle is mocked; the production try-lock guard is reviewed separately.', 'No foreground Activity operations were performed.')
}
$receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $folder 'verification.json') -Encoding utf8
$stdout | Write-Output
if ($runCode -ne 0 -or -not $cleanupVerified) { throw 'IOS bootstrap test failed; see build/nsmbw-thor/ios-bootstrap-test/verification.json' }
