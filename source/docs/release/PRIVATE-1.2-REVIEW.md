# Private WiiDS 1.2 source and package review

This profile prepares review artifacts for the owner's selected local **1.2** build. It does not
authorize publication or establish release readiness. The older 1.1 candidate/export scripts remain
historical and must not be reused for this build.

The current integrated Android input is `M:/WiiDS-release-wrap-2026-11-08/android`. The default is frozen
`WiiDS-1.2-candidate3.apk`, SHA-256
`49c54110846b9d2f056ae2ad1973ee16c923da0e39784f301ba2c5871c0e9cdc`.
Any further native/game fixes require a replacement candidate; pass that separately verified
APK and its SHA-256 explicitly. The source manifest records current source hashes, rather than
claiming an independently rebuilt release. The three changed native inputs are additionally bound
to the candidate's audited build receipts.

## Review commands

Run from the PORTING workspace with Python 3.11 or newer:

```powershell
python -I experiments/release/package_1_2_candidate.py
python -I experiments/release/export_1_2_source.py
python -I experiments/release/build_catalog_archive_1_2.py --build
python -I experiments/release/prepare_brawl_1_2.py --regenerate
```

Both write only beneath the private `package-review` folder. The package review inspects the existing
APK's owner certificate, package/version, nondebuggable flag, DEX and 36-library inventory; it does
not build/sign/install. It stages text notices into `package-review/licenses` and reports Android/APK
differences separately. `--stage-notices` is the explicit option for copying the reviewed notices into
the private Android source before a later app build; it never edits an APK. The exporter defaults to **dry run** and
records source coverage without producing a ZIP. `--archive` is an explicit optional source-only
review ZIP action, and refuses to overwrite an existing ZIP. Neither script contacts publication
services, changes Git refs, accesses ADB, or executes historical release scripts. The archive builder
only builds its standalone JNI dependency, and Brawl preparation only regenerates private disc-derived
translation inputs. Neither runs an app build. Regeneration refuses an existing output directory.

The exporter hashes all 91 paused workbench files before and after its work. It omits paused additions
and substitutes retained pre-experiment source for the non-Android modified inputs. Integrated Android
comes from the current private source snapshot: reverting an entire modern frontend file to an older
paused baseline would lose approved changes. The workbench itself is never rewritten. Compare these
choices with the previous public source profile before treating the snapshot as final.

## Native and portable setup inputs

- MKDD uses `mariokartdd-recomp` at `bd36eb982ea58c6651584cc48264876d1aba9546` and its
  `gcn-recomp` submodule at `b2193d7c7ce65c40959cf81859beaacafeebfd99`. The source review includes
  the actual modified runtime, translator/configuration, `experiments/mkdd-thor`, and texture patches.
  Generate translated C and the HUD fingerprint header privately from the supported GM4E01 revision 0
  disc. For Plus, use the pinned 3.3 NTSC patch and `MKDD_PLUS_BUILD=ON`; see `wiids/PLUS.md` inside
  the MKDD experiment for exact input identities. Game data and generated translations are excluded.
- Portable standard setup builds all 16 detailed course maps on device. Plus setup applies the pinned
  patch and stages its separate ISO and all 80 detailed maps together before publishing. The integrated
  `MkddPlusMod`, `MkddArt`, `MkddOverview`, `MkddVcdiff` and profile classes are included exactly as
  inventoried. Portable Plus setup is implemented: Options > Your games > Plus > Choose Plus 3.3
  patch. The actual Kotlin installer passed 10 host checks and the actual Android baker passed all
  80 Plus courses; fresh physical picker-to-install UI acceptance remains pending. The current `PLUS.md`
  describes committed `mkdd/plus/game/lower-art/overview` and the retained legacy fallback paths.
- `experiments/texture-packs` contains the managed PNG/DDS decoder and Galaxy/MKDD/Plus renderer
  patches. `experiments/galaxy-thor/patches/vk_shadergen_{fs,tables}-color-2026-11-08.patch` contains
  the current shader changes. The exporter overlays the current private `texture.cpp`,
  `gx_recorder_tex.cpp`, `gx_recorder.cpp`, `vk_sampler_manager.cpp` and shader-generator compile inputs at their original source paths and places
  their three decoder headers beside them. Those source files already contain the changes; patches
  remain as review/reproduction evidence, not instructions to apply them twice. Review actual native
  receipts for the final build. Retained compile/link command JSON and the binary upstream packages
  remain subject to the component's pinned dependency/build recipe; source coverage alone does not
  establish that a different toolchain produces an identical binary. The current raw-BP sampler patch
  is retained at `experiments/galaxy-thor/patches/raw-bp-filter-2026-11-08.patch`.
- Brawl includes the actual private handwritten runtime/translator/aurora sources, scoped project
  tools/configuration, Android integration JNI bridge, and build tools. Disc data, generated function
  shards, generated address maps/module layout and binary dependencies are excluded. The CC0
  `doldecomp/brawl` symbol/split metadata at `af23077265c70b833c8604a51eba9b64b6381c54` is included
  under `third_party/brawl-decomp-config`, with its license. The regeneration script applies the explicit
  2,771 symbol-name overrides, reconstructs the GPL Gecko handler from its pinned upstream representation
  and 26 explicit adapter word changes, then invokes the included module preparation tools. Its private
  verification reproduced all 126 modules, six normalized-text metadata outputs and both complete binary
  translation inputs exactly. This does not include generated game functions in the source ZIP.
