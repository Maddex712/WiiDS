# Drive the phone's UI over adb for testing: tap elements by their text (or content description) and
# list what is on screen, using uiautomator's view dump.
#   android-ui.ps1 -Tap 'Import Game Folder…' -Wait 3 -List
[CmdletBinding()]
param(
    [string[]]$Tap = @(),
    [int]$Wait = 2,
    [switch]$List
)

$adb = 'C:\Users\Wither\AppData\Local\Android\Sdk\platform-tools\adb.exe'
$ErrorActionPreference = 'Continue'

function Get-UiXml {
    & $adb shell uiautomator dump /sdcard/nsmbw-ui.xml 2>&1 | Out-Null
    return ((& $adb shell cat /sdcard/nsmbw-ui.xml) -join '')
}

foreach ($label in $Tap) {
    $xml = Get-UiXml
    $pattern = '(?:text|content-desc)="' + [regex]::Escape($label) + '"[^>]*?bounds="\[([0-9]+),([0-9]+)\]\[([0-9]+),([0-9]+)\]"'
    $m = [regex]::Match($xml, $pattern)
    if (-not $m.Success) { "not on screen: $label"; continue }
    $x = [int](([int]$m.Groups[1].Value + [int]$m.Groups[3].Value) / 2)
    $y = [int](([int]$m.Groups[2].Value + [int]$m.Groups[4].Value) / 2)
    & $adb shell input tap $x $y
    "tapped '$label' at $x,$y"
    Start-Sleep -Seconds $Wait
}

if ($List) {
    $xml = Get-UiXml
    $texts = [regex]::Matches($xml, '(?:text|content-desc)="([^"]+)"') | ForEach-Object { $_.Groups[1].Value } |
        Where-Object { $_ } | Select-Object -Unique
    'on screen: ' + ($texts -join ' | ')
}
