param(
    [string]$RuntimeRoot,
    [string]$Kotlinc = 'kotlinc',
    [string]$Java = 'java',
    [string]$OutputDirectory
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
if(!$RuntimeRoot) { $RuntimeRoot=Join-Path $projectRoot 'reference/driftdroid' }
if(!$OutputDirectory) {
    $OutputDirectory=Join-Path $projectRoot ('build/rr-updater-fixtures-'+[Guid]::NewGuid().ToString('N'))
}
if(Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a new fixture output directory.' }
$source=Join-Path $RuntimeRoot 'android/app/src/main/kotlin/com/driftdroid/android'
$files=@('RetroRewindCatalog.kt','RetroRewindRelease.kt','AtomicConfigFile.kt',
    'ThorRetroRewindUpdater.kt','ThorRetroRewindUpdateStatus.kt') | ForEach-Object { Join-Path $source $_ }
$files+=Join-Path $PSScriptRoot 'ThorRetroRewindUpdaterTest.kt'
foreach($file in $files) { if(!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing source: $file" } }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$jar=Join-Path $OutputDirectory 'fixtures.jar'
& $Kotlinc @files -jvm-target 17 -include-runtime -d $jar
if($LASTEXITCODE -ne 0) { throw 'Kotlin fixture compilation failed.' }
& $Java -jar $jar (Join-Path $OutputDirectory 'data')
if($LASTEXITCODE -ne 0) { throw 'Updater fixtures failed.' }
Write-Output "Fixture results: $(Join-Path $OutputDirectory 'data/results.json')"
