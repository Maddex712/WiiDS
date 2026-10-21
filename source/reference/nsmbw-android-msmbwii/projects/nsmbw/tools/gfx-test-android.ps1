# Start the game on the phone with graphics debug flags (debug.nsmbw.gfx, see runtime/src/main.cpp),
# screenshot it after -Seconds and save a crop of the title-screen characters for comparison.
#   gfx-test-android.ps1 -Flags 'noextract,sext' -Name s26-noextract
[CmdletBinding()]
param(
    [string]$Flags = '',
    [string]$Flags2 = '',
    [string]$Name = 'gfx-test',
    [int]$Seconds = 40,
    # x, y, width, height of the crop, in screenshot pixels
    [int[]]$Crop = @(1000, 820, 1100, 380)
)

$adb = 'C:\Users\Wither\AppData\Local\Android\Sdk\platform-tools\adb.exe'
$logs = 'E:\NSMBWPort\build-nsmbw\logs'
# setprop cannot store an empty value; a single space reads back as no flags.
& $adb shell setprop debug.nsmbw.gfx $(if ($Flags) { "'$Flags'" } else { "' '" })
& $adb shell setprop debug.nsmbw.gfx2 $(if ($Flags2) { "'$Flags2'" } else { "' '" })
& 'E:\NSMBWPort\tools\run-android.ps1' -Seconds $Seconds -Name $Name -Pattern 'debug.nsmbw|Graphics flags|WebGPU error|workaround' |
    Select-Object -First 12

Add-Type -AssemblyName System.Drawing
$bmp = [System.Drawing.Bitmap]::FromFile((Join-Path $logs "$Name.png"))
$rect = [System.Drawing.Rectangle]::new($Crop[0], $Crop[1], $Crop[2], $Crop[3])
$part = $bmp.Clone($rect, $bmp.PixelFormat)
$part.Save((Join-Path $logs "$Name-crop.png"))
$part.Dispose(); $bmp.Dispose()
"crop: $(Join-Path $logs "$Name-crop.png")"
