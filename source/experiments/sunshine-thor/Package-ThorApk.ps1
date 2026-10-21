[CmdletBinding()]
param([ValidateSet('Release','Debug')][string]$Variant = 'Release')
# Stages the already-built native libraries and packages a signed APK. Same steps as
# Build-Thor.ps1 -PackageOnly, written for Windows PowerShell 5.1 (Gradle's stderr notes are
# not treated as failures) and with a debuggable variant for on-device diagnostics.
$ErrorActionPreference = 'Continue'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$appRoot = Join-Path $workspace 'reference/sms-launcher-thor/android'
$output = Join-Path $workspace 'build/sunshine-thor'
$ndk = Join-Path $workspace '.tools/android-sdk/ndk/29.0.14206865'
$gradle = 'C:/Users/super/.gradle/wrapper/dists/gradle-8.8-bin/dl7vupf4psengwqhwktix4v1/gradle-8.8/bin/gradle.bat'
$env:JAVA_HOME = 'C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot'
$env:ANDROID_HOME = Join-Path $workspace '.tools/android-sdk'
$jni = Join-Path $output 'jniLibs/arm64-v8a'
New-Item -ItemType Directory -Force $jni | Out-Null
foreach ($lib in @('libsms.so','libsmsbootstrap.so','libSDL2.so')) {
    $match = @(Get-ChildItem "$output/android-native" -Filter $lib -Recurse -File)
    if ($match.Count -ne 1) { throw "Expected one native $lib, found $($match.Count)." }
    Copy-Item -LiteralPath $match[0].FullName -Destination (Join-Path $jni $lib) -Force
    & "$ndk/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe" --strip-debug (Join-Path $jni $lib)
    if ($LASTEXITCODE) { throw "Failed to strip $lib" }
}
Copy-Item "$ndk/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" $jni -Force
Remove-Item -LiteralPath "$appRoot/app/build/outputs/apk/release/app-release.apk" -ErrorAction SilentlyContinue
# One WiiDS version for every app (release/version.properties), passed to Gradle.
$wiidsVersion = @{}
Get-Content (Join-Path $workspace 'release/version.properties') | Where-Object { $_ -match '^\s*([^#=]+)=(.*)$' } | ForEach-Object { $wiidsVersion[$Matches[1].Trim()] = $Matches[2].Trim() }
$versionArgs = "-PwiidsVersionName=$($wiidsVersion['wiidsVersionName']) -PwiidsVersionCode=$($wiidsVersion['wiidsVersionCode'])"
$log = Join-Path $output "apk-$($Variant.ToLowerInvariant())-build.log"
cmd /c "`"$gradle`" -p `"$appRoot`" --offline -PsmsPortRoot=`"$workspace/reference/sms-thor`" -PsmsSdlRoot=`"$output/deps/SDL2-2.32.10`" -PsmsNativeLibs=`"$output/jniLibs`" $versionArgs :app:assemble$Variant > `"$log`" 2>&1"
if ($LASTEXITCODE) { throw "APK packaging failed; see $log" }
$tools = Join-Path $env:ANDROID_HOME 'build-tools/34.0.0'
if ($Variant -eq 'Debug') {
    $target = Join-Path $output 'Sunshine-Thor-debug.apk'
    Copy-Item "$appRoot/app/build/outputs/apk/debug/app-debug.apk" $target -Force
} else {
    $target = Join-Path $output 'Sunshine-Thor.apk'
    $signed = "$appRoot/app/build/outputs/apk/release/app-release.apk"
    if (Test-Path -LiteralPath $signed) {
        # Signed by Gradle with the private WiiDS key (%USERPROFILE%/.wiids-release).
        Copy-Item -LiteralPath $signed $target -Force
    } else {
    cmd /c "`"$tools/zipalign.exe`" -f -p 4 `"$appRoot/app/build/outputs/apk/release/app-release-unsigned.apk`" `"$output/Sunshine-Thor-aligned.apk`""
    if ($LASTEXITCODE) { throw 'zipalign failed.' }
    cmd /c "`"$tools/apksigner.bat`" sign --ks `"$env:USERPROFILE/.android/debug.keystore`" --ks-key-alias androiddebugkey --ks-pass pass:android --key-pass pass:android --out `"$target`" `"$output/Sunshine-Thor-aligned.apk`""
    if ($LASTEXITCODE) { throw 'APK signing failed.' }
    }
}
cmd /c "`"$tools/apksigner.bat`" verify `"$target`""
if ($LASTEXITCODE) { throw 'APK signature verification failed.' }
"$target SHA256=$((Get-FileHash $target -Algorithm SHA256).Hash.ToLowerInvariant())"
