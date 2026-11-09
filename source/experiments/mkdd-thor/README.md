# Double Dash lower-screen bring-up

Local work requested 2026-11-01. Codex owns lower-screen function; Claude owns
design. This module is not yet integrated into the WiiDS app.

The linked doldecomp/mkdd is a matching decompilation, used as a reference. The
runtime candidate is the MIT-licensed ethannicholas/mariokartdd-recomp with its
gcn-recomp submodule. Exact pins and rationale are in MODDING_PLAN.md/MODLOG.md.

Implemented here:

- Bounded passive native race reader, coherent guest-frame publication and JNI.
- Eight-kart positions, rank, laps/time, driver/passenger identity and held items.
- Immutable Java model, unknown-state guards and stale-frame expiry.
- Aspect-preserving course-space map projection with honest out-of-bounds handling.
- Android lower Presentation lifecycle and renderer factory, separate from design.
- Exact-DOL field checks, synthetic native/JVM tests and pinned ARM64 build recipe.
- Download watcher with complete-size and exact disc/executable identity checks.

Current validation: ARM64 library + GLES harness built; 44 native host checks,
46 JVM checks, 24 exact-retail field checks and 11 ISO guard tests pass;
API34 Java compilation passes. All 91 paused-source hashes match their baseline.
Native ARM64 tests are compiled but have not been executed on a device.

The partial owner transfer has a complete compatible DOL. That permits compilation
but does not establish a complete disc. No APK, flat game Activity, imported maps,
WiiDS case, gameplay, device acceptance or publication is claimed.

Read CLAUDE_HANDOFF.md for the stable design contract and remaining integration.

## Repeat compile-only work

Use the project's installed Python 3.12 executable (the `python` PATH entry on
this host is a Microsoft Store alias). Keep outputs on M: while C: is nearly full.

```powershell
& 'C:/Users/super/AppData/Local/Programs/Python/Python312/python.exe' `
  experiments/mkdd-thor/prepare.py `
  --dol 'M:/WiiDS-mkdd-2026-11-01/inspection-main.dol' --build
```

Only the exact USA GM4E01 executable is accepted. The private DOL must be extracted
from the owner's image. Nothing in this task downloads a game image or assets.

Check for the complete image without modifying any transfer:

```powershell
& 'C:/Users/super/AppData/Local/Programs/Python/Python312/python.exe' `
  experiments/mkdd-thor/watch_iso.py --seconds 50 `
  --receipt 'M:/WiiDS-mkdd-2026-11-01/download-status.json'
```

No build script installs, starts a game, changes device settings/saves or publishes.
Generated game code, executables, libraries and disc contents stay in the private
M: lab. Public source here is newly authored integration code and layout metadata.
