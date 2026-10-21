# Launch the NSMBW build and play it from the keyboard: a timeline of key presses sent with
# SendInput (hardware scancodes, as a real keyboard would), with window captures in between. Tests
# the virtual Wii Remote end to end. The game window is brought to the foreground for every action.
#   -Keys '6000:A:150,31000:X:150,50000:RIGHT:4000,51000:X:300'   time:KEY[+KEY]:holdMs
#   -AtSeconds 20,40                                                  capture times
[CmdletBinding()]
param(
    [string]$Keys = '',
    [double[]]$AtSeconds = @(),
    [string]$Name = 'keys',
    [int]$Seconds = 60
)
Remove-Item Env:NSMBW_INPUT_SCRIPT -ErrorAction SilentlyContinue

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class KeyWin {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)] struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Explicit)] struct UNION { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
    [StructLayout(LayoutKind.Sequential)] struct INPUT { public uint type; public UNION u; }
    [DllImport("user32.dll", SetLastError = true)] static extern uint SendInput(uint n, INPUT[] inputs, int size);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int cmd);
    public static uint Key(int scan, bool up) {
        const uint SCANCODE = 0x0008, KEYUP = 0x0002, EXTENDED = 0x0001;
        var input = new INPUT { type = 1 };
        input.u.ki.wScan = (ushort)(scan & 0xFF);
        input.u.ki.dwFlags = SCANCODE | (up ? KEYUP : 0) | ((scan & 0xE000) == 0xE000 ? EXTENDED : 0);
        return SendInput(1, new[] { input }, Marshal.SizeOf(typeof(INPUT)));
    }
}
'@

$scan = @{
    'A' = 0x1E; 'X' = 0x2D; 'Z' = 0x2C; 'C' = 0x2E; 'Q' = 0x10; 'E' = 0x12; 'ENTER' = 0x1C; 'SPACE' = 0x39
    'TAB' = 0x0F; 'MINUS' = 0x0C; 'LSHIFT' = 0x2A
    'LEFT' = 0xE04B; 'RIGHT' = 0xE04D; 'UP' = 0xE048; 'DOWN' = 0xE050
}

# Expand the timeline into sorted key-down/key-up actions plus captures.
$actions = New-Object System.Collections.Generic.List[object]
foreach ($item in ($Keys -split ',' | Where-Object { $_ })) {
    $parts = $item.Split(':')
    $at = [int]$parts[0]; $hold = if ($parts.Count -ge 3) { [int]$parts[2] } else { 150 }
    foreach ($k in $parts[1].Split('+')) {
        if (-not $scan.ContainsKey($k)) { Write-Error "unknown key $k"; return }
        $actions.Add([pscustomobject]@{ At = $at; Kind = 'down'; Scan = $scan[$k]; Text = "$k down" })
        $actions.Add([pscustomobject]@{ At = $at + $hold; Kind = 'up'; Scan = $scan[$k]; Text = "$k up" })
    }
}
foreach ($t in $AtSeconds) { $actions.Add([pscustomobject]@{ At = [int]($t * 1000); Kind = 'shot'; Scan = 0; Text = "${t}s" }) }
$actions = $actions | Sort-Object At, @{ Expression = { if ($_.Kind -eq 'up') { 0 } else { 1 } } }

$buildDir = 'E:\NSMBWPort\wiicompiled\native-build'
$logDir = 'E:\NSMBWPort\build-nsmbw\logs'
if (-not (Test-Path -LiteralPath (Join-Path $buildDir 'WiiCompiled.exe'))) { Write-Error 'WiiCompiled.exe is missing'; return }
Get-Process WiiCompiled -ErrorAction SilentlyContinue | Where-Object { $_.Path -like 'E:\NSMBWPort\*' } |
    ForEach-Object { Stop-Process -Id $_.Id -Force }
$p = Start-Process -FilePath (Join-Path $buildDir 'WiiCompiled.exe') -WorkingDirectory $buildDir -PassThru `
    -RedirectStandardOutput (Join-Path $logDir "$Name.stdout.txt") -RedirectStandardError (Join-Path $logDir "$Name.stderr.txt")
$start = Get-Date

function Get-GameWindow {
    $p.Refresh()
    $hwnd = $p.MainWindowHandle
    if ($hwnd -ne [IntPtr]::Zero -and [KeyWin]::GetForegroundWindow() -ne $hwnd) {
        [void][KeyWin]::ShowWindow($hwnd, 9)
        [void][KeyWin]::SetForegroundWindow($hwnd)
        Start-Sleep -Milliseconds 120
    }
    return $hwnd
}

foreach ($a in $actions) {
    while (((Get-Date) - $start).TotalMilliseconds -lt $a.At -and -not $p.HasExited) { Start-Sleep -Milliseconds 5 }
    if ($p.HasExited) { "process exited early (code $($p.ExitCode))"; break }
    $hwnd = Get-GameWindow
    if ($hwnd -eq [IntPtr]::Zero) { "no window for $($a.Text)"; continue }
    if ($a.Kind -eq 'shot') {
        $r = New-Object KeyWin+RECT
        [void][KeyWin]::GetWindowRect($hwnd, [ref]$r)
        $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
        $bmp = New-Object System.Drawing.Bitmap $w, $h
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size $w, $h))
        $g.Dispose()
        $out = Join-Path $logDir "$Name-$($a.Text).png"
        $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
        "captured $($a.Text) -> $out"
    } else {
        [void][KeyWin]::Key($a.Scan, $a.Kind -eq 'up')
    }
}
while (((Get-Date) - $start).TotalSeconds -lt $Seconds -and -not $p.HasExited) { Start-Sleep -Milliseconds 250 }
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
