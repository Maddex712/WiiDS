$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$android = Join-Path $workspace 'reference/driftdroid/android'
$env:JAVA_HOME = 'C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot'
$env:ANDROID_HOME = Join-Path $workspace '.tools/android-sdk'
$version = @{}
Get-Content (Join-Path $workspace 'release/version.properties') | ForEach-Object {
    if ($_ -match '^([^#=]+)=(.*)$') { $version[$Matches[1]] = $Matches[2].Trim() }
}
$arguments = @('-p', $android, '--offline',
    'assembleRelease', '-PthorBuild=true', '-PwiidsApp=true', '-PthorRetroRewind=true',
    "-PwiidsVersionName=$($version.wiidsVersionName)", "-PwiidsVersionCode=$($version.wiidsVersionCode)", '--console=plain', '--no-daemon')
& (Join-Path $android 'gradlew.bat') @arguments
if ($LASTEXITCODE -ne 0) { throw 'WiiDS Galaxy frontend build failed' }
