param(
    [ValidateRange(1, 32)][int]$BuildJobs = 8,
    [switch]$ConfigureOnly
)
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$source = Join-Path $workspace 'reference/galaxy-android'
$output = Join-Path $workspace 'build/galaxy-thor/native'
$cmake = Join-Path $workspace '.tools/host-tools/cmake/data/bin/cmake.exe'
$ninja = Join-Path $workspace '.tools/host-tools/cmake/data/bin/ninja.exe'
$ndk = Join-Path $workspace '.tools/android-sdk/ndk/29.0.14206865'
$pin = '1abcb975927ecb4189949bd51f81e73a04d97566'
foreach ($required in @($cmake, $ninja, (Join-Path $ndk 'build/cmake/android.toolchain.cmake'), (Join-Path $source 'CMakeLists.txt'))) {
    if (!(Test-Path -LiteralPath $required)) { throw "Missing prerequisite: $required" }
}
$actualPin = & git -C $source rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actualPin.Trim() -ne $pin) { throw "Galaxy source must be pinned to $pin" }
New-Item -ItemType Directory -Force -Path $output | Out-Null
$python = 'C:/Users/super/AppData/Local/Programs/Python/Python312/python.exe'
& $python (Join-Path $PSScriptRoot 'Build-Shaderc.py')
if ($LASTEXITCODE -ne 0) { throw 'Shaderc prerequisite failed' }
& $cmake -S $source -B $output -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_TOOLCHAIN_FILE=$ndk/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-32 -DTARGET_MOBILE=ON -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'Galaxy native configuration failed' }
if (!$ConfigureOnly) {
    & $cmake --build $output --target game main --parallel $BuildJobs
    if ($LASTEXITCODE -ne 0) { throw 'Galaxy native build failed' }
    $libraries = foreach ($name in @('libgame.so', 'libmain.so')) {
        $path = Join-Path $output $name
        if (!(Test-Path -LiteralPath $path)) { throw "Missing build output: $path" }
        [ordered]@{ name = $name; size = (Get-Item -LiteralPath $path).Length; sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
    }
    [ordered]@{
        sourceCommit = $pin
        ndk = '29.0.14206865'
        abi = 'arm64-v8a'
        api = 32
        builtAt = [DateTime]::UtcNow.ToString('o')
        libraries = @($libraries)
        deviceTested = $false
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output '../native-build.json') -Encoding utf8
}
