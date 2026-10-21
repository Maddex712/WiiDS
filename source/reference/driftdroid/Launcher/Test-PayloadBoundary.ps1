[CmdletBinding()]
param([Parameter(Mandatory)] [string]$PayloadRoot)

$ErrorActionPreference = 'Stop'
$PayloadRoot = [IO.Path]::GetFullPath($PayloadRoot)
if (-not (Test-Path -LiteralPath $PayloadRoot -PathType Container)) { throw "Payload does not exist: $PayloadRoot" }

$forbiddenNames = @(
    'WiiCompiled.exe', 'RetroRewind.exe', 'mkw_recompiled.exe',
    'main.dol', 'StaticR.rel', 'Code.pul', 'base_translation_sources.bin',
    'data_sections_init.cpp', 'data_sections_init_blobs.S', 'mkwii_base_manifest.json',
    'generated_func_aliases.cpp', 'translated_function_decls.h'
)
$forbiddenExtensions = @('.iso', '.gcm', '.gcz', '.ciso', '.wbfs', '.wia', '.rvz', '.dol', '.rel', '.pul')
$forbiddenSegments = @(
    '\BuildWorkspace\generated\', '\BuildWorkspace\runtime\build\', '\GameAssets\', '\BuildWorkspace\Assets\',
    '\BuildWorkspace\PulsarPacks\', '\BuildWorkspace\build\base\', '\BuildWorkspace\build\mods\'
)
$violations = [Collections.Generic.List[string]]::new()
foreach ($file in Get-ChildItem -LiteralPath $PayloadRoot -Recurse -File) {
    $rootPrefix = $PayloadRoot.TrimEnd('\') + '\'
    if (-not $file.FullName.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Payload enumeration escaped its root: $($file.FullName)"
    }
    $relative = '\' + $file.FullName.Substring($rootPrefix.Length).Replace('/', '\')
    $underCompiler = $relative.StartsWith('\Toolkit\llvm-mingw\', [StringComparison]::OrdinalIgnoreCase)
    $underPinnedDependency = $relative.StartsWith('\BuildWorkspace\Dependencies\', [StringComparison]::OrdinalIgnoreCase)
    if ($forbiddenNames -contains $file.Name -or $forbiddenExtensions -contains $file.Extension.ToLowerInvariant()) {
        $violations.Add($relative)
        continue
    }
    if ($forbiddenSegments | Where-Object { $relative.IndexOf($_, [StringComparison]::OrdinalIgnoreCase) -ge 0 }) {
        $violations.Add($relative)
        continue
    }
    if (-not $underCompiler -and -not $underPinnedDependency -and $file.Extension -in @('.obj','.o','.lib','.a')) {
        $violations.Add("$relative (precompiled object/library outside the licensed compiler payload)")
    }
}
if ($violations.Count -gt 0) {
    throw "Release payload contains forbidden game-derived or precompiled artifacts:`n$($violations -join "`n")"
}
Write-Host "Payload boundary audit passed: no translated game executable, game input, generated shard, or generated data was packaged."
