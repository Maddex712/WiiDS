# Stage the Android native build for the app: a stripped libmain.so in android/native/jniLibs and the
# runtime's data files (what a desktop build keeps beside its executable) in android/native/assets.
[CmdletBinding()]
param(
    [string]$BuildDirectory = 'E:\NSMBWPort\wiicompiled\android-build',
    [string]$NativeDirectory = 'E:\NSMBWPort\android\native'
)

$ErrorActionPreference = 'Stop'
$ndk = 'C:\Users\Wither\AppData\Local\Android\Sdk\ndk\29.0.14206865'
$strip = Join-Path $ndk 'toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-strip.exe'

$lib = Join-Path $BuildDirectory 'libmain.so'
if (-not (Test-Path -LiteralPath $lib)) { throw "libmain.so is missing: $lib" }
$libDir = Join-Path $NativeDirectory 'jniLibs\arm64-v8a'
New-Item -ItemType Directory -Force $libDir | Out-Null
$staged = Join-Path $libDir 'libmain.so'
# Only the dynamic symbol table is needed at run time; JNI and SDL_main are found through it.
& $strip --strip-unneeded -o $staged $lib
if ($LASTEXITCODE -ne 0) { throw 'llvm-strip failed' }
# Shared libraries the build made for libmain.so's own dependencies (aurora's libpng16.so). Android
# resolves them from the app's library folder when libmain.so loads; system libraries stay out.
Get-ChildItem -LiteralPath $BuildDirectory -Recurse -Filter '*.so' |
    Where-Object { $_.Name -ne 'libmain.so' } | ForEach-Object {
        & $strip --strip-unneeded -o (Join-Path $libDir $_.Name) $_.FullName
        if ($LASTEXITCODE -ne 0) { throw "llvm-strip failed for $($_.Name)" }
        "bundled $($_.Name)"
    }

$assets = Join-Path $NativeDirectory 'assets\runtime'
if (Test-Path -LiteralPath $assets) { Remove-Item -LiteralPath $assets -Recurse -Force }
New-Item -ItemType Directory -Force $assets | Out-Null
# initial_pipeline_cache.db: NSMBW's pipelines (projects/nsmbw/tools/make_pipeline_seed.py), built
# at the first launch so effects don't stutter the first time they appear.
foreach ($file in 'dsp_coef.bin', 'cacert.pem', 'initial_pipeline_cache.db') {
    Copy-Item -LiteralPath (Join-Path $BuildDirectory $file) -Destination $assets
}
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'wii_bootstrap') -Destination $assets -Recurse

'{0:N1} MB  {1}' -f ((Get-Item $staged).Length / 1MB), $staged
Get-ChildItem $assets -Recurse -File | Measure-Object Length -Sum |
    ForEach-Object { '{0} runtime files, {1:N1} MB' -f $_.Count, ($_.Sum / 1MB) }
