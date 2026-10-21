param([switch]$SkipRuntimePatch)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$git = (Get-Command git -CommandType Application -ErrorAction Stop).Source
$referenceRoot = Join-Path $projectRoot 'reference'

# These are private dependency checkouts, not vendored public source. The
# original MKW-3DS tools do not carry an explicit license; retain their NOTICE.
& $git -C $projectRoot check-ignore --no-index -q 'reference/.thor-source-bootstrap'
if ($LASTEXITCODE -ne 0) { throw 'The reference/ dependency directory must be Git-ignored before bootstrap.' }

function Invoke-SourceGit([string[]]$Arguments) {
    & $git @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Source Git operation failed: $($Arguments -join ' ')" }
}

function Initialize-PinnedSource([string]$Url, [string]$Directory, [string]$Commit, [string[]]$SparsePaths = @()) {
    if (Test-Path -LiteralPath $Directory) {
        if (!(Test-Path -LiteralPath (Join-Path $Directory '.git'))) { throw "Existing path is not a source checkout: $Directory" }
        $head = & $git -C $Directory rev-parse HEAD
        if ($LASTEXITCODE -ne 0 -or $head.Trim() -ne $Commit) {
            throw "Existing checkout is not at required pin $Commit; nothing reset or overwritten: $Directory"
        }
        return
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Directory) | Out-Null
    Invoke-SourceGit -Arguments @('clone', '--filter=blob:none', '--no-checkout', $Url, $Directory)
    if ($SparsePaths.Count -gt 0) {
        Invoke-SourceGit -Arguments @('-C', $Directory, 'sparse-checkout', 'init', '--cone')
        Invoke-SourceGit -Arguments (@('-C', $Directory, 'sparse-checkout', 'set', '--') + $SparsePaths)
    }
    Invoke-SourceGit -Arguments @('-C', $Directory, 'fetch', '--depth=1', 'origin', $Commit)
    Invoke-SourceGit -Arguments @('-C', $Directory, 'checkout', '--detach', $Commit)
    $head = & $git -C $Directory rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $head.Trim() -ne $Commit) { throw "Source pin verification failed: $Directory" }
}

$runtime = Join-Path $referenceRoot 'driftdroid'
Initialize-PinnedSource 'https://github.com/dorPXP/driftdroid.git' $runtime '8b33d31016a8b8735fe35b4fb633c491d33774c6'
$parser = Join-Path $referenceRoot 'mkw-3ds'
Initialize-PinnedSource 'https://github.com/MiniBandit/MKW-3DS.git' $parser 'bdacabbd37830086161702d57a9b9d526ff52e56' @('tools', 'include/mkw')
Initialize-PinnedSource 'https://github.com/magcius/noclip.website.git' (Join-Path $parser 'vendor/noclip') '4cb7471c7631370faf5d526a56876d91ba24aca0' @('src')
Initialize-PinnedSource 'https://github.com/LagoLunatic/gclib.git' (Join-Path $parser 'vendor/gclib') '4e06db0004df411f1c806ad39b0e9a18ac4a1de5' @('gclib')

if (!$SkipRuntimePatch) {
    $patch = Join-Path $projectRoot 'patches/driftdroid-ayn-thor.patch'
    if (!(Test-Path -LiteralPath $patch -PathType Leaf)) { throw "Source patch is missing: $patch" }
    & $git -C $runtime apply --reverse --check --whitespace=nowarn $patch 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-Output 'Thor source patch is already applied; existing changes preserved.'
    } else {
        Invoke-SourceGit -Arguments @('-C', $runtime, 'apply', '--check', '--whitespace=nowarn', $patch)
        Invoke-SourceGit -Arguments @('-C', $runtime, 'apply', '--whitespace=nowarn', $patch)
    }
}
Write-Output 'Pinned source dependencies are ready under ignored reference/. No game data or tool binaries downloaded.'
Write-Output 'Read reference/mkw-3ds/NOTICE. Its original tools stay private; do not vendor them into this repository.'
Write-Output 'Install the documented toolchain and parser dependencies separately; build the exporter locally with node tools/bundle_cooker.mjs.'
