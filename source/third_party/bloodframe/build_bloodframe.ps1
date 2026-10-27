# Builds libbloodframe.so (Lossless Scaling frame generation for Android) from M:\WiiDS-drivers\bloodframe
# (commit eadfad2) with the NDK 29 toolchain. Output: M:\WiiDS-drivers\bloodframe-build\libbloodframe.so,
# which build_android_round6.py then packages with its licences.
$ErrorActionPreference = 'Stop'
$T = 'C:\Users\super\Documents\ChatGPT\PORTING\.tools'
$cmake = "$T\host-tools\cmake\data\bin\cmake.exe"
& $cmake -S M:\WiiDS-drivers\bloodframe -B M:\WiiDS-drivers\bloodframe-build -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$($T -replace '\\','/')/host-tools/bin/ninja.exe" `
    "-DCMAKE_TOOLCHAIN_FILE=$($T -replace '\\','/')/android-sdk/ndk/29.0.14206865/build/cmake/android.toolchain.cmake" `
    -DBLOODFRAME=ON -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30 -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw "configure failed" }
& $cmake --build M:\WiiDS-drivers\bloodframe-build --target bloodframe
if ($LASTEXITCODE -ne 0) { throw "build failed" }
# The link output keeps its debug info; the APK gets a stripped copy.
$strip = (Get-ChildItem "$T\android-sdk\ndk\29.0.14206865\toolchains\llvm\prebuilt\*\bin\llvm-strip.exe" | Select-Object -First 1).FullName
& $strip --strip-unneeded -o M:\WiiDS-drivers\bloodframe-build\libbloodframe.so M:\WiiDS-drivers\bloodframe-build\android\libbloodframe.so
if ($LASTEXITCODE -ne 0) { throw "strip failed" }
Get-Item M:\WiiDS-drivers\bloodframe-build\libbloodframe.so | Select-Object FullName, Length
