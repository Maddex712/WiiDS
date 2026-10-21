# Backtrace a thread of the running Android game with lldb, symbolized against the unstripped
# libmain.so. Needs lldb-server running as the app (adb shell run-as com.wither.nsmbw ./lldb-server
# platform --server --listen unix-abstract:///com.wither.nsmbw-0/platform.sock).
#   android-backtrace.ps1 -Thread 16      (lldb thread index; default: the first SDLThread)
[CmdletBinding()]
param([int]$Thread = 0, [int]$Frames = 40)

$adb = 'C:\Users\Wither\AppData\Local\Android\Sdk\platform-tools\adb.exe'
$bin = 'C:\Users\Wither\AppData\Local\Android\Sdk\ndk\29.0.14206865\toolchains\llvm\prebuilt\windows-x86_64\bin'
$lib = 'E:\NSMBWPort\wiicompiled\android-build\libmain.so'
$out = 'E:\NSMBWPort\build-nsmbw\logs\android-backtrace.txt'
$ErrorActionPreference = 'Continue'

$gamePid = (& $adb shell pidof com.wither.nsmbw:game)
if (-not $gamePid) { 'game process is not running'; return }
$gamePid = $gamePid.Trim()

# libmain.so's load base: the read-only segment just below its (largest) executable segment.
$maps = & $adb shell "run-as com.wither.nsmbw sh -c 'grep base.apk /proc/$gamePid/maps'"
$rows = foreach ($line in $maps) {
    if ($line -match '^([0-9a-f]+)-([0-9a-f]+) (\S+) ([0-9a-f]+)') {
        [pscustomobject]@{ Start = [Convert]::ToUInt64($Matches[1], 16); End = [Convert]::ToUInt64($Matches[2], 16)
                           Perm = $Matches[3]; Offset = [Convert]::ToUInt64($Matches[4], 16) }
    }
}
$text = $rows | Where-Object { $_.Perm -eq 'r-xp' } | Sort-Object { $_.End - $_.Start } -Descending | Select-Object -First 1
$first = $rows | Where-Object { $_.Perm -eq 'r--p' -and $_.Start -lt $text.Start } | Sort-Object Start -Descending | Select-Object -First 1
$base = $first.Start
'libmain.so base 0x{0:x}, pid {1}' -f $base, $gamePid

$cmds = @('settings set interpreter.stop-command-source-on-error false', 'platform select remote-android',
          'platform connect unix-abstract-connect:///com.wither.nsmbw-0/platform.sock', "attach $gamePid",
          "thread backtrace all -c $Frames", 'detach', 'quit') -join "`n"
$script = Join-Path $env:TEMP 'nsmbw-lldb-bt.txt'
[IO.File]::WriteAllText($script, $cmds + "`n", (New-Object Text.UTF8Encoding $false))
& (Join-Path $bin 'lldb.cmd') -b -s $script 2>&1 | ForEach-Object { "$_" } | Out-File -LiteralPath $out -Encoding utf8
$lines = Get-Content $out
$start = 0..($lines.Count - 1) | Where-Object { $lines[$_] -match '^\(lldb\) thread backtrace all' } | Select-Object -First 1
$sub = $lines[$start..($lines.Count - 1)]
$header = if ($Thread -gt 0) { "thread #$Thread," } else { "name = 'SDLThread'" }
$i = 0..($sub.Count - 1) | Where-Object { $sub[$_] -match [regex]::Escape($header) } | Select-Object -First 1
if ($null -eq $i) { "thread not found ($header)"; return }
$j = 0..($sub.Count - 1) | Where-Object { $_ -gt $i -and $sub[$_] -match '^\s*\*?\s*thread #\d+,' } | Select-Object -First 1
if ($null -eq $j) { $j = $sub.Count }
$sub[$i]
foreach ($frame in $sub[($i + 1)..($j - 1)]) {
    if ($frame -notmatch 'frame #(\d+): 0x([0-9a-f]+)\s*(.*)$') { continue }
    $index = [int]$Matches[1]; $address = [Convert]::ToUInt64($Matches[2], 16); $rest = $Matches[3]
    if ($address -ge $base -and $address -lt $base + 0x10000000) {
        $vaddr = $address - $base - $(if ($index -eq 0) { 0 } else { 4 })
        $sym = & (Join-Path $bin 'llvm-symbolizer.exe') --obj=$lib --functions=linkage --inlining=false ('0x{0:x}' -f $vaddr)
        $where = ($sym | Select-Object -Skip 1 -First 1) -replace '^.*/(wiicompiled|NSMBWPort)/', ''
        '#{0,-2} {1}  {2}' -f $index, ($sym | Select-Object -First 1), $where
    } else {
        '#{0,-2} {1}' -f $index, $rest
    }
}
