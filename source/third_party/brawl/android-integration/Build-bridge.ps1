$ErrorActionPreference = 'Stop'
$mergeRoot = Split-Path -Parent $PSScriptRoot
$ndkRoot = 'C:\Users\super\Documents\ChatGPT\PORTING\.tools\android-sdk\ndk\29.0.14206865\toolchains\llvm\prebuilt\windows-x86_64'
$compiler = Join-Path $ndkRoot 'bin\clang++.exe'
$output = Join-Path $mergeRoot 'android\app\src\main\jniLibs\arm64-v8a\libwiids_brawl_jni.so'
if (!(Test-Path -LiteralPath $compiler)) { throw 'The existing NDK 29 compiler is required.' }
& $compiler --target=aarch64-linux-android30 "--sysroot=$ndkRoot\sysroot" -shared -fPIC -O2 '-Wl,-z,max-page-size=16384' '-Wl,-soname,libwiids_brawl_jni.so' -o $output (Join-Path $PSScriptRoot 'brawl_jni.cpp') -llog -ldl
if ($LASTEXITCODE -ne 0) { throw 'JNI bridge build failed.' }
Get-FileHash -LiteralPath $output -Algorithm SHA256
