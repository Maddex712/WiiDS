# Claude handoff — Kaizo Rebalanced companion and case polish

The owner asked for Kaizo Mario Galaxy Rebalanced 2.1 inside WiiDS with the same
accurate lower-screen idea as the existing Galaxy design. This task owns engine,
import, isolation and verification. Claude can polish presentation using that
design as the baseline. Do not message or operate another assistant automatically.

The local engine slice is complete, including bounded physical Thor checks.
Final installed owner-signed, nondebuggable APK: code906, `1.0-kaizo-dev`, SHA256
`13ad3816d77b2b27ade61066241713d112bf49df175e328cd8b0e3111ded02b9`.
Earlier candidates, particularly `c06635fb…`, are superseded before the native
external-path isolation fix. Public WiiDS1.0 was not changed or republished.

## Current implementation

- One WiiDS APK; Kaizo is case **7**, alongside original Galaxy case **6**, in
  family bundle `[6,7]`. Earlier case indices are unchanged.
- Package-private `KaizoActivities.Launch`, `.Game`, `.Import` use separate
  `:galaxy_kaizo`, `:galaxy_kaizo_game`, `:galaxy_kaizo_import` processes.
- `GalaxyStorage(Context, true)` scopes **both** internal and external files to
  `galaxy-kaizo`; original uses `galaxy`. NAND/settings/cache/locks/import journals
  are independent. Never point Kaizo at the original profile or reuse its caches.
- `platform/src/port/kaizo.cpp` selects the variant before `MobileApp::run` using
  the scoped Activity paths plus a verified imported `sys/KaizoTables.txt` header.
  It loads named native tables; it never runs or writes Wii instructions.
- All 25 active USA memory sites have native equivalents; see
  [PATCH-MAPPING.md](PATCH-MAPPING.md). They include comet/unlock tables, boss and
  projectile behavior, flip-panel capacity, hidden stars, death Star Bit reset,
  and Mailtoad's 99-life reward. These remain engine-owned.
- Setup uses the owner's clean USA RMGE01 revision0 disc and exact creator ZIP
  (SHA256 `b0b43ee39e3b2c2b7a3dcc4bc33d77718cb97da4b95f490146ec8a80ceddd548`).
  A SAF picker and official download link are provided; patch assets are **not**
  bundled while adaptation/redistribution terms are unspecified. Credits are
  copied into the private profile. Never include game data/NAND in source or APKs.
- The archive's SD save redirect is implemented as independent native NAND. The
  optional Luigi-unlocked author save is not seeded; new normal progress is the
  default. Original player progress is never imported into Kaizo automatically.
- `tools/cook/kaizo_patch.py` overlays the private extraction before normal
  cooking, reconstructs its FST, converts GalaxyID data, and emits portable event/
  comet tables. The normal `LowerAssets.build` then sees the **modified** files.
  New file/model paths must be in this rebuilt catalogue.
- All **157** lower files / **44** planet sets are generated per profile. In the
  host fixture, 43 planet-set files differ from vanilla. A repeated vanilla view
  is not an acceptable substitute for mod geometry, placements or actor state.

## Existing design and exact source entrypoints

Canonical Java templates are in `experiments/galaxy-thor/android/`; staging copies
go to `reference/driftdroid/android/app/src/main/java/dev/thor/galaxy/` via
`Stage-Integrated.py`. Edit the templates, then stage; avoid divergent copies.

- `GalaxyLowerScreen.java`: phase-aware space/chart/Observatory ledger, Gardens
  flat map, mission/star information, real counters, player marker, touch orbit
  and follow behavior. This is the existing Claude design to preserve.
- `GalaxyPlanetRenderer.java`: a transparent **TextureView**, GLES3 on a dedicated
  HandlerThread/EGL context, depth tested and preferably 4xMSAA. Each mesh uploads
  once; placements draw with live matrices. Keep dirty-frame scheduling, existing
  ~60Hz draw budget (16ms minimum interval), and resource teardown behavior.
- `GalaxyPlanets.java`: GPL4 mesh/placement reader, live-state matching and camera
  frames. Preserve model/zone identity; model availability is data-dependent.
- `GalaxyActivity.java`: shared NativeActivity, physical presentation selection,
  `GalaxyLowerScreen.LowerSource`, JNI calls, settings, pause/resume and exit.
  `KaizoActivities.Game` only selects its profile; do not copy the renderer into
  a second implementation.
- `GalaxySettingsDialog.java` / `GalaxySettingsMenu.java`: same visual language
  and interactions. Settings must continue writing the selected profile.
- Frontend case/catalog work: `ThorGameLaunch.kt`, `ModePickerActivity.kt`,
  `ThorLibraryCompanionView.kt`, `rom/GameCatalog.kt`, `GamesActivity.kt`.
