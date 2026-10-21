# Re-run the NSMBW translation pipeline after runtime natives changed, then rebuild WiiCompiled.
# Mirrors Launcher/LocalBuild.ps1: translate-recursive -> emit-base-manifest -> generate-data-init ->
# emit-build-shards -> cmake --build. Ninja only recompiles shards whose content changed.
[CmdletBinding()]
param([int]$Threads = 10, [int]$Jobs = 10)

$ErrorActionPreference = 'Stop'
$ws = 'E:\NSMBWPort\wiicompiled'
$logs = 'E:\NSMBWPort\build-nsmbw\logs'
$env:DOTNET_ROOT = 'E:\NSMBWPort\toolchain\dotnet'
$env:PATH = "$env:DOTNET_ROOT;$env:PATH"
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$translator = Join-Path $ws 'translator\src\Translator.Cli\bin\Release\net8.0\Translator.Cli.dll'
$project = 'projects/nsmbw/recomp.yml'

function Invoke-Step([string]$Name, [string[]]$Arguments) {
    $log = Join-Path $logs "rebuild-$Name.log"
    $ErrorActionPreference = 'Continue'
    & dotnet $translator @Arguments 2>&1 | ForEach-Object { "$_" } | Out-File -LiteralPath $log -Encoding utf8
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($code -ne 0) { Get-Content $log -Tail 20; throw "translator step '$Name' failed ($code); see $log" }
    "$Name ok"
}

Push-Location $ws
try {
    # Stop any test instance so the link can replace WiiCompiled.exe.
    Get-Process WiiCompiled -ErrorAction SilentlyContinue | Where-Object { $_.Path -like 'E:\NSMBWPort\*' } |
        ForEach-Object { Stop-Process -Id $_.Id -Force }
    Invoke-Step 'translate' @('translate-recursive', '0x80004050', '--project', $project,
        '--outdir', 'generated/functions', '--output-metadata', 'generated/base_translation_output.json',
        '--production-source-bundle', 'generated/base_translation_sources.bin',
        '--no-function-files', '--prune-stale', '--threads', "$Threads")
    Invoke-Step 'manifest' @('emit-base-manifest', '--project', $project, '--out', 'build/base',
        '--functions-dir', 'generated/functions', '--translation-output-metadata',
        'generated/base_translation_output.json', '--region', 'E')
    Invoke-Step 'data-init' @('generate-data-init', '--project', $project)
    Invoke-Step 'shards' @('emit-build-shards', '--project', $project, '--base-metadata',
        'generated/base_translation_output.json', '--base-functions-dir', 'generated/functions',
        '--native-source-dir', 'runtime/src', '--out', 'generated/build_shards')
} finally {
    Pop-Location
}
& 'E:\NSMBWPort\tools\build-native.ps1' -Target WiiCompiled -Jobs $Jobs -LogPath (Join-Path $logs 'rebuild-build.log')
$code = $LASTEXITCODE
Get-Content (Join-Path $logs 'rebuild-build.log') | Select-String ' error' | Select-Object -First 10 | ForEach-Object Line
"build exit=$code"
exit $code
