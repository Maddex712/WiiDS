# Launch the NSMBW build, wait, capture its window contents to a PNG (PrintWindow, works when the
# window is covered), then stop it. Used to see what the game renders during boot bring-up.
[CmdletBinding()]
param(
    [int[]]$AtSeconds = @(20),
    [string]$Name = 'shot',
    # Scripted Wii Remote input for the run (runtime reads NSMBW_INPUT_SCRIPT), e.g. '6000:A:200'.
    [string]$InputScript = ''
)
if ($InputScript) { $env:NSMBW_INPUT_SCRIPT = $InputScript } else { Remove-Item Env:NSMBW_INPUT_SCRIPT -ErrorAction SilentlyContinue }

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class Win {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int cmd);
}
'@

$buildDir = 'E:\NSMBWPort\wiicompiled\native-build'
$logDir = 'E:\NSMBWPort\build-nsmbw\logs'
if (-not (Test-Path -LiteralPath (Join-Path $buildDir 'WiiCompiled.exe'))) {
    Write-Error 'WiiCompiled.exe is missing (did the last link fail?)'
    return
}
Get-Process WiiCompiled -ErrorAction SilentlyContinue | Where-Object { $_.Path -like 'E:\NSMBWPort\*' } |
    ForEach-Object { Stop-Process -Id $_.Id -Force }
$p = Start-Process -FilePath (Join-Path $buildDir 'WiiCompiled.exe') -WorkingDirectory $buildDir -PassThru `
    -RedirectStandardOutput (Join-Path $logDir "$Name.stdout.txt") -RedirectStandardError (Join-Path $logDir "$Name.stderr.txt")
$start = Get-Date
foreach ($t in $AtSeconds) {
    while (((Get-Date) - $start).TotalSeconds -lt $t -and -not $p.HasExited) { Start-Sleep -Milliseconds 250 }
    if ($p.HasExited) { "process exited early (code $($p.ExitCode))"; break }
    $p.Refresh()
    $hwnd = $p.MainWindowHandle
    if ($hwnd -eq [IntPtr]::Zero) { "no main window at ${t}s"; continue }
    # D3D12 flip-model swapchains are not visible to PrintWindow (it returns the GDI background), so
    # bring the window forward and copy what the compositor actually shows.
    [void][Win]::ShowWindow($hwnd, 9)  # SW_RESTORE
    [void][Win]::SetForegroundWindow($hwnd)
    Start-Sleep -Milliseconds 400
    $r = New-Object Win+RECT
    [void][Win]::GetWindowRect($hwnd, [ref]$r)
    $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size $w, $h))
    $g.Dispose()
    $out = Join-Path $logDir "$Name-${t}s.png"
    $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    "captured ${w}x${h} at ${t}s -> $out"
}
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
