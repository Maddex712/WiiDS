[CmdletBinding()]
param(
    [string]$AndroidSdk,
    [string]$JavaHome,
    [string]$Python,
    [string]$Output,
    [switch]$SkipEclipseStaging,
    [string]$ExpectedCertificateSha256 = '72eef06b956bc1c6848a09bf4956b158095736a6e81f43131cf93cb30788345f'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$androidRoot = Join-Path $projectRoot 'reference/driftdroid/android'
$jniRoot = Join-Path $androidRoot 'app/src/main/jniLibs/arm64-v8a'

function Find-Directory([string[]]$Candidates, [string]$RequiredChild, [string]$Description) {
    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath (Join-Path $candidate $RequiredChild) -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw "$Description not found. Supply its explicit script parameter or configure its environment variable."
}

function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Release command failed ($LASTEXITCODE): $Executable" }
}

$sdkCandidates = @($AndroidSdk, $env:ANDROID_SDK_ROOT, $env:ANDROID_HOME,
    (Join-Path $projectRoot '.tools/android-sdk'))
if ($AndroidSdk -and !(Test-Path -LiteralPath (Join-Path $AndroidSdk 'platforms/android-34/android.jar') -PathType Leaf)) {
    throw 'The explicitly selected AndroidSdk does not contain API 34.'
}
if ($env:LOCALAPPDATA) { $sdkCandidates += Join-Path $env:LOCALAPPDATA 'Android/Sdk' }
$resolvedSdk = Find-Directory $sdkCandidates 'platforms/android-34/android.jar' 'Android SDK (API 34)'
$javaCandidates = @($JavaHome, $env:JAVA_HOME)
if ($JavaHome -and !(Test-Path -LiteralPath (Join-Path $JavaHome 'bin/javac.exe') -PathType Leaf)) {
    throw 'The explicitly selected JavaHome is not a JDK.'
}
$javaCompiler = Get-Command javac -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if ($javaCompiler) { $javaCandidates += Split-Path -Parent (Split-Path -Parent $javaCompiler.Source) }
$resolvedJava = Find-Directory $javaCandidates 'bin/javac.exe' 'JDK 17 or newer'
$javaVersionOutput = & (Join-Path $resolvedJava 'bin/java.exe') -version 2>&1
if ($LASTEXITCODE -ne 0 -or ($javaVersionOutput -join "`n") -notmatch 'version "([0-9]+)\.') {
    throw 'Cannot read the selected Java version.'
}
if ([int]$Matches[1] -lt 17) { throw 'JDK 17 or newer is required.' }
if (!$Python) {
    foreach ($name in @('python', 'python3', 'py')) {
        $command = Get-Command $name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($command) { $Python = $command.Source; break }
    }
}
if (!$Python) { throw 'Python 3 not found. Pass -Python with its executable path.' }
if (!(Test-Path -LiteralPath (Join-Path $androidRoot 'gradlew.bat') -PathType Leaf)) {
    throw 'The pinned Android checkout and Gradle wrapper are required.'
}
if (!(Test-Path -LiteralPath (Join-Path $resolvedSdk 'ndk/27.2.12479018/source.properties') -PathType Leaf)) {
    throw 'Install Android NDK 27.2.12479018 in the selected SDK before packaging.'
}
# Gradle permits debug-key fallback for local development. A public-release wrapper
# must instead require the owner's private signing configuration outside the repo.
$signingProperties = Join-Path ([Environment]::GetFolderPath('UserProfile')) '.wiids-release/keystore.properties'
if (!(Test-Path -LiteralPath $signingProperties -PathType Leaf)) {
    throw 'Release signing configuration is missing: create your private .wiids-release/keystore.properties outside the repository.'
}
if ($ExpectedCertificateSha256 -notmatch '^[a-fA-F0-9]{64}$') {
    throw 'ExpectedCertificateSha256 must contain the expected release certificate SHA-256 (64 hex digits).'
}
$version = @{}
Get-Content -LiteralPath (Join-Path $projectRoot 'release/version.properties') | ForEach-Object {
    if ($_ -match '^\s*([^#=]+)=(.*)$') { $version[$Matches[1].Trim()] = $Matches[2].Trim() }
}
if ($version.wiidsVersionName -notmatch '^[A-Za-z0-9_.-]+$' -or $version.wiidsVersionCode -notmatch '^[1-9][0-9]*$') {
    throw 'Invalid release/version.properties.'
}
if (!$Output) { $Output = Join-Path $projectRoot "build/public-release/WiiDS-$($version.wiidsVersionName)-arm64.apk" }
$Output = [IO.Path]::GetFullPath($Output)

# Runtime builds have already been staged and verified. Never recopy/strip all
# native libraries here: that can silently replace a known-good newer runtime.
$requiredLibraries = @('libWiiCompiled.so', 'libRetroRewind.so', 'libnsmbw.so',
    'libmsmbwii.so', 'libsms.so', 'libsmsbootstrap.so', 'libSDL2.so',
    'libc++_shared.so', 'libpng16.so')
foreach ($name in $requiredLibraries) {
    if (!(Test-Path -LiteralPath (Join-Path $jniRoot $name) -PathType Leaf)) { throw "Stage the verified runtime first: $name" }
}
foreach ($name in @('runtime-nsmbw', 'runtime-msmbwii', 'mods/MSMBWii.zip')) {
    if (!(Test-Path -LiteralPath (Join-Path $projectRoot "build/wiids-merge/assets/$name"))) { throw "Missing merged-runtime input: $name" }
}

