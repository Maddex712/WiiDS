[CmdletBinding()]
param(
    [string]$WorkspaceRoot = (Join-Path $PSScriptRoot '..\..'),
    [string]$PreparedDirectory,
    [string]$DotNetPath,
    [string]$TranslatorDll,
    [int]$Threads = 6
)
# Run prepare_e2.py --apply-reviewed-runtime first. This never builds an E1
# translation against an E2 disc, installs an APK, or changes the owner's saves.
$ErrorActionPreference = 'Stop'
$WorkspaceRoot = [IO.Path]::GetFullPath($WorkspaceRoot)
if (-not $PreparedDirectory) { $PreparedDirectory = Join-Path $WorkspaceRoot 'build\nsmbw-thor\e2-prepared' }
if (-not $DotNetPath) { $DotNetPath = Join-Path $WorkspaceRoot '.tools\dotnet8\dotnet.exe' }
if (-not $TranslatorDll) { $TranslatorDll = Join-Path $WorkspaceRoot 'build\nsmbw-thor\translator\Translator.Cli.dll' }
$repo = Join-Path $WorkspaceRoot 'reference\nsmbw-android'
$project = Join-Path $PreparedDirectory 'recomp-e2.yml'
$receipt = Get-Content -LiteralPath (Join-Path $PreparedDirectory 'preparation.json') -Raw | ConvertFrom-Json
if ($receipt.revision -ne 2 -or -not $receipt.runtime_patch_applied -or
    $receipt.input_sha256.'main.dol' -ne '0373b4dbffd7c9da9d27eb7257a7677877a3e33ceb81818bab1daa93560b9907') {
    throw 'Missing matching, reviewed E2 preparation receipt.'
}
$logDirectory = Join-Path $PreparedDirectory 'translation-logs'
[IO.Directory]::CreateDirectory($logDirectory) | Out-Null
$steps = @(
    @{ Name = 'translate'; Args = @('translate-recursive','0x80004050','--project',$project,
        '--outdir','generated/functions','--output-metadata','generated/base_translation_output.json',
        '--production-source-bundle','generated/base_translation_sources.bin','--no-function-files',
        '--prune-stale','--threads',"$Threads") },
    @{ Name = 'manifest'; Args = @('emit-base-manifest','--project',$project,'--out','build/base',
        '--functions-dir','generated/functions','--translation-output-metadata','generated/base_translation_output.json','--region','E') },
    @{ Name = 'data-init'; Args = @('generate-data-init','--project',$project) },
    @{ Name = 'shards'; Args = @('emit-build-shards','--project',$project,
        '--base-metadata','generated/base_translation_output.json','--base-functions-dir','generated/functions',
        '--native-source-dir','runtime/src','--out','generated/build_shards') }
)
$results = @()
Push-Location $repo
try {
    foreach ($step in $steps) {
        $log = Join-Path $logDirectory ($step.Name + '.log')
        $ErrorActionPreference = 'Continue'
        & $DotNetPath $TranslatorDll @($step.Args) 2>&1 | ForEach-Object { "$_" } | Out-File -LiteralPath $log -Encoding utf8
        $code = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        $results += @{ step=$step.Name; exit_code=$code; log=$log }
        if ($code -ne 0) { Get-Content -LiteralPath $log -Tail 30; throw "$($step.Name) failed ($code)." }
        Write-Output "$($step.Name) passed"
    }
} finally { Pop-Location }
@{ status='translated_not_built_or_booted'; revision=2; source_commit=$receipt.source_commit;
   dol_sha256=$receipt.input_sha256.'main.dol'; steps=$results } |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $PreparedDirectory 'translation.json') -Encoding utf8
