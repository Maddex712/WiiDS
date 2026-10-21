# Start the game on the phone (launcher -> Play), wait, take a screenshot and show the runtime's console
# log from that run, filtered by -Pattern (or its tail).
[CmdletBinding()]
param(
    [int]$Seconds = 30,
    [string]$Name = 'android-run',
    [string]$Pattern = '',
    [int]$Tail = 40
)

$adb = 'C:\Users\Wither\AppData\Local\Android\Sdk\platform-tools\adb.exe'
$logs = 'E:\NSMBWPort\build-nsmbw\logs'
$ErrorActionPreference = 'Continue'
# A game left running from an earlier start would keep the launcher (and its PLAY button) away.
& $adb shell am force-stop com.wither.nsmbw
& $adb logcat -c
& $adb shell am start -n com.wither.nsmbw/.LauncherActivity 2>&1 | Out-Null
Start-Sleep -Seconds 2
& 'E:\NSMBWPort\tools\android-ui.ps1' -Tap 'PLAY' -Wait $Seconds
cmd /c "`"$adb`" exec-out screencap -p > `"$logs\$Name.png`""
& $adb logcat -d 2>&1 | ForEach-Object { "$_" } | Out-File -LiteralPath (Join-Path $logs "$Name-logcat.txt") -Encoding utf8
$gamePid = & $adb shell pidof com.wither.nsmbw:game
"game process: $(if ($gamePid) { "running (pid $gamePid)" } else { 'not running' })"
$latest = (& $adb shell 'ls -t /sdcard/Android/data/com.wither.nsmbw/files/Logs | head -1').Trim()
"log: $latest"
$console = "/sdcard/Android/data/com.wither.nsmbw/files/Logs/$latest/console.log"
& $adb shell "cat $console" 2>&1 | ForEach-Object { "$_" } | Out-File -LiteralPath (Join-Path $logs "$Name-console.txt") -Encoding utf8
$lines = Get-Content (Join-Path $logs "$Name-console.txt")
if ($Pattern) {
    $lines | Select-String -Pattern $Pattern | ForEach-Object { $_.Line.Substring(0, [Math]::Min(240, $_.Line.Length)) }
} else {
    $lines | Select-Object -Last $Tail | ForEach-Object { $_.Substring(0, [Math]::Min(240, $_.Length)) }
}
