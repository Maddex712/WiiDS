# WiiDS source release

The APK and matching source archive must refer to the same release. This source
candidate contains the current handwritten Android, runtime, translator, disc/art
pipeline and build-tool sources from the explicitly named projects. The manifest
records the upstream commit and every included file hash. Local changes are part
of the snapshot, rather than merely links to unmodified upstream repositories.

## Rebuilding

1. Read `SOURCE-MANIFEST.json` and restore the named upstream repositories at the
   listed commits, including their documented submodules/dependencies. Overlay
   the archived source at the matching paths. The archive omits binary upstream
   dependencies; retrieve them using the respective upstream build instructions.
2. Install the documented Android SDK/NDK, JDK, CMake/Ninja, Python and .NET
   toolchains. Inspect the component build scripts before running them; this is a
   Windows workbench export and some scripts still contain local toolchain paths.
3. Supply your own supported discs. Generate the game translations privately
   using the included translator/unpack/configuration scripts. The archive does
   not contain generated translated game functions or extracted disc sections.
4. Follow `docs/release/STEP1-RELEASE-BASICS.md` through
   `STEP4-5-ONE-APK-AND-MODS.md`, and the NSMB/Sunshine/Eclipse experiment build
   notes. Stage merged libraries and licence notices, then package the Android
   release. Create your own signing key; the owner's key is never included.

For the exact prepared one-APK beta.2 packaging command, see
`docs/release/RELEASE-DRAFT.md` and `scripts/Package-PublicRelease.ps1`. The wrapper
uses an explicit or environment-configured JDK/SDK and already-staged runtimes.
It stages the embedded Eclipse host/bootstrap and verified official patch, then
licences, before invoking the combined app's Gradle release target. It refuses
debug-key fallback and preserves the other staged runtime libraries.

The archive also includes the actual fixed-up Eclipse/BSE/moveset/header sources
under `third_party/`, plus the modified SDL2 source used by the combined launcher.
Their original pins are recorded in the manifest. The upstream Eclipse CMake
configuration normally fetches these pins and reapplies the included fixup script;
the snapshots let reviewers inspect the source actually used by this workbench.

## Publication gate

This is an inspectable source candidate, not a verified clean-room build kit.
Before publishing binaries, match every shipped native library and Android DEX
to its preferred source/build instructions. Include applicable copyright and
licence texts. Supply complete corresponding source for GPL components,
including the scripts needed to generate/build them; merely linking to this
showcase repository or supplying a patch against an unspecified base is
insufficient. Record dependency versions and build flags for the exact release.

The candidate exporter intentionally leaves out all ROMs, extracted assets,
translated game outputs, saves, private logs, keys and binaries. It must not be
described as complete corresponding source until the inventory and generation
path are reviewed. This source snapshot has not been rebuilt from scratch on a
second computer.

## Components and credits

- DriftDroid/WiiCompiled and the NSMB Wii runtimes: GPL-3.0; retain their notices.
- Super Mario Sunshine platform port: MIT (`SoftwareAura LLC`), as recorded in
  the pinned local port's `LICENSE`; Android host MIT; decompilation CC0.
- Super Mario Eclipse mod code: GPL-3.0 in the Eclipse team's source repository.
- Retro Rewind/MSMBWii: retain the recorded author permissions and credits.
- SDL, aurora, Dolphin-derived portions, FreeType and compression/graphics
  dependencies: retain each component's notices and applicable source obligations.

Source and binary licences do not grant redistribution rights to Nintendo art,
music, disc data, or unrelated fan artwork. The application can reconstruct its
game HUD/art from the user's disc and accept optional custom art. The source
archive does not include the owner's private cover/disc/music files.
