[CmdletBinding()]
param(
    [ValidateSet('Debug','Release','Both')][string]$Variant = 'Release',
    [ValidateSet('E1','E2')][string]$DiscRevision = 'E2',
    [string]$RuntimeRepository,
    [string]$BuildDirectory,
    [string]$PreparedDirectory,
    [string]$OutputDirectory,
    [string]$PythonPath = "$env:LOCALAPPDATA/Programs/Python/Python312/python.exe",
    [string]$GradlePath = "$env:USERPROFILE/.gradle/wrapper/dists/gradle-8.8-bin/dl7vupf4psengwqhwktix4v1/gradle-8.8/bin/gradle.bat"
)
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$revisionNumber = if ($DiscRevision -eq 'E1') { 1 } else { 2 }
if (-not $RuntimeRepository) { $RuntimeRepository = Join-Path $workspace $(if ($revisionNumber -eq 1) {'reference/nsmbw-android-e1'} else {'reference/nsmbw-android'}) }
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $workspace $(if ($revisionNumber -eq 1) {'build/nsmbw-thor/e1/android-native'} else {'build/nsmbw-thor/android-native'}) }
if (-not $PreparedDirectory) { $PreparedDirectory = Join-Path $workspace $(if ($revisionNumber -eq 1) {'build/nsmbw-thor/e1/prepared'} else {'build/nsmbw-thor/e2-prepared'}) }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $workspace $(if ($revisionNumber -eq 1) {'build/nsmbw-thor/e1'} else {'build/nsmbw-thor'}) }
$RuntimeRepository = [IO.Path]::GetFullPath($RuntimeRepository)
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$PreparedDirectory = [IO.Path]::GetFullPath($PreparedDirectory)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
# One WiiDS version for every app (release/version.properties), passed to Gradle.
$wiidsVersion = @{}
Get-Content (Join-Path $workspace 'release/version.properties') | Where-Object { $_ -match '^\s*([^#=]+)=(.*)$' } | ForEach-Object { $wiidsVersion[$Matches[1].Trim()] = $Matches[2].Trim() }
$versionArgs = @("-PwiidsVersionName=$($wiidsVersion['wiidsVersionName'])", "-PwiidsVersionCode=$($wiidsVersion['wiidsVersionCode'])")
$project = Join-Path $RuntimeRepository 'android'
$signer = Join-Path $workspace '.tools/android-sdk/build-tools/34.0.0/apksigner.bat'
foreach ($required in @($PythonPath, $GradlePath, $signer)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing tool: $required" }
}
& $PythonPath (Join-Path $PSScriptRoot 'stage_native.py') --revision $revisionNumber --repo $RuntimeRepository --build $BuildDirectory --prepared $PreparedDirectory
if ($LASTEXITCODE -ne 0) { throw 'Native staging failed; no APK was packaged.' }
$variants = if ($Variant -eq 'Both') { @('Debug', 'Release') } else { @($Variant) }
$results = @()
foreach ($item in $variants) {
    & $GradlePath -p $project ":app:assemble$item" -PthorBuild=true "-PthorDiscRevision=$revisionNumber" @versionArgs --offline --console=plain
    if ($LASTEXITCODE -ne 0) { throw "Thor NSMB $item packaging failed." }
    $directory = $item.ToLowerInvariant()
    $source = Join-Path $project "app/build/outputs/apk/$directory/app-$directory.apk"
    $filename = if ($item -eq 'Release') { 'NSMBW-Thor.apk' } else { 'NSMBW-Thor-debug.apk' }
    $target = Join-Path $OutputDirectory $filename
    Copy-Item -LiteralPath $source -Destination $target
    & $signer verify $target
    if ($LASTEXITCODE -ne 0) { throw 'APK signature verification failed.' }
    $results += @{ variant=$item; path=$target; sha256=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() }
}
@{ status='packaged_not_gameplay_verified'; package='com.wither.nsmbw.thor'; disc_id='SMNE01'; revision=$revisionNumber; native_receipt=(Join-Path $RuntimeRepository 'android/native/thor-native.json'); apks=$results } |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'apk-manifest.json') -Encoding utf8
$results | ForEach-Object { Write-Output "Local private APK: $($_.path) SHA256=$($_.sha256)" }
