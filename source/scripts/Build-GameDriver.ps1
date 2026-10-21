param(
    [string]$AndroidSdkRoot = $(if ($env:THOR_ANDROID_SDK_ROOT) { $env:THOR_ANDROID_SDK_ROOT } else { Join-Path (Split-Path -Parent $PSScriptRoot) '.tools/android-sdk' }),
    [string]$BuildToolsVersion = '34.0.0',
    [string]$AndroidApi = '34'
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$out=Join-Path $projectRoot 'build/game-driver'
$tools=Join-Path $AndroidSdkRoot "build-tools/$BuildToolsVersion"
$jar=Join-Path $AndroidSdkRoot "platforms/android-$AndroidApi/android.jar"
foreach($required in @($jar,"$tools/d8.bat","$tools/aapt2.exe","$tools/zipalign.exe","$tools/apksigner.bat")) {
    if(!(Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing Android build prerequisite: $required" }
}
New-Item -ItemType Directory -Force -Path "$out/classes", "$out/dex" | Out-Null
& javac -source 17 -target 17 -classpath $jar -d "$out/classes" "$projectRoot/diagnostics/driver/Driver.java"
if($LASTEXITCODE -ne 0) { throw 'Game driver compile failed' }
& jar cf "$out/classes.jar" -C "$out/classes" .
& "$tools/d8.bat" --min-api 29 --lib $jar --output "$out/dex" "$out/classes.jar"
if($LASTEXITCODE -ne 0) { throw 'Game driver DEX failed' }
& "$tools/aapt2.exe" link -I $jar --manifest "$projectRoot/diagnostics/driver/AndroidManifest.xml" -o "$out/unsigned.apk"
if($LASTEXITCODE -ne 0) { throw 'Game driver APK linking failed' }
& jar uf "$out/unsigned.apk" -C "$out/dex" classes.dex
New-Item -ItemType Directory -Force -Path "$out/native/lib/arm64-v8a" | Out-Null
Copy-Item -LiteralPath "$out/libthorprobe.so" -Destination "$out/native/lib/arm64-v8a/libthorprobe.so"
& jar uf "$out/unsigned.apk" -C "$out/native" lib
& "$tools/zipalign.exe" -f 4 "$out/unsigned.apk" "$out/aligned.apk"
& "$tools/apksigner.bat" sign --ks "$env:USERPROFILE/.android/debug.keystore" --ks-key-alias androiddebugkey --ks-pass pass:android --key-pass pass:android --out "$out/thor-driver.apk" "$out/aligned.apk"
if($LASTEXITCODE -ne 0) { throw 'Game driver signing failed' }
& "$tools/apksigner.bat" verify "$out/thor-driver.apk"
if($LASTEXITCODE -ne 0) { throw 'Game driver verification failed' }
