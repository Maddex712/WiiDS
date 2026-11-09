[CmdletBinding()]
param([string]$Root = 'M:\WiiDS-brawl')
$ErrorActionPreference = 'Stop'
$stripTool = 'C:\Users\super\Documents\ChatGPT\PORTING\.tools\android-sdk\ndk\29.0.14206865\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-strip.exe'
$nativeLibrary = Join-Path $Root 'android-native\libmain.so'
$strippedLibrary = Join-Path $Root 'android-native\libmain.stripped.so'
$nativeRoot = Join-Path $Root 'android\native'
$discDol = Join-Path $Root 'extract\sys\main.dol'
$expectedDol = 'db4222ace8eccb1d6caa18aac7dec20c62134e0e794c7ba5fb27433e712c9689'
if ((Get-FileHash -LiteralPath $discDol -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedDol) {
    throw 'The source DOL is not Brawl USA revision 2.'
}
& $stripTool --strip-unneeded -o $strippedLibrary $nativeLibrary
if ($LASTEXITCODE -ne 0) { throw 'Native strip failed.' }
$destination = Join-Path $nativeRoot 'jniLibs\arm64-v8a\libmain.so'
Copy-Item -LiteralPath $strippedLibrary -Destination $destination -Force
$digest = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
if ($digest -ne (Get-FileHash -LiteralPath $strippedLibrary -Algorithm SHA256).Hash.ToLowerInvariant()) {
    throw 'Staged native library did not verify.'
}
$receipt = [ordered]@{
    schema = 1; disc_id = 'RSBE01'; disc_revision = 2; dol_sha256 = $expectedDol
    library_sha256 = $digest; library_bytes = (Get-Item -LiteralPath $destination).Length
    source_commit = (& git -C (Join-Path $Root 'src') rev-parse HEAD)
    local_uncommitted_changes = $true; created_utc = [DateTime]::UtcNow.ToString('o')
}
$receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $nativeRoot 'brawl-native.json') -Encoding utf8NoBOM
$receipt | ConvertTo-Json
