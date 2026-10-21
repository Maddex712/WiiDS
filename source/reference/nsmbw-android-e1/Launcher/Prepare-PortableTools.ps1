[CmdletBinding()]
param([string]$Destination)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0

$packages = @(
    [pscustomobject]@{
        Name = 'llvm-mingw'; File = 'llvm-mingw-20260616-ucrt-x86_64.zip'
        Uri = 'https://github.com/mstorsjo/llvm-mingw/releases/download/20260616/llvm-mingw-20260616-ucrt-x86_64.zip'
        Sha256 = 'b9b68a4d276e16fa25802aaba458e4638f64b3884c290aaccdc2d87083b6ca35'
        Root = 'llvm-mingw-20260616-ucrt-x86_64'
    },
    [pscustomobject]@{
        Name = 'CMake'; File = 'cmake-4.3.3-windows-x86_64.zip'
        Uri = 'https://github.com/Kitware/CMake/releases/download/v4.3.3/cmake-4.3.3-windows-x86_64.zip'
        Sha256 = '935ade9e5e8723583c07f44c5592cea2a1c8f65c56ca7e07b34c025c880e0bd6'
        Root = 'cmake-4.3.3-windows-x86_64'
    },
    [pscustomobject]@{
        Name = 'Ninja'; File = 'ninja-win-1.13.2.zip'
        Uri = 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip'
        Sha256 = '07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65'
        Root = $null
    }
)

if (-not $Destination) { $Destination = Join-Path $PSScriptRoot 'artifacts\portable-tools' }
$Destination = [IO.Path]::GetFullPath($Destination)
$downloads = Join-Path $PSScriptRoot 'artifacts\downloads'
[IO.Directory]::CreateDirectory($Destination) | Out-Null
[IO.Directory]::CreateDirectory($downloads) | Out-Null

foreach ($package in $packages) {
    $archive = Join-Path $downloads $package.File
    if (-not (Test-Path -LiteralPath $archive -PathType Leaf) -or
        (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $package.Sha256) {
        Write-Host "Downloading $($package.Name)..."
        $temporary = $archive + '.partial'
        Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
        Invoke-WebRequest -UseBasicParsing -Uri $package.Uri -OutFile $temporary
        $actual = (Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne $package.Sha256) { throw "$($package.Name) hash mismatch: $actual" }
        Move-Item -LiteralPath $temporary -Destination $archive -Force
    }

    $target = Join-Path $Destination $package.Name
    if (Test-Path -LiteralPath $target -PathType Container) { continue }
    $extract = Join-Path $Destination ('.extract-' + $package.Name)
    if (Test-Path -LiteralPath $extract) { Remove-Item -LiteralPath $extract -Recurse -Force }
    Expand-Archive -LiteralPath $archive -DestinationPath $extract
    $source = if ($package.Root) { Join-Path $extract $package.Root } else { $extract }
    if (-not (Test-Path -LiteralPath $source -PathType Container)) { throw "$($package.Name) archive layout changed." }
    Move-Item -LiteralPath $source -Destination $target
    if (Test-Path -LiteralPath $extract) { Remove-Item -LiteralPath $extract -Recurse -Force }
}

$license = @"
Portable build tools bundled by WiiCompiled

LLVM-MinGW 20260616 (LLVM/Clang/LLD, mingw-w64, libc++)
  https://github.com/mstorsjo/llvm-mingw
  Licenses are included in the llvm-mingw directory.

CMake 4.3.3
  https://cmake.org/
  License is included in the CMake directory.

Ninja 1.13.2
  https://github.com/ninja-build/ninja
  Apache License 2.0; see the installer license inventory.
"@
$license | Set-Content -LiteralPath (Join-Path $Destination 'README-LICENSES.txt') -Encoding UTF8
Write-Host "Portable tools ready: $Destination"
