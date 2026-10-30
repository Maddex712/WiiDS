# One-shot Super Paper Mario build: portable tools -> pinned dependencies -> translate -> configure -> compile.
# Run from anywhere:  powershell -ExecutionPolicy Bypass -File <project>\tools\build-spm.ps1
# Everything it downloads is the pinned set WiiCompiled's own Launcher scripts fetch (llvm-mingw, CMake,
# Ninja, SDL3, Dawn and the other Dependencies/ trees); nothing else is installed. Logs: <project>\build-logs\.
[CmdletBinding()]
param(
    [int]$Jobs = 0,
    [switch]$SkipTools,
    [switch]$SkipTranslate,
    [switch]$ConfigureOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0

$root = Split-Path -Parent $PSScriptRoot            # <project>\tools -> <project>
$wc = Join-Path $root 'wiicompiled'
$toolchain = Join-Path $root 'toolchain'
$logs = Join-Path $root 'build-logs'
$project = 'projects/spm/recomp.yml'
if ($Jobs -le 0) { $Jobs = [Environment]::ProcessorCount }
[IO.Directory]::CreateDirectory($logs) | Out-Null
. (Join-Path $wc 'Launcher\NativeBuildFlags.ps1')

function Set-Status([string]$Text) {
    $line = "{0:HH:mm:ss} {1}" -f (Get-Date), $Text
    Write-Host $line
    Add-Content -LiteralPath (Join-Path $logs 'status.txt') -Value $line
}

# Runs a native tool, keeps its output in build-logs\<name>.log and stops on a non-zero exit code.
function Invoke-Step([string]$Name, [string]$Exe, [string[]]$Arguments, [string]$WorkDir) {
    $log = Join-Path $logs "$Name.log"
    Set-Status "$Name: started"
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'            # Windows PowerShell 5.1 turns native stderr into errors
    Push-Location $WorkDir
    try { & $Exe @Arguments 2>&1 | ForEach-Object { "$_" } | Out-File -LiteralPath $log -Encoding utf8 }
    finally { Pop-Location; $ErrorActionPreference = $previous }
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        Set-Status "$Name: FAILED (exit $code), see $log"
        Get-Content -LiteralPath $log -Tail 40
        throw "$Name failed with exit code $code."
    }
    Set-Status "$Name: ok"
}

Remove-Item -LiteralPath (Join-Path $logs 'status.txt') -ErrorAction SilentlyContinue
Set-Status "build started, project $root, $Jobs jobs"

foreach ($required in @((Join-Path $root 'game\main.dol'), (Join-Path $wc 'projects\spm\recomp.yml'))) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing $required" }
}

# 1. Toolchain and dependencies
if (-not $SkipTools) {
    if (-not (Test-Path -LiteralPath (Join-Path $toolchain 'llvm-mingw\bin\x86_64-w64-mingw32-clang++.exe'))) {
        Set-Status 'tools: downloading llvm-mingw, CMake and Ninja'
        & (Join-Path $wc 'Launcher\Prepare-PortableTools.ps1') -Destination $toolchain *>&1 |
            ForEach-Object { "$_" } | Out-File -LiteralPath (Join-Path $logs 'tools.log') -Encoding utf8
    }
    if (-not (Test-Path -LiteralPath (Join-Path $wc 'Dependencies\dawn_prebuilt\bin\webgpu_dawn.dll'))) {
        Set-Status 'dependencies: downloading the pinned source trees and Dawn'
        & (Join-Path $wc 'Launcher\Prepare-Dependencies.ps1') -Destination (Join-Path $wc 'Dependencies') *>&1 |
            ForEach-Object { "$_" } | Out-File -LiteralPath (Join-Path $logs 'dependencies.log') -Encoding utf8
    }
    foreach ($tool in @('llvm-mingw\bin\x86_64-w64-mingw32-clang++.exe', 'CMake\bin\cmake.exe', 'Ninja\ninja.exe')) {
        if (-not (Test-Path -LiteralPath (Join-Path $toolchain $tool))) { throw "Toolchain file missing after setup: $tool (see build-logs\tools.log)" }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $wc 'Dependencies\dawn_prebuilt\bin\webgpu_dawn.dll'))) {
        throw 'Dependencies are incomplete (see build-logs\dependencies.log)'
    }
    Set-Status 'tools and dependencies ready'
}

# 2. Translate (.NET 8 runtime; the translator is already built)
if (-not $SkipTranslate) {
    $dotnet = Get-Command dotnet -ErrorAction SilentlyContinue
    if (-not $dotnet) { throw 'dotnet was not found on PATH (the .NET 8 runtime is enough).' }
    $env:DOTNET_ROLL_FORWARD = 'Major'
    $env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
    $translator = Join-Path $wc 'translator\src\Translator.Cli\bin\Release\net8.0\Translator.Cli.dll'
    Invoke-Step 'translate' $dotnet.Source @($translator, 'translate-recursive', '0x80006124', '--project', $project,
        '--outdir', 'generated/functions', '--output-metadata', 'generated/base_translation_output.json',
        '--production-source-bundle', 'generated/base_translation_sources.bin',
        '--no-function-files', '--prune-stale', '--threads', "$Jobs") $wc
    Invoke-Step 'data-init' $dotnet.Source @($translator, 'generate-data-init', '--project', $project) $wc
    Invoke-Step 'shards' $dotnet.Source @($translator, 'emit-build-shards', '--project', $project,
        '--base-metadata', 'generated/base_translation_output.json', '--base-functions-dir', 'generated/functions',
        '--native-source-dir', 'runtime/src', '--out', 'generated/build_shards') $wc
}

# 3. Configure and compile with the portable toolchain on a scrubbed PATH
$env:PATH = Get-MkwToolchainPath $toolchain
$bin = Join-Path $toolchain 'llvm-mingw\bin'
$build = Join-Path $wc 'native-build'
$configure = Get-MkwNativeConfigureArguments `
    -SourceDirectory (Join-Path $wc 'runtime') -BuildDirectory $build `
    -Ninja (Join-Path $toolchain 'Ninja\ninja.exe') `
    -CCompiler (Join-Path $bin 'x86_64-w64-mingw32-clang.exe') `
    -CxxCompiler (Join-Path $bin 'x86_64-w64-mingw32-clang++.exe') `
    -ResourceCompiler (Join-Path $bin 'x86_64-w64-mingw32-windres.exe') `
    -DependenciesDirectory (Join-Path $wc 'Dependencies') `
    -AdditionalArguments @("-DMKW_TRANSLATED_COMPILE_JOBS=$Jobs")
$cmake = Join-Path $toolchain 'CMake\bin\cmake.exe'
Invoke-Step 'configure' $cmake $configure $wc
if ($ConfigureOnly) { Set-Status 'configure only: done'; exit 0 }
Invoke-Step 'compile' $cmake @('--build', $build, '--target', 'WiiCompiled', '--parallel', "$Jobs") $wc
Set-Status "DONE: $build\WiiCompiled.exe"