- Current Kaizo case uses art prefix `kaizo` with the launcher's normal text
  fallback. No bespoke cover/disc/spine was authored. This is an explicit
  presentation opportunity; never overwrite original Galaxy artwork.

Layering matters: `GalaxyLowerScreen.layered()` places Canvas space/chart first,
the transparent planet TextureView second, and the `Over` view (marker, counters,
black veil) last. Retain title/story/cutscene veils and touch routing. The lower
Presentation is touchable but **not focusable**, so controls stay with the game.

## Live APIs: source of truth

Do not derive progress from screenshots or invent counters. JNI entrypoints are
registered on `GalaxyActivity` and inherited by the Kaizo Activity:

- `nativeLowerState(ByteBuffer)` returns a seqlock sequence. Layout is
  `platform/include/port/thor_lower.h`, version2, little endian. Inspect validity
  bits before using phase/stage/scenario/star lists, ledger, counters or Mario
  position/up vector. Flags identify Luigi, Observatory, storybook and selected
  mission state. All names come from the mod's loaded messages/scenarios.
- `nativeWorldState(ByteBuffer, haveSequence)` uses
  `platform/include/port/thor_world.h`, version1: header/generation/zone names,
  then up to4096 72-byte pieces. Key is **zone name + placement l_id + child**;
  a group's children use1,2,… . Flags are alive, hidden, matrix and upper-camera
  clipped. A clipped piece can still appear on the lower view. Transform is
  row-major3x4 **without scale**, followed by separate scale. Never apply it twice.
- `nativeSpace(ByteBuffer, haveSequence, int[] mode)` supplies the game's live
  space image. Preserve current decode/validity behavior; do not replace it with
  fabricated animation or a performance claim.

Producers: `decomp/src/Game/System/ThorLowerState.cpp`, `ThorWorldState.cpp`,
`platform/mobile/thor/ThorLowerBridge.cpp`. Bake:
`Bake-LowerPlanets.py`, `Bake-LowerMaps.py`, `LowerAssets.py`, `lowermap/` and the
actual NameObjFactory class classification. The current renderer does not render
all enemies/NPCs, player animation or every animated material. Some unmatched
pieces/materials remain known limits; do not hide those by drawing invented data.

## Native profile paths — keep this fix

The first physical save exposed inherited hardcoded external paths even though
the Activity's internal NAND was scoped. `port/profile.h` and `profile.cpp` now
configure the actual Activity roots before variant setup and `MobileApp::run`.
Mirror/restore/quick saves, manual save sync, texture overrides, shader manifests,
cache fallback and crash logs use these roots. Kaizo's manual exchange folder is
`Download/WiiDS-Kaizo`; it does not scan unrelated Dolphin saves. Path strings use
`PortHostAllocScope`. Never reintroduce a fixed original-Galaxy storage path.

`Apply-ProfileIsolation.py` and `profile-path-audit.json` record the affected files.
The actual `profile.cpp` + `nand.cpp` Android regression writes different original
and mod saves and confirms both NAND and mirror bytes remain separate. The final
packaged ARM64 ELF is audited against staging and for absence of fixed original
profile paths. Obsolete `platform/android/reserve.c` is not built or packaged;
its historical wrap/preloader driver hooks are outside the active runtime.

The task-created misplaced mirror was preserved on the computer and renamed
intact on the Thor before original migration. No pre-existing original/helper
save was overwritten. See `accidental-mirror-preserved.json`.

## Build and run locally

Workspace: `C:/Users/super/Documents/ChatGPT/PORTING`.
Python: `C:/Users/super/AppData/Local/Programs/Python/Python312/python.exe`.
JDK21: `C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot`.

Native CMake caches are `build/galaxy-integration/native` (ARM64) and `native-x86`;
canonical native source is `reference/galaxy-android`, pinned upstream1abcb975… .
The established build mirror is `build/galaxy-integration/source`; ensure modified
files are synchronized and inspect compile database paths before building.

1. Build `game` and `main` with the cached CMake/Ninja setup.
2. `Build-Importer.py` rebuilds importer/native Python bridge and code-only assets.
3. `Stage-Integrated.py --native-dir build/galaxy-integration/native` stages Java,
   game/importer libraries and source scripts. It does not stage commercial dumps.
4. Run Gradle `assembleRelease` with `-PthorBuild=true -PwiidsApp=true
   -PthorRetroRewind=true -PwiidsVersionName=1.0-kaizo-dev
   -PwiidsVersionCode=906` for this **reversible physical test**. Do not increase
   its device version code casually: the old nondebuggable build must be restorable
   in place. Owner signing config stays outside the repository; never print it.
5. `Export-Source.py` exports reviewed native/frontend patches and checks them
   without changing Git indexes. It now includes GamesActivity and GameCatalog.

