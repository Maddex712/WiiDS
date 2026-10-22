param([switch]$Copy)
$ErrorActionPreference = 'Stop'
$handoffPath = Join-Path $PSScriptRoot 'CLAUDE_BOTTOM_SCREEN_HANDOFF.md'
$handoffText = [IO.File]::ReadAllText($handoffPath)
if ($Copy) {
    Set-Clipboard -Value $handoffText
    Write-Output "Copied the Galaxy bottom-screen handoff for Claude."
} else {
    Write-Output $handoffText
}
