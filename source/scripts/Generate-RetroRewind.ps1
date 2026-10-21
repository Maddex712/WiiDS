param(
    [Parameter(Mandatory=$true)][string]$PackRoot,
    [string]$Dotnet = $(if ($env:THOR_DOTNET) { $env:THOR_DOTNET } else { 'dotnet' }),
    [ValidateRange(1,64)][int]$Threads = 8,
    [switch]$EnableRetroWfc
)
$ErrorActionPreference = 'Stop'
# Resolve a PATH command or explicit override before checking required files.
$Dotnet = (Get-Command -Name $Dotnet -CommandType Application -ErrorAction Stop).Source
$projectRoot = Split-Path -Parent $PSScriptRoot
$runtimeRoot = Join-Path $projectRoot 'reference/driftdroid'
$buildRoot = Join-Path $projectRoot 'build'
$translator = Join-Path $runtimeRoot 'translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll'
$sourceManifest = Join-Path $runtimeRoot 'projects/mkwii/recomp.yml'
$manifest = Join-Path $buildRoot 'rr-project.yml'
$generated = Join-Path $runtimeRoot 'generated'
$backup = Join-Path $buildRoot 'before-retro-rewind'
$backupReceipt = Join-Path $backup 'snapshot.json'
$runReceipt = Join-Path $buildRoot 'retro-rewind-generation.json'
$modOutput = Join-Path $runtimeRoot 'build/mods/retro_rewind_full_cpp'
$resolved = Join-Path $modOutput 'resolved_dispatch_profile.json'
$cpp = Join-Path $modOutput 'cpp'
$baseManifest = Join-Path $runtimeRoot 'build/base/mkwii_base_manifest.json'
$pack = (Resolve-Path -LiteralPath $PackRoot).Path
$codePul = Join-Path $pack 'Binaries/Code.pul'
$supportedCodeHash = '7471e1d2f61c11ab0de6544728406ee1359a4eab736b2dc6a73b741f69e7b4a3'

foreach ($required in @($Dotnet,$translator,$sourceManifest,$codePul,(Join-Path $pack 'xml/RetroRewind6.xml'))) {
    if (!(Test-Path -LiteralPath $required -PathType Leaf)) { throw "Required input missing: $required" }
}
foreach ($asset in @(
    @{ Path='Assets/main.dol'; Hash='80d18895b39c63bd80f457398bfcbb91b7d16ac116a41a88967e954080155b05' },
    @{ Path='Assets/StaticR.rel'; Hash='16d9d146112541fefea701ecb5bc1a496f9d50e4a752fbb5b6778e7c6399f67d' }
)) {
    $path = Join-Path $runtimeRoot $asset.Path
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Clean PAL input missing: $path" }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $asset.Hash) {
        throw "Unsupported PAL input hash: $path. No region or integrity checks were relaxed."
    }
}

