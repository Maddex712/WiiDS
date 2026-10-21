[CmdletBinding()]
param([int]$Threads = 6)
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$repo = Join-Path $workspace 'reference/nsmbw-android-e1'
$prepared = Join-Path $workspace 'build/nsmbw-thor/e1/prepared'
$project = Join-Path $prepared 'recomp-e1.yml'
$dotnet = Join-Path $workspace '.tools/dotnet8/dotnet.exe'
$translator = Join-Path $workspace 'build/nsmbw-thor/translator/Translator.Cli.dll'
$inputReceipt = Get-Content -LiteralPath (Join-Path $prepared 'preparation.json') -Raw | ConvertFrom-Json
if ($inputReceipt.revision -ne 1 -or $inputReceipt.dol_sha256 -ne '166eb6b7cca21418e316a6cfbf1bfbf0a213eb8f6396d42126a1482bb764a955') {
    throw 'Missing verified canonical E1 preparation receipt.'
}
$logs = Join-Path $prepared 'translation-logs'
[IO.Directory]::CreateDirectory($logs) | Out-Null
$steps = @(
    @{ Name='translate'; Args=@('translate-recursive','0x80004050','--project',$project,'--outdir','generated/functions','--output-metadata','generated/base_translation_output.json','--production-source-bundle','generated/base_translation_sources.bin','--no-function-files','--prune-stale','--threads',"$Threads") },
    @{ Name='manifest'; Args=@('emit-base-manifest','--project',$project,'--out','build/base','--functions-dir','generated/functions','--translation-output-metadata','generated/base_translation_output.json','--region','E') },
    @{ Name='data-init'; Args=@('generate-data-init','--project',$project) },
    @{ Name='shards'; Args=@('emit-build-shards','--project',$project,'--base-metadata','generated/base_translation_output.json','--base-functions-dir','generated/functions','--native-source-dir','runtime/src','--out','generated/build_shards') }
)
Push-Location $repo
try {
    foreach ($step in $steps) {
        $log = Join-Path $logs ($step.Name + '.log')
        & $dotnet $translator @($step.Args) 2>&1 | ForEach-Object { "$_" } | Out-File -LiteralPath $log -Encoding utf8
        if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath $log -Tail 20; throw "$($step.Name) failed; see $log" }
        Write-Output "$($step.Name) passed"
    }
} finally { Pop-Location }
