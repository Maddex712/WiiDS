# Configure (first run) and build on Windows. Extra arguments are passed to ninja.
#
# Uses clang in its GNU driver mode rather than MSVC: the recompiled game code and the
# runtime rely on GCC-style flags and builtins, and clang handles the very large
# generated translation units better.
# 'Continue', not 'Stop': cmake, ninja and the recompiler all write progress and
# warnings to stderr, and under 'Stop' PowerShell turns that into a fatal error. Native
# failures are detected via $LASTEXITCODE instead.
$ErrorActionPreference = 'Continue'
Set-Location $PSScriptRoot

function Need($name) {
    $c = Get-Command $name -ErrorAction SilentlyContinue
    if (-not $c) { throw "$name not found on PATH. See README.md for requirements." }
    return $c.Source
}

$cc = Need 'clang'
$cxx = (Get-Command 'clang++' -ErrorAction SilentlyContinue).Source
if (-not $cxx) { $cxx = $cc }   # clang drives C++ too, given a .cpp input
Need 'cmake' | Out-Null
Need 'ninja' | Out-Null
Need 'python' | Out-Null

# The shared recompiler and runtime are a submodule.
if (-not (Test-Path gcn-recomp/CMakeLists.txt)) {
    git submodule update --init
    if ($LASTEXITCODE -ne 0) { throw "git submodule update failed" }
}

if (-not (Test-Path build/build.ninja)) {
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo `
        "-DCMAKE_C_COMPILER=$cc" "-DCMAKE_CXX_COMPILER=$cxx" @args
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
    ninja -C build
} else {
    ninja -C build @args
}
if ($LASTEXITCODE -ne 0) { throw "build failed" }