- The catalog archive reader uses libarchive 3.8.11 and XZ 5.8.4. Retained tarball SHA-256 values are
  respectively `900fe9fc7237c2b04f090f3c3648de8fe597525771f5035016537ce2d667d12b` and
  `0014c7886930454fe8bd4228665b51af55eeae560ea135c9c4cd33f55b2591d9`. Their actual source and
  notices are inventoried. `build_catalog_archive_1_2.py` verifies these tarballs, extracts them with
  path/link checks, builds static XZ/libarchive and invokes the new `catalog-archive/CMakeLists.txt`
  JNI target. NDK 27.2/API26/ARM64 settings, dependency switches and exact commands are recorded in its
  build receipt. The standalone build passed privately; it does not automatically replace the retained
  APK library, and its output hash differs from that earlier build. The historical receipt's XZ hash
  omitted two `e` characters; the corrected 64-character value above matches the actual tarball and
  the official upstream GitHub asset digest.

For a source-only rebuild, obtain the two tarballs from these pinned version URLs, place them in a
chosen `--downloads` folder, and pass `--jni-source reference/driftdroid/android/app/src/main/cpp/wiids_catalog_archive.c`.
The driver verifies the hashes before extracting or compiling:

- `https://github.com/libarchive/libarchive/releases/download/v3.8.11/libarchive-3.8.11.tar.gz`
- `https://github.com/tukaani-project/xz/releases/download/v5.8.4/xz-5.8.4.tar.gz`

From an exported snapshot, pass `--source-root third_party/brawl`,
`--config third_party/brawl-decomp-config/RSBE01_02`,
`--handler-inputs third_party/Gecko-codehandler`, `--main-dol <owner main.dol>` and
`--modules <owner files/module>` to `prepare_brawl_1_2.py`. Select a fresh private `--output`
below `--private-root/package-review`. It checks the exact revision2 DOL hash and all126module inputs.
The recipe metadata contains hashes and source-level symbol/adapter changes, never disc instructions.
Pinned upstream handler source references are recorded in `third_party/Gecko-codehandler/PROVENANCE.json`.

## Java and Android build recipe

Use JDK 21, SDK 34, cached Gradle 8.8, and the documented component NDK pins (MKDD 27.2.12479018;
Brawl/Galaxy 29.0.14206865). Build only from an isolated prepared snapshot. Keep the private Java 8
init recipe included as `experiments/release/private-1.2-build/preview.gradle`: MKDD Java must be
compiled with `-source 8 -target 8` before d8. Rebase its output/JDK paths for the isolated checkout.
The main Android project currently stages already built native libraries; Gradle alone does not
rebuild all engines. Do not describe that as a clean native rebuild.

Pass `-PthorBuild=true -PwiidsApp=true -PthorRetroRewind=true -PwiidsVersionName=1.2` and an
explicit reviewed internal `-PwiidsVersionCode=1200` used by candidate3 (advance it if the owner later
selects another internal build). Verify the output
certificate and nondebuggable manifest; never use debug-key fallback for the owner candidate.
No APK build is performed by the review scripts.

## Before a final candidate/source pair

Reconcile every shipped library/DEX with exact source/build receipts, complete Galaxy scene acceptance,
and verify the internal version code. Review newly staged Brawl and XZ notices against the final APK.
Retain the standalone archive build and Brawl regeneration receipts alongside the final source manifest.
They establish source/build paths, not all-game acceptance or a second-machine rebuild.

The source ZIP includes the existing **535 explicitly manifested presentation files** (34,041,452 bytes)
and their manifest only after every byte matches the selected candidate APK. It reuses the previous
exporter's target/format/path/size limits and owner-approved attribution policy. The named approved
master icon is included at its fixed SHA-256 for resource regeneration. Authored content catalog metadata
is included only after matching the APK. This adds no new artwork permissions or original assets.

Discs, generated translations, extracted sections, owner saves, keys, native binaries and all unlisted
private assets remain excluded. The supplemental runtime asset kit records the other 67 exact APK paths,
including approved Dolphin defaults, CA/seed metadata and exact Galaxy ZIP regeneration from pinned
dependencies. See `RUNTIME-ASSETS-1.2.md` for the isolated staging recipe and 602-content/53-license
byte-exact smoke result. A successful inventory
establishes coverage of the selected source roots and approved presentation, not an independently rebuilt
release kit. No publication is authorized.

Candidate1 predates the nine added Brawl/XZ notices. The exporter reports that known mismatch during a
dry run and refuses `--archive` until the selected rebuilt candidate contains the exact current notices.
Pass the final candidate through `--apk <path> --expected-apk-sha256 <verified hash>` to both review
scripts after the root build/acceptance work is finished. This prevents the old APK from being paired
with a source/license snapshot already changed for its successor.

Source ZIP creation also requires `--candidate-audit <root candidate audit JSON>` (default candidate3's audit). The exporter checks
that audit's APK/native hashes, checks every changed native source/header against its actual compiler
receipt, and independently reruns Gradle's `--strip-unneeded` normalization on the three built libraries
before comparing them with the chosen APK. Current Galaxy sampler/recorder inputs must agree with those
receipts; a source edit after the corresponding native build blocks archive creation.

The retained candidate3 source ZIP predates the later `PLUS.md` documentation correction clarifying
implemented portable setup versus pending physical UI acceptance. The correction and current notes
are outside that immutable reviewed archive; no source ZIP was regenerated for this documentation edit.
It also predates the later exact runtime-asset kit described in `RUNTIME-ASSETS-1.2.md`.
