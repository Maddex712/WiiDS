# WiiDS 0.9.0-beta.2 — release preparation

**Final combined-build device checks are pending.** This file describes the
prepared release, not a completed final-build playthrough.

One signed ARM64 APK brings together the current AYN Thor case launcher, six game
cases, per-game lower screens, settings and physical controls. Package
`io.github.maddex712.wiids`, version `0.9.0-beta.2`, Android version code `901`.

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

Game-derived HUD, font, map and menu assets are reconstructed from the player's
disc/mod installation. Optional user-selected artwork remains user content. The
release does not bundle ROMs, owner saves or private credentials. Record any
separately bundled authorised mod assets explicitly in the release inventory.

The owner's private retail/fan cover scans and Wii Menu recording are not bundled.
Disc-derived case compositions and title audio provide the initial experience;
private artwork/audio can remain optional user customisation. The approved new
minimal application icon is included. The Eclipse patch travels unchanged with
the team's attribution and applicable mod/patcher licence texts.

## Build the prepared release

Start with the pinned source checkouts and privately generated translations from
supported owner discs. Build/stage the current native runtimes first using the
component build notes. The ordinary merge staging command below replaces its
own asset staging directory, so run it **before** Eclipse patch staging:

```powershell
python experiments/release/stage_merged.py
python experiments/eclipse-thor/Stage-Embedded.py --bundle-patch --sdk-root .tools/android-sdk
python experiments/release/stage_licenses.py

Push-Location reference/driftdroid/android
.\gradlew.bat assembleRelease -PthorBuild=true -PwiidsApp=true -PthorRetroRewind=true -PwiidsVersionName=0.9.0-beta.2 -PwiidsVersionCode=901 --console=plain --no-daemon
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

## Validation and limits

Report the actual final APK's version, signing certificate, SHA-256, installation
and launch checks. Keep previous revision checks separate from new final-build
validation. Do not claim exhaustive course/stage coverage or long-session
stability. Experimental 120 FPS presentation for recompiled Wii games retains
60 Hz guest logic; it is not a 120 Hz physics change.

Attach the final matching source/build material, licences, SHA256SUMS and concise
changelog. Record the final APK digest and bounded combined-app checks before
publishing; older standalone checks do not establish this release's validation.
