# Package a game's Android NativeActivity library into an installable APK, and optionally
# install it and push the disc image. A game project calls this from its own script with
# its name and package: gcn_add_game(... ANDROID_PACKAGE ...) builds lib<Name>.so and
# writes the manifest into the build directory.
#
# There is no Java source and no Gradle: the framework's android.app.NativeActivity
# loads the library directly, so this is aapt2 -> zip -> zipalign -> apksigner.
param(
    [Parameter(Mandatory)][string]$Name,      # gcn_add_game's NAME
    [Parameter(Mandatory)][string]$Package,   # gcn_add_game's ANDROID_PACKAGE
    [Parameter(Mandatory)][string]$Title,     # for the debug key and the install hint
    [string]$BuildDir = 'build-android',      # relative to the current directory
    [string]$ImageDir = 'rom',
    [switch]$Install,
    [string]$Abi = 'arm64-v8a'
)

# 'Continue', not 'Stop': the SDK tools write progress to stderr, which would otherwise be
# turned into fatal errors. Failures are caught via $LASTEXITCODE.
$ErrorActionPreference = 'Continue'

$sdk = "$env:LOCALAPPDATA\Android\Sdk"
$env:JAVA_HOME = (Get-ChildItem "$env:ProgramFiles\Microsoft" -Directory -ErrorAction SilentlyContinue |
                  Where-Object Name -match 'jdk' | Select-Object -Last 1).FullName
if (-not $env:JAVA_HOME) { throw "No JDK found under $env:ProgramFiles\Microsoft (winget install Microsoft.OpenJDK.21)." }

$bt = Get-ChildItem "$sdk\build-tools" -Directory -ErrorAction SilentlyContinue |
      Sort-Object Name -Descending | Select-Object -First 1
if (-not $bt) { throw "No build-tools under $sdk\build-tools." }
$platform = Get-ChildItem "$sdk\platforms" -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending | Select-Object -First 1
if (-not $platform) { throw "No platform under $sdk\platforms." }

$aapt2    = Join-Path $bt.FullName 'aapt2.exe'
$zipalign = Join-Path $bt.FullName 'zipalign.exe'
$apksigner= Join-Path $bt.FullName 'apksigner.bat'
$androidJar = Join-Path $platform.FullName 'android.jar'
$keytool  = Join-Path $env:JAVA_HOME 'bin\keytool.exe'

$so = Join-Path $BuildDir "lib$Name.so"
if (-not (Test-Path $so)) { throw "$so not found. Build for Android first." }
$manifest = Join-Path $BuildDir 'AndroidManifest.xml'
if (-not (Test-Path $manifest)) { throw "$manifest not found; gcn_add_game writes it when given ANDROID_PACKAGE." }

$out = Join-Path $BuildDir 'apk'
New-Item -ItemType Directory -Force $out | Out-Null
$unsigned = Join-Path $out "$Name-unsigned.apk"
$aligned  = Join-Path $out "$Name-aligned.apk"
$signed   = Join-Path $out "$Name.apk"
foreach ($f in $unsigned, $aligned, $signed) { if (Test-Path $f) { Remove-Item $f } }

# 1. Manifest -> base APK. No resources, so aapt2 only has the manifest to link.
& $aapt2 link -o $unsigned -I $androidJar --manifest $manifest `
    --min-sdk-version 29 --target-sdk-version 34
if ($LASTEXITCODE -ne 0) { throw "aapt2 link failed" }

# 2. Add the native libraries: ours, plus the OpenXR loader it links against, which
#    FetchContent unpacked from the Khronos AAR.
$libs = @{ "lib/$Abi/lib$Name.so" = (Resolve-Path $so).Path }
$loader = Get-ChildItem -Recurse -Filter 'libopenxr_loader.so' "$BuildDir/_deps" -ErrorAction SilentlyContinue |
          Where-Object { $_.FullName -match [regex]::Escape($Abi) } | Select-Object -First 1
if ($loader) {
    $libs["lib/$Abi/libopenxr_loader.so"] = $loader.FullName
    Write-Host "bundling $($loader.Name)"
} else {
    Write-Warning "libopenxr_loader.so not found under $BuildDir/_deps - the app will fail to load"
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::Open((Resolve-Path $unsigned), 'Update')
foreach ($entry in $libs.GetEnumerator()) {
    [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $entry.Value, $entry.Key)
}
$zip.Dispose()

# 3. Align, then sign with a local debug key (created once).
& $zipalign -f 4 $unsigned $aligned
if ($LASTEXITCODE -ne 0) { throw "zipalign failed" }

$ks = Join-Path $out 'debug.keystore'
if (-not (Test-Path $ks)) {
    & $keytool -genkeypair -keystore $ks -storepass android -keypass android `
        -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 `
        -dname "CN=$Title Debug, O=gcn-recomp, C=US"
    if ($LASTEXITCODE -ne 0) { throw "keytool failed" }
}
& $apksigner sign --ks $ks --ks-pass pass:android --key-pass pass:android `
    --ks-key-alias androiddebugkey --out $signed $aligned
if ($LASTEXITCODE -ne 0) { throw "apksigner failed" }

Write-Host "built $signed ($([math]::Round((Get-Item $signed).Length/1MB,1)) MB)"
if (-not $Install) { Write-Host "re-run with -Install to install on an attached device"; exit 0 }

$adb = Join-Path $sdk 'platform-tools\adb.exe'
$devices = & $adb devices | Select-Object -Skip 1 | Where-Object { $_ -match '\sdevice$' }
if (-not $devices) { throw "No device in 'device' state." }

# -d: a debuggable package may be installed over one with a higher version code, which
# is what a build from before a manifest change is.
& $adb install -r -d $signed
if ($LASTEXITCODE -ne 0) { throw "adb install failed" }

# The image lives in the app's external files directory, which needs no runtime
# permission. It is the user's own disc image and is never part of the APK.
$iso = (Get-ChildItem "$ImageDir/*" -Include *.iso, *.ciso | Select-Object -First 1)
if (-not $iso) { throw "No .iso or .ciso in $ImageDir/" }
$image = "game$($iso.Extension)"
$dataDir = "/sdcard/Android/data/$Package/files"
& $adb shell mkdir -p $dataDir
# stat returns nothing when the file isn't there yet, so guard against null.
$have = "$(& $adb shell "stat -c %s $dataDir/$image 2>/dev/null")".Trim()
if ($have -ne "$($iso.Length)") {
    Write-Host "pushing game image ($([math]::Round($iso.Length/1GB,2)) GB)..."
    & $adb push $iso.FullName "$dataDir/$image"
    if ($LASTEXITCODE -ne 0) { throw "pushing the image failed" }
} else {
    Write-Host "game image already in place"
}

Write-Host ""
Write-Host "Installed. On the headset: Library -> Unknown Sources -> $Title"
Write-Host "Logs:  adb logcat -s $Name"
