param([switch]$CompileOnly, [switch]$Debug)
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$appRoot = Join-Path $workspace 'reference/eclipse-launcher-thor/android'
$output = Join-Path $workspace 'build/eclipse-thor'
$gradle = 'C:/Users/super/.gradle/wrapper/dists/gradle-8.8-bin/dl7vupf4psengwqhwktix4v1/gradle-8.8/bin/gradle.bat'
$env:JAVA_HOME = 'C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot'
$env:ANDROID_HOME = Join-Path $workspace '.tools/android-sdk'
$props = @{}
Get-Content (Join-Path $workspace 'release/version.properties') | Where-Object { $_ -match '^\s*([^#=]+)=(.*)$' } | ForEach-Object { $props[$Matches[1].Trim()] = $Matches[2].Trim() }
$versionArgs = "-PwiidsVersionName=$($props['wiidsVersionName']) -PwiidsVersionCode=$($props['wiidsVersionCode'])"
$variant = if ($Debug) { 'debug' } else { 'release' }
$taskVariant = if ($Debug) { 'Debug' } else { 'Release' }
$target = if ($CompileOnly) { ":app:compile${taskVariant}JavaWithJavac" } else { ":app:assemble${taskVariant}" }
$log = Join-Path $output $(if ($Debug) { 'app-package-debug.log' } else { 'app-package.log' })
cmd /c "`"$gradle`" -p `"$appRoot`" --offline -PsmsPortRoot=`"$workspace/reference/sms-eclipse-thor`" -PsmsSdlRoot=`"$workspace/build/sunshine-thor/deps/SDL2-2.32.10`" -PsmsNativeLibs=`"$output/jniLibs`" $versionArgs $target > `"$log`" 2>&1"
if ($LASTEXITCODE) { throw "Eclipse host build failed: $log" }
if ($CompileOnly) { Write-Output 'Eclipse Java host compiled.'; return }
foreach ($name in @('libsms.so', 'libsmsbootstrap.so', 'libSDL2.so', 'libc++_shared.so')) {
    if (!(Test-Path (Join-Path $output "jniLibs/arm64-v8a/$name"))) { throw "Missing Eclipse native library: $name" }
}
$apk = Join-Path $output $(if ($Debug) { 'Eclipse-Thor-debug.apk' } else { 'Eclipse-Thor.apk' })
Copy-Item -LiteralPath "$appRoot/app/build/outputs/apk/$variant/app-$variant.apk" -Destination $apk
$tools = Join-Path $env:ANDROID_HOME 'build-tools/34.0.0'
& "$tools/apksigner.bat" verify $apk
if ($LASTEXITCODE) { throw 'Eclipse APK signature validation failed.' }
Get-FileHash -LiteralPath $apk -Algorithm SHA256