IMPORTANT: actual Thor-before Sunshine `libsms.so` differs from public1.0. This
local build intentionally retains the owner's installed library (SHA256 in the
preservation receipts), staged under app/src/main/jniLibs/arm64-v8a. Do not run a
broad restaging step that replaces it with another Sunshine build. No Sunshine
source was changed by this task; do not claim a full source ZIP matches that
unchanged historical binary without tracing its existing provenance.

## Verification and evidence

Authoritative final device result is `build/galaxy-kaizo/` receipts and the final
dated MODLOG entry. Treat any work-in-progress note as pending until it names a
passed capture/check. Current established checks:

- ARM64 and x86 engine builds; combined nondebuggable APK builds.
- 67 actual-source transaction/migration assertions, including seven injected
  move failures and rejection of bad checksums/traversal.
- Actual `kaizo.cpp` + actual `GameEventFlagTable.cpp` Android x86 regression:
  vanilla unchanged,24 guarded overrides,four terminated schedules,repeat safe.
- Full fresh in-app x86 own-disc/official-patch setup completed, including157
  lower files; scoped native profile loaded. Emulator gameplay was not established
  before its transport disappeared. No physical claim follows from that test.
- All13 native libraries from the actual previous Thor APK are retained in the
  preserved candidate. Other presentation assets remain checked against1.0.
- Physical backups include the actual old APK and helper NAND;50 readable save/
  settings/art hashes.22 protected paths and nondebuggable main internal files
  were inaccessible to shell, explicitly documented. No uninstall or data clear.

Physical Kaizo setup completed with the exact creator ZIP and verified owner ISO;
it omitted movies for storage and generated157lowerfiles. The first candidate ran
Gardens gameplay, movement, an airborne A jump, live lower-map marker, Start pause/
resume and Home/recents resume. Its normal new zero-star file reloaded, but the
external mirror bug made that candidate unsuitable as the final build.

On the corrected final APK, Kaizo loaded its scoped tables/NAND/cache and the
existing new file. A normal icon change from Peach to Yoshi committed through the
actual NAND writer into **galaxy-kaizo/GalaxyData/saves/GameData.bin**. Original
Galaxy's external mirror stayed absent. A fresh native process reloaded Yoshi,
the new timestamp and0stars. Both final Kaizo processes exited cleanly. The mod
mirror SHA stayed unchanged after the original Galaxy test.

The original profile was copied with a per-file SHA-verified2545-file backup of
vanilla cooked data, lower assets and the existing helper NAND/settings. Its old
Luigi file (Mario icon,0stars,1banked Star Bit, old timestamp) loaded separately.
Original Gateway gameplay and the textured GPU planet rendered on the corrected
ARM64 APK;195live objects matched105/109bakedpieces. Movement was sampled and
Return shut down cleanly. The original helper was never modified.

`thor-after-preservation.json` confirms the installed APK hash, all50readable
previous save/settings/art hashes and all6helper NAND file hashes.22protected
paths and main-app private data remain unreadable to shell; no uninstall/clear
was used. `final-native-apk-audit.json` verifies the actual packaged ARM64 library
matches corrected staging, contains no fixed original-profile storage paths, and
retains all13old native libraries.1472other assets match the published baseline.

**Limits:** Kaizo gameplay was sampled in Gardens with its flat lower map before
the path-only final fix. A physical **Kaizo** 3D-planet stage and each changed boss/
rule were not exercised. Its device-generated Gateway GPL4 file matches the host
mod bake's exact geometry/placements and decoded RGBA textures after PNG encoding
normalization (`thor-planet-equivalence.json`); raw host/device hashes differ.
The post-runtime lower inventory has163files with the same paths as the host
fixture; setup's157file count is recorded separately. No all-stage, sustained FPS,
audible-audio or haptic claim. Kaizo case art is still the text fallback.

Screenshots live in `build/galaxy-kaizo/evidence`; final consolidated evidence is
`physical-final-verification.json`. Only cite images actually viewed. The Thor is
left in the library with no Galaxy game process; the temporary input JAR is removed.

## Narrow design opportunities

- Kaizo-specific cover, spine and disc presentation that clearly identifies
  **Rebalanced2.1**, while preserving the original Galaxy case and family gestures.
- Readability/spacing within the established lower-screen visual language;
  coherent mod/mission labeling using real loaded names and validity bits.
- Review framing of changed planets and level-specific fallback behavior using
  actual mod gameplay captures. Keep orbit/follow, markers and occlusion truthful.
- Polish setup wording (the shared found-disc text currently says “Set up Galaxy”
  even though the Kaizo action correctly says “Set up Rebalanced”).

Do not alter native game rules, live snapshot layouts, profile routing, import
transactions, memory reservations, DSP host allocations, GX address conversion,
16-byte allocator alignment or Vulkan texture-cache teardown as part of visual
polish. Never use unsafe MAP_FIXED fallback, a helper APK, synthetic completion,
shared NAND, vanilla planet caches, or an all-level/FPS compatibility claim.
No public release/upload/push is authorized. Retain creator and component credits.
