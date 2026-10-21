param(
    [Parameter(Mandatory=$true)][string]$DiscPath,
    [string]$Dotnet = $(if ($env:THOR_DOTNET) { $env:THOR_DOTNET } else { 'dotnet' }),
    [int]$Threads = 8
)
$ErrorActionPreference = 'Stop'
# Resolve a PATH command or explicit override before checking required files.
$Dotnet = (Get-Command -Name $Dotnet -CommandType Application -ErrorAction Stop).Source
$projectRoot = Split-Path -Parent $PSScriptRoot
$runtimeRoot = Join-Path $projectRoot 'reference/driftdroid'
$dtk = Join-Path $projectRoot '.tools/dtk.exe'
$translator = Join-Path $runtimeRoot 'translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll'
$manifest = Join-Path $runtimeRoot 'projects/mkwii/recomp.yml'
$extracted = Join-Path $runtimeRoot 'DiscData'
$marker = Join-Path $projectRoot 'build/disc-validation.json'
$disc = (Resolve-Path -LiteralPath $DiscPath).Path
if (!(Test-Path -LiteralPath $translator)) { throw 'Build Translator.Cli in Release first.' }
if ($Threads -lt 1) { throw 'Threads must be positive.' }

# Deny concurrent writes: a file whose transfer has not finished must not be translated.
$readLock = [System.IO.File]::Open($disc, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
try {
    $header = New-Object byte[] 1024
    if ($readLock.Read($header, 0, $header.Length) -ne $header.Length) { throw 'Disc image is incomplete.' }
    $offset = if ([System.Text.Encoding]::ASCII.GetString($header,0,4) -eq 'WBFS') { 512 } else { 0 }
    $id = [System.Text.Encoding]::ASCII.GetString($header, $offset, 6)
    if ($id -ne 'RMCP01') { throw "This manifest needs PAL RMCP01; the supplied image is $id. No region checks were changed." }
    $imageHash = (Get-FileHash -LiteralPath $disc -Algorithm SHA256).Hash.ToLowerInvariant()
    if (Test-Path -LiteralPath $extracted) {
        if (!(Test-Path -LiteralPath $marker)) { throw 'An unverified extraction already exists. Inspect it before retrying; nothing was overwritten.' }
        $verified = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
        if ($verified.imageSha256 -ne $imageHash) { throw 'Cached extraction is from a different disc image.' }
    } else {
        & $dtk disc extract --validate $disc $extracted
        if ($LASTEXITCODE -ne 0) { throw 'Disc extraction or data-hash validation failed.' }
    }
    $dol = Join-Path $extracted 'sys/main.dol'
    $rel = Join-Path $extracted 'files/rel/StaticR.rel'
    $dolHash = (Get-FileHash -LiteralPath $dol -Algorithm SHA256).Hash.ToLowerInvariant()
    $relHash = (Get-FileHash -LiteralPath $rel -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($dolHash -ne '80d18895b39c63bd80f457398bfcbb91b7d16ac116a41a88967e954080155b05') { throw "Unsupported main.dol hash: $dolHash" }
    if ($relHash -ne '16d9d146112541fefea701ecb5bc1a496f9d50e4a752fbb5b6778e7c6399f67d') { throw "Unsupported StaticR.rel hash: $relHash" }
    New-Item -ItemType Directory -Force -Path (Join-Path $runtimeRoot 'Assets'), (Split-Path -Parent $marker) | Out-Null
    Copy-Item -LiteralPath $dol -Destination (Join-Path $runtimeRoot 'Assets/main.dol')
    Copy-Item -LiteralPath $rel -Destination (Join-Path $runtimeRoot 'Assets/StaticR.rel')
    @{gameId=$id; imageSha256=$imageHash; dolSha256=$dolHash; relSha256=$relHash} |
        ConvertTo-Json | Set-Content -LiteralPath $marker -Encoding utf8
} finally {
    $readLock.Dispose()
}

Push-Location -LiteralPath $runtimeRoot
try {
    foreach ($translatorArgs in @(
        @('generate-data-init','--project',$manifest),
        @('translate-recursive','0x800060A4','--project',$manifest,'--threads',"$Threads",'--output-metadata','generated/base_translation_output.json'),
        @('emit-base-manifest','--project',$manifest),
        @('emit-build-shards','--project',$manifest)
    )) {
        & $Dotnet --roll-forward Major $translator @translatorArgs
        if ($LASTEXITCODE -ne 0) { throw "Translation command failed: $($translatorArgs[0])" }
    }
} finally {
    Pop-Location
}
Write-Output 'Local PAL translation generated. Native compilation and real gameplay verification are still required.'
