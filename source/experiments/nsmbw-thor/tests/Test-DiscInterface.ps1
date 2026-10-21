[CmdletBinding()]
param(
    [string]$WorkspaceRoot = (Join-Path $PSScriptRoot '..\..\..'),
    [string]$DeviceSerial = '192.168.15.84:5555'
)
$ErrorActionPreference = 'Stop'
$WorkspaceRoot = [IO.Path]::GetFullPath($WorkspaceRoot)
$compiler = Join-Path $WorkspaceRoot '.tools\android-sdk\ndk\29.0.14206865\toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android30-clang++.cmd'
$adb = Join-Path $WorkspaceRoot '.tools\platform-tools\adb.exe'
$repo = Join-Path $WorkspaceRoot 'reference\nsmbw-android-e1'
$source = Join-Path $repo 'runtime\src\hle\storage\disc_interface.cpp'
$header = Join-Path $repo 'runtime\include\hle\disc_interface.h'
$include = Join-Path $repo 'runtime\include'
$test = Join-Path $PSScriptRoot 'disc_interface_test.cpp'
$mock = Join-Path $PSScriptRoot 'disc-mock'
$folder = Join-Path $WorkspaceRoot 'build\nsmbw-thor\disc-interface-test'
[IO.Directory]::CreateDirectory($folder) | Out-Null
$binary = Join-Path $folder 'disc_interface_test'
$remote = '/data/local/tmp/thor-nsmb-di-test-' + [Guid]::NewGuid().ToString()
if ($remote -notmatch '^/data/local/tmp/thor-nsmb-di-test-[0-9a-f-]{36}$') { throw 'Unexpected test destination' }
& $compiler -std=c++20 -O2 -static-libstdc++ "-I$mock" "-I$include" $test $source -o $binary
if ($LASTEXITCODE -ne 0) { throw "DI test compilation failed ($LASTEXITCODE)" }
$created = $false
$stdout = @()
$runCode = -1
$cleanupVerified = $false
try {
    & $adb -s $DeviceSerial shell mkdir $remote
    if ($LASTEXITCODE -ne 0) { throw 'Test directory creation failed' }
    $created = $true
    & $adb -s $DeviceSerial push $binary "$remote/test"
    if ($LASTEXITCODE -ne 0) { throw 'Test push failed' }
    & $adb -s $DeviceSerial shell chmod 700 "$remote/test"
    if ($LASTEXITCODE -ne 0) { throw 'Test chmod failed' }
    $stdout = @(& $adb -s $DeviceSerial shell "$remote/test" "$remote/fixture" 2>&1 | ForEach-Object { "$_" })
    $runCode = $LASTEXITCODE
} finally {
    if ($created) {
        # Every removal is an exact path inside the checked unique test root.
        foreach ($suffix in @('/test','/fixture/disc/bca.bin','/fixture/sys/boot.bin')) {
            & $adb -s $DeviceSerial shell rm -f "$remote$suffix"
            if ($LASTEXITCODE -ne 0) { throw "Cannot remove exact test file $remote$suffix" }
        }
        foreach ($suffix in @('/fixture/disc','/fixture/sys','/fixture','')) {
            & $adb -s $DeviceSerial shell rmdir "$remote$suffix"
            if ($LASTEXITCODE -ne 0) { throw "Cannot remove exact test directory $remote$suffix" }
        }
        & $adb -s $DeviceSerial shell test '!' -e $remote
        $cleanupVerified = $LASTEXITCODE -eq 0
    }
}
$mocks = @(Get-ChildItem -LiteralPath $mock -File -Recurse | ForEach-Object {
    @{ name = $_.Name; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
})
$receipt = @{
    status = if ($runCode -eq 0 -and $cleanupVerified) { 'passed' } else { 'failed' }
    test_kind = 'actual_disc_interface_source_with_real_files_and_mock_guest_memory_on_arm64'
    source_sha256 = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
    header_sha256 = (Get-FileHash -LiteralPath $header -Algorithm SHA256).Hash.ToLowerInvariant()
    test_sha256 = (Get-FileHash -LiteralPath $test -Algorithm SHA256).Hash.ToLowerInvariant()
    harness_sha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
    binary_sha256 = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant()
    mocks = $mocks
    exit_code = $runCode; stdout = $stdout; exact_remote_path = $remote
    cleanup_verified = $cleanupVerified
    limits = @('Arbitrary test metadata only; it does not match a real game verifier.', 'No app-data writes, Activity launch, game input or genuine disc metadata acquisition.', 'Guest RAM is mocked with strict bounds; no boot/rendering/gameplay or installed-app integration validation.')
}
$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $folder 'verification.json') -Encoding utf8
$stdout | Write-Output
if ($runCode -ne 0 -or -not $cleanupVerified) { throw 'DI test failed; see build/nsmbw-thor/disc-interface-test/verification.json' }