function Quote-YamlPath([string]$Path) {
    return "'" + $Path.Replace('\','/').Replace("'","''") + "'"
}
function Invoke-Translator([string[]]$CommandArguments) {
    Write-Output ("Translator: " + ($CommandArguments -join ' '))
    & $Dotnet --roll-forward Major $translator @CommandArguments
    if ($LASTEXITCODE -ne 0) { throw "Translation failed ($LASTEXITCODE): $($CommandArguments[0]). Original backup remains at $backup" }
}

# A pack being replaced mid-run must not silently change the translation input.
$codeRead = [IO.File]::Open($codePul,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
try {
    if ((Get-FileHash -LiteralPath $codePul -Algorithm SHA256).Hash -ne $supportedCodeHash) {
        throw 'Unsupported Code.pul: this script and the Android frontend require the pinned Retro Rewind 6.13.0 code.'
    }
    if (!(Test-Path -LiteralPath $generated -PathType Container)) { throw 'The original generated tree is missing; cannot preserve the known-good base.' }
    $originalApk = Join-Path $buildRoot 'DriftDroid-Thor.apk'
    if (!(Test-Path -LiteralPath $originalApk -PathType Leaf)) { throw 'Known-good build/DriftDroid-Thor.apk missing; refusing to translate without a preserved fallback.' }

    if (Test-Path -LiteralPath $backup) {
        if (!(Test-Path -LiteralPath $backupReceipt -PathType Leaf)) {
            throw "Incomplete backup already exists at $backup. Inspect it manually; it will not be overwritten."
        }
        $saved = Get-Content -LiteralPath $backupReceipt -Raw | ConvertFrom-Json
        if ($saved.schema -ne 1 -or $saved.generatedSource -ne $generated -or
            !(Test-Path -LiteralPath (Join-Path $backup 'generated') -PathType Container)) {
            throw 'Existing original-generation backup is invalid; it will not be overwritten.'
        }
        foreach ($artifact in $saved.artifacts) {
            if (!(Test-Path -LiteralPath $artifact.backup -PathType Leaf) -or
                (Get-FileHash -LiteralPath $artifact.backup -Algorithm SHA256).Hash -ne $artifact.sha256) {
                throw "Original APK backup failed integrity verification: $($artifact.backup)"
            }
        }
        Write-Output "Reusing original backup without changes: $backupReceipt"
    } else {
        $sourceFiles = @(Get-ChildItem -LiteralPath $generated -Recurse -File -Force)
        if ($sourceFiles.Count -eq 0) { throw 'The original generated tree is empty.' }
        $sourceBytes = ($sourceFiles | Measure-Object -Property Length -Sum).Sum
        New-Item -ItemType Directory -Path $backup | Out-Null
        # The destination was just created and contains no prior snapshot to overwrite.
        Copy-Item -LiteralPath $generated -Destination $backup -Recurse -Force
        $copiedFiles = @(Get-ChildItem -LiteralPath (Join-Path $backup 'generated') -Recurse -File -Force)
        if ($copiedFiles.Count -ne $sourceFiles.Count -or
            ($copiedFiles | Measure-Object -Property Length -Sum).Sum -ne $sourceBytes) {
            throw "Generated-tree backup is incomplete at $backup. Translation was not started."
        }
        $artifactFolder = Join-Path $backup 'artifacts'
        New-Item -ItemType Directory -Path $artifactFolder | Out-Null
        $artifacts = @()
        foreach ($artifact in Get-ChildItem -LiteralPath $buildRoot -File -Filter 'DriftDroid-Thor*.apk*') {
            $destination = Join-Path $artifactFolder $artifact.Name
            Copy-Item -LiteralPath $artifact.FullName -Destination $destination
            $hash = (Get-FileHash -LiteralPath $artifact.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $hash) {
                throw "APK backup copy differs: $destination. Translation was not started."
            }
            $artifacts += @{ source=$artifact.FullName; backup=$destination; sha256=$hash }
        }
        @{ schema=1; createdUtc=[DateTime]::UtcNow.ToString('o'); generatedSource=$generated;
           generatedBackup=(Join-Path $backup 'generated'); fileCount=$sourceFiles.Count; bytes=$sourceBytes;
           artifacts=$artifacts } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $backupReceipt -Encoding utf8
        Write-Output "Original generated tree and APKs preserved: $backupReceipt"
    }

    $yaml = Get-Content -LiteralPath $sourceManifest -Raw
    foreach ($replacement in @(
        @{ Pattern='(?m)^workspace_root:[^\r\n]*'; Value=('workspace_root: ' + (Quote-YamlPath $runtimeRoot)) },
        @{ Pattern='(?m)^    code_pul:[^\r\n]*'; Value=('    code_pul: ' + (Quote-YamlPath $codePul)) },
        @{ Pattern='(?m)^    mod_root:[^\r\n]*'; Value=('    mod_root: ' + (Quote-YamlPath $pack)) }
    )) {
        if ([regex]::Matches($yaml,$replacement.Pattern).Count -ne 1) { throw 'Pinned project manifest structure changed; refusing an ambiguous YAML rewrite.' }
        $yaml = [regex]::Replace($yaml,$replacement.Pattern,[Text.RegularExpressions.MatchEvaluator]{ param($match) $replacement.Value })
    }
    Set-Content -LiteralPath $manifest -Value $yaml -Encoding utf8
    $record = @{ schema=1; startedUtc=[DateTime]::UtcNow.ToString('o'); complete=$false;
        packRoot=$pack; codePulSha256=$supportedCodeHash; sourceManifest=$sourceManifest;
        sourceManifestSha256=(Get-FileHash -LiteralPath $sourceManifest -Algorithm SHA256).Hash.ToLowerInvariant();
        derivedManifest=$manifest; backupReceipt=$backupReceipt; retroWfcEnabled=[bool]$EnableRetroWfc;
        resolvedProfile=$resolved; cppDirectory=$cpp }
    $record | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $runReceipt -Encoding utf8
    Write-Output "Derived project: $manifest"
    if (!$EnableRetroWfc) { Write-Output 'Offline translation: --skip-retro-wfc. Online WFC lowering/support is omitted and is not validated.' }

    Push-Location -LiteralPath $runtimeRoot
    try {
        Invoke-Translator -CommandArguments @('generate-data-init','--project',$manifest)
        Invoke-Translator -CommandArguments @('translate-recursive','0x800060A4','--project',$manifest,
            '--threads',"$Threads",'--output-metadata','generated/base_translation_output.json')
        Invoke-Translator -CommandArguments @('emit-base-manifest','--project',$manifest,
            '--translation-output-metadata','generated/base_translation_output.json')
        $modArguments = @('translate-mod','--project',$manifest,'--profile','retro-rewind',
            '--base-manifest',$baseManifest,'--out',$modOutput,'--emit-cpp','--threads',"$Threads")
        if (!$EnableRetroWfc) { $modArguments += '--skip-retro-wfc' }
        Invoke-Translator -CommandArguments $modArguments
        Invoke-Translator -CommandArguments @('emit-build-shards','--project',$manifest,
            '--resolved-profile',$resolved,'--retro-cpp-dir',$cpp)
    } finally { Pop-Location }

    $graph = Get-Content -LiteralPath (Join-Path $generated 'build_shards/shards.cmake') -Raw
    if ($graph -notmatch 'set\(MKW_HAVE_RETRO_REWIND_SHARDS\s+ON\)' -or
        $graph -notmatch 'set\(MKW_RETRO_REWIND_FUNCTION_COUNT\s+[1-9][0-9]*\)') {
        throw 'Translator finished without a nonempty enabled Retro Rewind graph.'
    }
    $profile = Get-Content -LiteralPath $resolved -Raw | ConvertFrom-Json
    if ($profile.Format -ne 'mkwii-resolved-dispatch-profile' -or @($profile.Entries).Count -eq 0) {
        throw 'Translator finished without a valid resolved dispatch profile.'
    }
    $collisions = @($profile.NativeOverriddenModPatches | Where-Object { $null -ne $_ })
    Write-Output "NativeOverriddenModPatches: $($collisions.Count) (review every entry before native validation)"
    foreach ($collision in $collisions) {
        Write-Output ('  0x{0:X8}: {1} overridden by {2} ({3})' -f [uint32]$collision.Address,$collision.Mod,$collision.Native,$collision.NativeSource)
    }
    $record.complete = $true
    $record.completedUtc = [DateTime]::UtcNow.ToString('o')
    $record.nativeOverriddenModPatches = $collisions
    $record | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $runReceipt -Encoding utf8
    Write-Output "Generated RR sources only. Receipt: $runReceipt. Native build, pack staging and real gameplay validation remain."
} finally { $codeRead.Dispose() }
