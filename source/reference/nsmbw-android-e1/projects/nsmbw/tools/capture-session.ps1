# Like capture-window.ps1, but follows the game across its own restarts (a LAN room restarts the
# game into the session): every capture finds whichever WiiCompiled window is running now, and the
# run ends by stopping all of them. Logs: the runtime's own console.log in UserData\Logs\<run>.
[CmdletBinding()]
param(
    [double[]]$AtSeconds = @(20),
    [string]$Name = 'session',
    [string]$InputScript = '',
    [string]$SessionScript = ''
)
if ($InputScript) { $env:NSMBW_INPUT_SCRIPT = $InputScript } else { $env:NSMBW_INPUT_SCRIPT = $null }
if ($SessionScript) { $env:NSMBW_SESSION_SCRIPT = $SessionScript } else { $env:NSMBW_SESSION_SCRIPT = $null }

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class Win2 {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int cmd);
}
'@

$buildDir = 'E:\NSMBWPort\wiicompiled\native-build'
$logDir = 'E:\NSMBWPort\build-nsmbw\logs'
function Get-Game { Get-Process WiiCompiled -ErrorAction SilentlyContinue | Where-Object { $_.Path -like 'E:\NSMBWPort\*' } }
Get-Game | ForEach-Object { Stop-Process -Id $_.Id -Force }
$p = Start-Process -FilePath (Join-Path $buildDir 'WiiCompiled.exe') -WorkingDirectory $buildDir -PassThru
$start = Get-Date
foreach ($t in $AtSeconds) {
    while (((Get-Date) - $start).TotalSeconds -lt $t) { Start-Sleep -Milliseconds 250 }
    $game = Get-Game | Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } | Select-Object -First 1
    if (-not $game) { "no game window at ${t}s"; continue }
    $hwnd = $game.MainWindowHandle
    [void][Win2]::ShowWindow($hwnd, 9)
    [void][Win2]::SetForegroundWindow($hwnd)
    Start-Sleep -Milliseconds 400
    $r = New-Object Win2+RECT
    [void][Win2]::GetWindowRect($hwnd, [ref]$r)
    $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size $w, $h))
    $g.Dispose()
    $out = Join-Path $logDir "$Name-${t}s.png"
    $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    "captured pid $($game.Id) ${w}x${h} at ${t}s -> $out"
}
Get-Game | ForEach-Object { Stop-Process -Id $_.Id -Force }
