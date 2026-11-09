# Double Dash as a WiiDS game (case 9)

The optional 2026-11-03 private Double Dash Plus mode is documented in [PLUS.md](PLUS.md).

Owner approved adding the case on 2026-11-02. Base tree: Codex's
`M:/WiiDS-issues-9-10-2026-10-31/android-latest` (WiiDS 1.1.2 + issues 9/10 + merged Brawl). That is the
exact source of the installed `1.1.2-issues9-10-preview` / 1112. The base is copied and never edited.
Working tree: `M:/WiiDS-mkdd-2026-11-01/wiids/android`.

The 2026-11-03 widescreen HUD also needs its texture-fingerprint header before building the native
target. This reads the owner's verified disc and writes metadata beside the private translated code;
no texels or game instructions are copied into this repository:

```powershell
python -I experiments/mkdd-thor/native/hud_profile.py --iso '<verified GM4E01 ISO>' `
  --output M:/WiiDS-mkdd-2026-11-01/generated/wiids_hud_profile.h
```

Keep `native/runtime-widescreen.patch` applied to the pinned runtime. The new `WidescreenHud` pass
only adjusts renderer-owned projection states: single-screen race HUD groups move to the 16:9 edges,
icon sizes stay unchanged, and wholly offscreen 2D animation panes remain hidden. 4:3 is unchanged.
The lower-screen renderer is unchanged. This is a private preview, not publication approval.

MKDD Java must compile with `-source 8 -target 8` before d8. The verified local build init script is
`M:/WiiDS-mkdd-2026-11-01/audit-2026-11-03/preview.gradle`: it recompiles the 21 MKDD Java sources at
the end of `compileReleaseJavaWithJavac`, retaining the existing settings for other ports. The final
31 MKDD class files were checked as JVM major 52.

```powershell
python -I experiments/mkdd-thor/wiids/apply_mkdd_case.py <android dir> M:/WiiDS-mkdd-2026-11-01/libwiids_mkdd_game.stripped.so
# then, in <android dir> (JAVA_HOME = JDK 21, ANDROID_HOME = PORTING/.tools/android-sdk):
gradlew.bat assembleRelease -PthorBuild=true -PwiidsApp=true -PthorRetroRewind=true `
  -PwiidsVersionName=1.1.2-mkdd-preview -PwiidsVersionCode=1112 -I <output.gradle> --offline --console=plain
```

`apply_mkdd_case.py` makes 46 anchored edits in 16 files (each anchor must match exactly once, and line
endings are kept). It copies `android/` + `wiids/` Java into `dev/thor/mkdd` and adds
`jniLibs/arm64-v8a/libwiids_mkdd_game.so`.

| Piece | Where |
|---|---|
| Disc | `GameCatalog.Disc.MKDD_USA` (GM4E01 rev 0, GameCube), common Your games import; `MkddStorage.validate` accepts only the exact 1,459,978,240-byte image, SHA-1 34bdef70... |
| Files | `<external>/mkdd/`: `game/game.iso` (install dir, replaced whole on import), `saves/memcard_a.raw`, `shaders.bin`, `lower-art/` |
| Art | `WiiDsArt` delegates to `MkddArt.build` (Java port of `design/stage_art.py`, from the imported disc) |
| Launch | `ThorGameLaunch.launchMkdd` -> `MkddLaunchActivity` (`:mkdd`) -> `WiiDsMkddGameActivity` (`:mkdd_game`, NativeActivity `wiids_mkdd_game`). Back opens the WiiDS menu; Return to WiiDS exits normally to the library. |
| Saves | Save manager profile `mkdd/saves` (backup/restore; no Dolphin import). It is also protected from offload. |
| Offload | Manage games removes `mkdd/game` and `mkdd/lower-art` only |
| Carousel | Case 9, its own stack, GameCube black case and 8 cm disc. Art keys `mkdd-cover`, `mkdd-disc`, `mkdd-spine` |

## Case art (owner's personal launcher set, not in the APK)

Downloaded with the owner's OK on 2026-11-02 from GameTDB: `coverfullHQ/US/GM4E01.png` (1024x680,
SHA-256 5344b10d...) and `disc/US/GM4E01.png` (160x160, b6696782...; no discHQ exists).
`wiids/prepare_case_art.py` cuts exact crops with no resampling, as experiments/launcher-spines does:
spine x487-539 (back/spine fold edge at x487, front band at x539) and front x539-1024. The disc is used as
downloaded. Added as `mkdd-cover`, `mkdd-spine` and `mkdd-disc` to
`<external>/custom/launcher/artwork.json` (30 entries). The previous manifest is backed up at
`M:/WiiDS-mkdd-2026-11-01/case-art/backup-20261102/artwork.json` (SHA eaff44a1...).
