# Rebuild libmain.so, stage it, package the APK and install it on the connected phone.
[CmdletBinding()]
param([int]$Jobs = 12, [switch]$SkipNative)

$ErrorActionPreference = 'Stop'
$logs = 'E:\NSMBWPort\build-nsmbw\logs'
if (-not $SkipNative) {
    & 'E:\NSMBWPort\tools\build-android.ps1' -Target WiiCompiled -Jobs $Jobs -LogPath (Join-Path $logs 'build-android-deploy.log')
    if ($LASTEXITCODE -ne 0) {
        Get-Content (Join-Path $logs 'build-android-deploy.log') | Select-String -Pattern ' error: |FAILED' |
            Select-Object -First 20 | ForEach-Object Line
        throw 'native build failed'
    }
}
& 'E:\NSMBWPort\tools\stage-android.ps1' | Out-Null

$env:JAVA_HOME = 'C:\Users\Wither\tools\jdk17'
$env:ANDROID_HOME = 'C:\Users\Wither\AppData\Local\Android\Sdk'
$env:PATH = "$env:JAVA_HOME\bin;$env:PATH"
$ErrorActionPreference = 'Continue'
& 'C:\Users\Wither\tools\gradle-8.7\bin\gradle.bat' -p 'E:\NSMBWPort\android' assembleDebug --offline --no-daemon --console=plain 2>&1 |
    ForEach-Object { "$_" } | Out-File -LiteralPath (Join-Path $logs 'gradle-apk.log') -Encoding utf8
if ($LASTEXITCODE -ne 0) {
    Get-Content (Join-Path $logs 'gradle-apk.log') | Select-String -Pattern 'error|What went wrong' -Context 0,4 |
        Select-Object -First 10 | ForEach-Object { $_.Line; $_.Context.PostContext }
    throw 'APK build failed'
}
& 'C:\Users\Wither\AppData\Local\Android\Sdk\platform-tools\adb.exe' install -r 'E:\NSMBWPort\android\app\build\outputs\apk\debug\app-debug.apk' 2>&1 |
    Select-Object -Last 1
