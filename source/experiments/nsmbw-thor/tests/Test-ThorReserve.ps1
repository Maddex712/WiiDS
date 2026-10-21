[CmdletBinding()]
param([string]$WorkspaceRoot=(Join-Path $PSScriptRoot '..\..\..'),[string]$DeviceSerial='192.168.15.84:5555')
$ErrorActionPreference='Stop'
$WorkspaceRoot=[IO.Path]::GetFullPath($WorkspaceRoot)
$compiler=Join-Path $WorkspaceRoot '.tools\android-sdk\ndk\29.0.14206865\toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android30-clang++.cmd'
$adb=Join-Path $WorkspaceRoot '.tools\platform-tools\adb.exe'
$checkout=Join-Path $WorkspaceRoot 'reference\nsmbw-android-e1'
$source=Join-Path $checkout 'runtime\src\thor_reserve.cpp'
$out=Join-Path $WorkspaceRoot 'build\nsmbw-thor\e1\reserve-test'
[IO.Directory]::CreateDirectory($out)|Out-Null
$binary=Join-Path $out 'thor_reserve_test'
$remote='/data/local/tmp/thor-nsmb-reserve-'+[Guid]::NewGuid().ToString()
& $compiler -std=c++20 -O2 -static-libstdc++ "-I$PSScriptRoot\reserve-mock" "-I$checkout\runtime\include" (Join-Path $PSScriptRoot 'thor_reserve_test.cpp') $source -o $binary
if($LASTEXITCODE -ne 0){throw 'Reserve test compilation failed'}
$results=@()
try {
 & $adb -s $DeviceSerial shell mkdir $remote
 & $adb -s $DeviceSerial push $binary "$remote/test"
 & $adb -s $DeviceSerial shell chmod 700 "$remote/test"
 foreach($mode in @('run','reload','sibling','reload','corrupt')) {
  $results+=@(& $adb -s $DeviceSerial shell "THOR_RESERVE_TEST_ROOT=$remote $remote/test $mode" 2>&1|ForEach-Object{"$_"})
  if($LASTEXITCODE -ne 0){throw "Reserve $mode failed: $results"}
 }
} finally {
 if($remote -match '^/data/local/tmp/thor-nsmb-reserve-[0-9a-f-]{36}$') { & $adb -s $DeviceSerial shell rm -r $remote }
}
@{status='passed';kind='actual_reserve_source_arm64_mock_guest_calls_and_memory_real_sidecar_io';stdout=$results;source_sha256=(Get-FileHash $source).Hash.ToLowerInvariant();limits=@('No game Activity or controls changed.','Mock actor constructor and memory do not prove live game behavior.')}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $out 'verification.json')
$results
