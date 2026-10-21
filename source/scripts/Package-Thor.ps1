param(
    [ValidateSet('Debug','Release','Both')][string]$Variant='Release',
    [switch]$IncludeRetroRewind
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$runtimeRoot=Join-Path $projectRoot 'reference/driftdroid'
# One WiiDS version for every app (release/version.properties), passed to Gradle.
$wiidsVersion=@{}
Get-Content (Join-Path $projectRoot 'release/version.properties') | Where-Object { $_ -match '^\s*([^#=]+)=(.*)$' } | ForEach-Object { $wiidsVersion[$Matches[1].Trim()]=$Matches[2].Trim() }
$versionArgs=@("-PwiidsVersionName=$($wiidsVersion['wiidsVersionName'])", "-PwiidsVersionCode=$($wiidsVersion['wiidsVersionCode'])")
$nativeRoot=Join-Path $projectRoot 'build/android-native'
$destination=Join-Path $runtimeRoot 'android/app/src/main/jniLibs/arm64-v8a'
$strip=Join-Path $projectRoot '.tools/android-sdk/ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe'
$libraries=@('libWiiCompiled.so','_deps/png-build/libpng16.so')
if($IncludeRetroRewind) {
    # Source-only plumbing: this gate does not establish RR gameplay or lower-map support.
    $graph=Join-Path $runtimeRoot 'generated/build_shards/shards.cmake'
    $resolved=Join-Path $runtimeRoot 'build/mods/retro_rewind_full_cpp/resolved_dispatch_profile.json'
    $modData=Join-Path $runtimeRoot 'build/mods/retro_rewind_full_cpp/cpp/mod_data_patches.cpp'
    $awareness=Join-Path $runtimeRoot 'generated/base_translation_mod_awareness.json'
    $release=Join-Path $runtimeRoot 'android/app/src/main/kotlin/com/driftdroid/android/RetroRewindRelease.kt'
    $retroLibrary=Join-Path $nativeRoot 'libRetroRewind.so'
    foreach($required in @($graph,$resolved,$modData,$awareness,$release,$retroLibrary)) {
        if(!(Test-Path -LiteralPath $required -PathType Leaf)) { throw "Retro Rewind prerequisite missing: $required" }
    }
    $graphText=Get-Content -LiteralPath $graph -Raw
    if($graphText -notmatch 'set\(MKW_HAVE_RETRO_REWIND_SHARDS\s+ON\)' -or
       $graphText -notmatch 'set\(MKW_RETRO_REWIND_FUNCTION_COUNT\s+[1-9][0-9]*\)') {
        throw 'Retro Rewind graph is disabled or empty. Run translate-mod --emit-cpp and emit-build-shards with the RR resolved profile and C++ directory.'
    }
    $resolvedProfile=Get-Content -LiteralPath $resolved -Raw | ConvertFrom-Json
    if($resolvedProfile.Format -ne 'mkwii-resolved-dispatch-profile' -or @($resolvedProfile.Entries).Count -eq 0) {
        throw 'Retro Rewind resolved dispatch profile is invalid or empty.'
    }
    $releaseText=Get-Content -LiteralPath $release -Raw
    if($releaseText -notmatch 'const val CODE_PUL_SHA256\s*=\s*"([0-9a-fA-F]{64})"') {
        throw 'Cannot identify the Android frontend supported Code.pul hash.'
    }
    $supportedCodeHash=$Matches[1]
    $baseAwareness=Get-Content -LiteralPath $awareness -Raw | ConvertFrom-Json
    $compatibleProfiles=@($baseAwareness.profiles | Where-Object {
        $_.profile -eq 'retro-rewind' -and $_.region -eq 'P' -and $_.codePulSha256 -eq $supportedCodeHash
    })
    if($compatibleProfiles.Count -ne 1) {
        throw 'Base translation is not stamped for the supported PAL Retro Rewind Code.pul. Regenerate the base with that pack present.'
    }
    foreach($generated in @($graph,$resolved,$modData,$awareness)) {
        if((Get-Item -LiteralPath $retroLibrary).LastWriteTimeUtc -lt (Get-Item -LiteralPath $generated).LastWriteTimeUtc) {
            throw "libRetroRewind.so predates $generated. Rebuild the standalone RetroRewind target before packaging."
        }
    }
    $libraries+='libRetroRewind.so'
    Write-Output 'Including standalone Retro Rewind. Lower maps require separately staged maps matching the loaded course archive; gameplay validation is separate.'
}
# Check all inputs before staging anything, so an optional RR failure leaves the base staging alone.
foreach($library in $libraries) {
    $source=Join-Path $nativeRoot $library
    if(!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing native output: $source" }
}
New-Item -ItemType Directory -Force -Path $destination | Out-Null
$stagedLibraries=@()
foreach($library in $libraries) {
    $source=Join-Path $nativeRoot $library
    if(!(Test-Path -LiteralPath $source)) { throw "Missing native output: $source" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $destination (Split-Path -Leaf $library))
    $stagedLibraries+=Join-Path $destination (Split-Path -Leaf $library)
}
& $strip --strip-unneeded @stagedLibraries
if($LASTEXITCODE -ne 0) { throw 'Native stripping failed.' }
$variants=if($Variant -eq 'Both') { @('Debug','Release') } else { @($Variant) }
Push-Location -LiteralPath (Join-Path $runtimeRoot 'android')
try {
    foreach($buildVariant in $variants) {
        $retroProperty=if($IncludeRetroRewind) {'true'} else {'false'}
        & .\gradlew.bat "assemble$buildVariant" -PthorBuild=true -PwiidsApp=true "-PthorRetroRewind=$retroProperty" @versionArgs --console=plain
        if($LASTEXITCODE -ne 0) { throw "Android $buildVariant build failed." }
        $variantDirectory=$buildVariant.ToLowerInvariant()
        $apk=Join-Path $runtimeRoot "android/app/build/outputs/apk/$variantDirectory/app-$variantDirectory.apk"
        $name=if($buildVariant -eq 'Release') {'WiiDS.apk'} else {'WiiDS-debug.apk'}
        $artifact=Join-Path $projectRoot "build/$name"
        Copy-Item -LiteralPath $apk -Destination $artifact
        & "$projectRoot/.tools/android-sdk/build-tools/34.0.0/apksigner.bat" verify $artifact
        if($LASTEXITCODE -ne 0) { throw 'APK signature verification failed.' }
        $hash=(Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
        Set-Content -LiteralPath "$artifact.sha256" -Value "$hash  $name" -Encoding ascii
        Write-Output "Local build: $artifact"
    }
} finally { Pop-Location }
