[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PayloadRoot,
    [Parameter(Mandatory = $true)]
    [string]$SetupHost,
    [Parameter(Mandatory = $true)]
    [string]$LlvmReadobjPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0

if (-not (Test-Path -LiteralPath $LlvmReadobjPath -PathType Leaf)) {
    throw "Bundled llvm-readobj.exe is missing: $LlvmReadobjPath"
}
$llvmReadobj = [System.IO.Path]::GetFullPath($LlvmReadobjPath)

$systemDlls = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
@(
    'advapi32.dll', 'authz.dll', 'bcrypt.dll', 'bcryptprimitives.dll', 'combase.dll', 'comdlg32.dll', 'crypt32.dll',
    'd3d11.dll', 'd3d12.dll', 'dbghelp.dll', 'dcomp.dll', 'dwrite.dll', 'dwmapi.dll',
    'dxgi.dll', 'gdi32.dll', 'imm32.dll',
    'iphlpapi.dll', 'kernel32.dll', 'mf.dll', 'mfplat.dll', 'mfreadwrite.dll', 'mfuuid.dll',
    'ntdll.dll', 'ole32.dll', 'oleaut32.dll', 'opengl32.dll', 'powrprof.dll', 'rpcrt4.dll',
    'secur32.dll', 'setupapi.dll', 'shell32.dll', 'shlwapi.dll', 'user32.dll', 'userenv.dll',
    'version.dll', 'winhttp.dll', 'winmm.dll', 'wtsapi32.dll', 'ws2_32.dll', 'msvcrt.dll',
    'netapi32.dll', 'normaliz.dll', 'psapi.dll', 'ucrtbase.dll', 'uxtheme.dll'
) | ForEach-Object { [void]$systemDlls.Add($_) }

$redistributableDlls = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$redistDirectory = Join-Path ([System.IO.Path]::GetFullPath($PayloadRoot)) 'Toolkit\Redist'
if (Test-Path -LiteralPath $redistDirectory -PathType Container) {
    Get-ChildItem -LiteralPath $redistDirectory -Filter '*.dll' -File |
        ForEach-Object { [void]$redistributableDlls.Add($_.Name) }
}

$binaries = @(
    Get-ChildItem -LiteralPath ([System.IO.Path]::GetFullPath($PayloadRoot)) -File -Recurse |
        Where-Object { $_.Extension -in @('.exe', '.dll') }
)
$binaries += Get-Item -LiteralPath ([System.IO.Path]::GetFullPath($SetupHost))
$missing = [Collections.Generic.List[string]]::new()
$checkedImports = 0

foreach ($binary in $binaries) {
    $output = & $llvmReadobj --coff-imports $binary.FullName 2>&1
    if ($LASTEXITCODE -ne 0) { throw "llvm-readobj failed for $($binary.FullName).`n$output" }
    $dependencies = @($output | ForEach-Object {
        if ($_ -match '^\s*Name:\s+([A-Za-z0-9._-]+\.dll)\s*$') { $Matches[1] }
    } | Sort-Object -Unique)
    foreach ($dependency in $dependencies) {
        $checkedImports++
        if ($dependency.StartsWith('api-ms-', [StringComparison]::OrdinalIgnoreCase) -or
            $dependency.StartsWith('ext-ms-', [StringComparison]::OrdinalIgnoreCase) -or
            $systemDlls.Contains($dependency) -or
            $redistributableDlls.Contains($dependency) -or
            (Test-Path -LiteralPath (Join-Path $binary.DirectoryName $dependency) -PathType Leaf)) {
            continue
        }
        $missing.Add("$($binary.FullName) -> $dependency")
    }
}

if ($missing.Count -gt 0) {
    throw "Native dependency audit found imports that are neither Windows system DLLs nor app-local:`n$($missing -join "`n")"
}

Write-Host "Native dependency audit passed: $($binaries.Count) binaries, $checkedImports imports."
