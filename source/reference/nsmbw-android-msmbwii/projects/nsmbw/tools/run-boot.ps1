# Launch the NSMBW build once, collect its log and crash report, and always stop it afterwards so a
# lingering fatal-error popup never keeps WiiCompiled.exe locked for the next link.
[CmdletBinding()]
param(
    [int]$Seconds = 40,
    [string]$Name = 'boot',
    [int]$TailLines = 45,
    [string]$Exclude = 'aurora::gpu\]|maxTexture|maxDynamic|maxStorage|minUniform|minStorage|API: |Device: |Driver: |\[os\] (PPCMt|EXI|PPCDisable)'
)

$buildDir = 'E:\NSMBWPort\wiicompiled\native-build'
$logDir = 'E:\NSMBWPort\build-nsmbw\logs'

Get-Process WiiCompiled -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -like 'E:\NSMBWPort\*' } |
    ForEach-Object { Stop-Process -Id $_.Id -Force }

$stdout = Join-Path $logDir "$Name.stdout.txt"
$stderr = Join-Path $logDir "$Name.stderr.txt"
$p = Start-Process -FilePath (Join-Path $buildDir 'WiiCompiled.exe') -WorkingDirectory $buildDir -PassThru `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr
$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline -and -not $p.HasExited) { Start-Sleep -Milliseconds 500 }
if ($p.HasExited) { "pid $($p.Id) exited with code $($p.ExitCode)" } else { "pid $($p.Id) still running after $Seconds s; stopping it" }
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue

$crash = Get-ChildItem 'E:\NSMBWPort\UserData\Logs' -Recurse -Filter 'crash_*.txt' -ErrorAction SilentlyContinue |
    Where-Object { $_.Directory.Name -like "*pid$($p.Id)" }
if ($crash) { 'crash report: ' + ($crash | ForEach-Object FullName) } else { 'no crash report' }
"=== stderr tail ($Name)"
Get-Content $stderr -ErrorAction SilentlyContinue | Select-String -NotMatch $Exclude |
    Select-Object -Last $TailLines | ForEach-Object { $_.Line.Substring(0, [Math]::Min(230, $_.Line.Length)) }