$previousJava = $env:JAVA_HOME
$previousSdkRoot = $env:ANDROID_SDK_ROOT
$previousAndroidHome = $env:ANDROID_HOME
$previousPath = $env:PATH
try {
    $env:JAVA_HOME = $resolvedJava
    $env:ANDROID_SDK_ROOT = $resolvedSdk
    $env:ANDROID_HOME = $resolvedSdk
    $env:PATH = (Join-Path $resolvedJava 'bin') + ';' + $previousPath
    if (!$SkipEclipseStaging) {
        foreach ($name in @('reference/eclipse-launcher-thor/android/app/src/main/java',
            'reference/sms-eclipse-thor/android/bootstrap.cpp',
            'build/eclipse-thor/jniLibs/arm64-v8a/libsms.so',
            'build/eclipse-thor/android-native/SDL2/include/SDL2/SDL.h',
            'build/eclipse-thor/mod/patches/v1.1.0.xdelta')) {
            if (!(Test-Path -LiteralPath (Join-Path $projectRoot $name))) { throw "Eclipse staging prerequisite missing: $name" }
        }
        Invoke-Checked $Python @((Join-Path $projectRoot 'experiments/eclipse-thor/Stage-Embedded.py'), '--bundle-patch', '--sdk-root', $resolvedSdk)
    }
    foreach ($name in @('libeclipse.so', 'libeclipsebootstrap.so')) {
        if (!(Test-Path -LiteralPath (Join-Path $jniRoot $name) -PathType Leaf)) { throw "Missing embedded Eclipse library: $name" }
    }
    if (!(Test-Path -LiteralPath (Join-Path $projectRoot 'build/wiids-merge/assets/mods/eclipse-v1.1.0.xdelta') -PathType Leaf)) {
        throw 'The bundled, verified official Eclipse patch is required.'
    }
    Invoke-Checked $Python @((Join-Path $projectRoot 'experiments/release/stage_licenses.py'))
    $before = @{}
    Get-ChildItem -LiteralPath $jniRoot -Filter '*.so' -File | ForEach-Object {
        $before[$_.Name] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    Push-Location -LiteralPath $androidRoot
    try {
        Invoke-Checked (Join-Path $androidRoot 'gradlew.bat') @('assembleRelease', '-PthorBuild=true', '-PwiidsApp=true',
            '-PthorRetroRewind=true', "-PwiidsVersionName=$($version.wiidsVersionName)",
            "-PwiidsVersionCode=$($version.wiidsVersionCode)", '--console=plain', '--no-daemon')
    } finally { Pop-Location }
    foreach ($name in $before.Keys) {
        $after = (Get-FileHash -LiteralPath (Join-Path $jniRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($after -ne $before[$name]) { throw "Packaging unexpectedly changed a staged runtime: $name" }
    }
    $buildTools = Get-ChildItem -LiteralPath (Join-Path $resolvedSdk 'build-tools') -Directory |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'apksigner.bat') } |
        Sort-Object { try { [version]$_.Name } catch { [version]'0.0' } } -Descending | Select-Object -First 1
    if (!$buildTools) { throw 'Android build-tools with apksigner and aapt are required.' }
    $builtApk = Join-Path $androidRoot 'app/build/outputs/apk/release/app-release.apk'
    $certificate = & (Join-Path $buildTools.FullName 'apksigner.bat') verify --print-certs $builtApk 2>&1
    if ($LASTEXITCODE -ne 0) { throw 'APK signature verification failed.' }
    if (($certificate -join "`n") -notmatch 'Signer #1 certificate SHA-256 digest:\s*([a-fA-F0-9]{64})') {
        throw 'Cannot identify APK release signing certificate.'
    }
    $signer = $Matches[1].ToLowerInvariant()
    if ($signer -ne $ExpectedCertificateSha256.ToLowerInvariant()) { throw 'APK signing certificate differs from the expected release key.' }
    $badging = & (Join-Path $buildTools.FullName 'aapt.exe') dump badging $builtApk 2>&1
    if ($LASTEXITCODE -ne 0) { throw 'APK manifest inspection failed.' }
    $badgingText = $badging -join "`n"
    if ($badgingText -notmatch "package: name='io.github.maddex712.wiids' versionCode='$($version.wiidsVersionCode)' versionName='$([regex]::Escape($version.wiidsVersionName))'" -or
        $badgingText -match 'application-debuggable') { throw 'Wrong package/version or debuggable release APK.' }
    $digest = (Get-FileHash -LiteralPath $builtApk -Algorithm SHA256).Hash.ToLowerInvariant()
    if (Test-Path -LiteralPath $Output) {
        if ((Get-FileHash -LiteralPath $Output -Algorithm SHA256).Hash.ToLowerInvariant() -ne $digest) {
            throw 'Output already holds a different APK. Supply a new -Output path to preserve the prior artifact.'
        }
    } else {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null
        Copy-Item -LiteralPath $builtApk -Destination $Output
    }
    if ((Get-FileHash -LiteralPath $Output -Algorithm SHA256).Hash.ToLowerInvariant() -ne $digest) { throw 'Final APK copy hash mismatch.' }
    $leaf = Split-Path -Leaf $Output
    Set-Content -LiteralPath "$Output.sha256" -Value "$digest  $leaf" -Encoding ascii
    @{ package = 'io.github.maddex712.wiids'; versionName = $version.wiidsVersionName;
       versionCode = [int]$version.wiidsVersionCode; sha256 = $digest;
       signerCertificateSha256 = $signer; stagedNativeLibraries = $before;
       validation = 'Build/signature/package/version/non-debuggable/copy verified; device smoke tests recorded separately.'
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$Output.packaging.json" -Encoding utf8
    Write-Output "Release APK: $Output"
    Write-Output "SHA-256: $digest"
} finally {
    $env:JAVA_HOME = $previousJava
    $env:ANDROID_SDK_ROOT = $previousSdkRoot
    $env:ANDROID_HOME = $previousAndroidHome
    $env:PATH = $previousPath
}
