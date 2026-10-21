# WiiDS 0.9.0-beta.5 — release preparation

This file describes packaging and verification scope. Completed build results
are recorded in the published release notes and inventory.

One signed ARM64 APK brings together the current AYN Thor case launcher, six game
cases, per-game lower screens, settings and physical controls. Package
`io.github.maddex712.wiids`, version `0.9.0-beta.5`, Android version code `905`.

- Mario Kart Wii and Retro Rewind: independent lower-screen course map, standings,
  timer and live racers. Retro Rewind's supported runtime is included; its content
  installs/updates from the team's official servers with compatibility checks.
- New Super Mario Bros. Wii and MSMBWii: the companion HUD and stored-item box.
  The authorised MSMBWii mod archive is bundled and installs after disc import.
- Sunshine and Eclipse: the existing Sunshine companion design and separate
  profiles. Eclipse's runtime and unchanged official 1.1.0 VCDIFF patch are bundled
  in this APK; no additional Eclipse APK is required for the combined app.
- The approved minimal WiiDS icon; white Mario Kart cases, red NSMB cases and
  black GameCube cases; opening-case and lifting/spinning-disc animations.
- The owner's selected current cover/disc artwork, menu music and UI/HUD assets,
  packaged as the exact hash-verified presentation snapshot.
- Rendered Retro Rewind lower-screen map cache, with its map index and referenced
  PNG/JSON receipts. Cached maps still require the loaded course archive's exact
  basename and SHA-256; incompatible/unknown courses retain the normal fallback.
- Existing video, controller, menu-music, updater and diagnostic settings.

Players supply their own supported game discs. Neither an original game image nor
the patched Eclipse image is included.

## Install

Download the signed ARM64 release APK and verify its checksum. Install it on the
AYN Thor, then import the supported discs shown in the application's library.
Existing users should update in place using an APK with the same application ID
and signing certificate. Do not uninstall without backing up your own saves.

1. Import Mario Kart Wii Europe/PAL `RMCP01` revision 0 for Original/Retro Rewind.
2. Import NSMB Wii USA `SMNE01` revision 1 for Original/MSMBWii. Disc metadata must
   come from the player's own dump; no fabricated replacement metadata is supplied.
3. Import Sunshine USA `GMSE01` revision 0. Choose **Install Eclipse** on the
   Eclipse card to apply the bundled patch to the unchanged supported Sunshine
   disc. The installer verifies the full source/patch/result fingerprints.
   Importing the exact supported prepatched Eclipse image is also available.
4. Keep the app open while content/HUD assets are prepared. Eclipse needs at
   least 1.5 GB free for its separate image; updates retain the old image until
   the replacement is verified. Additional storage is needed for imported games
   and downloaded Retro Rewind content.

Original/mod game files and save profiles are separate. Existing standalone apps
are not replaced; their personal saves are not copied into WiiDS automatically.

## Appearance and content

This release carries the exact current cover/disc images, Wii Menu music and
UI/HUD/settings presentation selected by the owner, so a new install gets that
presentation with the application. Its manifest records each file's size and
SHA-256. User overrides remain separate; the packaged snapshot supplies the
default/fallback. The on-device disc/mod art builders remain available for
content that is missing or needs regeneration.

The application icon and approved generated disc artwork are included. The
Eclipse fan disc design is credited to Some-Crappy-Edits; the retail game artwork,
fonts/UI and Wii Menu recording retain Nintendo attribution. See the application
notice for the owner-supplied Eclipse cover's unverified creator. These credits
do not imply Nintendo endorsement or assign the code's GPL licence to artwork.
ROMs, owner saves and private credentials are excluded. The Eclipse patch travels
unchanged with the team's attribution and applicable mod/patcher licence texts.

## Original Sunshine polish

Beta.5 updates Original Sunshine motion, map framing and widescreen HUD anchors.
Eclipse and the other native game libraries remain unchanged from beta.4.
See the beta.5 inventory for the exact source and bounded verification scope.
The complete live damage animation was not visually verified.

## Build the prepared release

Start with the pinned source checkouts and privately generated translations from
supported owner discs. Build/stage the current native runtimes first using the
component build notes. Merge staging replaces only its own two runtime trees and
MSMBWii archive, preserving the presentation snapshot and staged Eclipse patch:

```powershell
python experiments/release/stage_merged.py
python experiments/eclipse-thor/Stage-Embedded.py --bundle-patch --sdk-root .tools/android-sdk
python scripts/Stage-Presentation.py --serial 192.168.15.95:5555 --capture-root build/public-release/presentation-capture --revision thor-2026-10-20-r2
python experiments/release/stage_licenses.py

Push-Location reference/driftdroid/android
.\gradlew.bat assembleRelease -PthorBuild=true -PwiidsApp=true -PthorRetroRewind=true -PwiidsVersionName=0.9.0-beta.5 -PwiidsVersionCode=905 --console=plain --no-daemon
Pop-Location
```

After the native inputs are already staged, the public packaging wrapper performs
the Eclipse/licence staging and exact Gradle invocation, verifies signing,
package/version/non-debuggable status, and writes checksums:

```powershell
.\scripts\Package-PublicRelease.ps1 -JavaHome $env:JAVA_HOME -AndroidSdk .tools/android-sdk -Python python
```

It does not blindly restage or strip other game libraries. Set `-SkipEclipseStaging`
only when the exact embedded Eclipse libraries and patch have already been
staged and reviewed. The wrapper requires a release signing configuration outside
the repository; no debug-key fallback is accepted. For independently signed
builds, pass the SHA-256 of your own certificate using
`-ExpectedCertificateSha256` and create your own signing properties.

The Eclipse archive/patch reproduction helper is included. It downloads the
official pinned mod archive and uses a local Sunshine disc read-only; all image
outputs remain private under `build/`:

```powershell
python -m pip install requests py7zr
python experiments/eclipse-thor/Prepare-Eclipse.py --source "C:\path\to\your\GMSE01.iso" --patcher reference/sms-eclipse-thor/tools/mods/vcdiff.py
```

The source archive includes that helper and the exact patch fingerprints, not the
generated private image or the mod archive itself.

`Stage-Presentation.py` captures a configured Thor during release preparation;
its example address is this owner's device, not a required public endpoint.
Rebuilders use the already included presentation tree from the matching source
archive, so no access to the owner's device is needed. Merge staging preserves
that tree. Capturing another device is an explicit optional authoring step.

The matching source release also includes the exact presentation manifest and
only its listed payloads at `build/wiids-merge/assets/presentation/`. Approved
formats are PNG/JPEG images, WAV audio, JSON, native RGBA settings images and TSV
glyph metadata. The source exporter verifies hashes and sizes before including
this explicit staging tree; other `build/` game files are not exported.

The rendered Retro Rewind cache contains PNG/JSON presentation, not its SZS
course archives. The actual mod pack still installs from official servers.
Publish the named full source ZIP as a release asset so all matching source and
presentation files travel together without adding hundreds of megabytes of
generated map imagery to the repository history.

## Validation and limits

Report the actual final APK's version, signing certificate, SHA-256, installation
and launch checks. Keep previous revision checks separate from new final-build
validation. Do not claim exhaustive course/stage coverage or long-session
stability. Experimental 120 FPS presentation for recompiled Wii games retains
60 Hz guest logic; it is not a 120 Hz physics change.

Attach the final matching source/build material, licences, SHA256SUMS and concise
changelog. Record the final APK digest and bounded combined-app checks before
publishing; older standalone checks do not establish this release's validation.
