# MKDD Plus private mode

Owner requested Double Dash Plus on 2026-11-03, then clarified that it needs its own case behind
Double Dash, matching the other mod stacks. The private 1.2 candidate implements portable
on-device patch installation and course-map generation. The fresh setup flow passed on the Thor.
No publication is authorized. Standard Double Dash and its card are retained.

Supported input: **MKDD Plus 3.3 NTSC**, patched against the owner's complete GM4E01 revision 0 disc.
The patch was linked by the [RetroAchievements project topic](https://retroachievements.org/forums/topic/27182).
Its SHA-256 is `e78cfc35f54b5b277ac0c7358435c9c2c82a2a0a8ecbdca6d0bdb47fbd8ba158`
and its size is 789,136,548 bytes. The source ISO must be the unchanged 1,459,978,240-byte USA
GM4E01 revision 0 disc, SHA-1 `34bdef709a1ae9799fdabe8e54cff1c7d3665f2b`.
The resulting DOL SHA-1 must be `3cd4cbfa6c9a7d4b95dd66028f14f26d23f6f304`; the ISO is
1,074,593,792 bytes with SHA-256
`0bc58c2ceff549e1a7eb7d6efac37fbabc4fc3649a804282efc703e67e6b505d`.
Other versions require separate translation and acceptance.

The MIT [digdat0/mkdd-nx](https://github.com/digdat0/mkdd-nx) project, pinned at
`3e9afcd9af4d001b4e5aa1c5deab824f98502b17`, supplied useful reference material.
WiiDS uses its existing gcn-recomp runtime, not that project's engine or compiled output.

## On-device setup

1. In **Options > Your games**, import the supported original Double Dash disc first.
2. Open the **Mario Kart: Double Dash!! Plus** setup card and select **Choose Plus 3.3 patch**.
   Before the original disc is ready, this button reads **Double Dash required**. The adjacent
   **Plus patch and instructions** button opens the creator project topic.
3. Choose the original 3.3 NTSC xdelta in Android's file picker. WiiDS verifies the source disc,
   patch and patched ISO, then builds all 80 course maps and detailed overviews locally. The UI
   reports progress and offers **Cancel**. The setup space gate requires the result size plus
   256 MiB free, roughly 1.35 GB, in addition to the already stored source/patch files.
4. After successful setup, launch Plus from its own case behind Double Dash. Its separate card
   is retained or created by that profile; setup never copies or replaces the standard card.

`MkddPlusMod` stages `game.iso` and `lower-art` together under `mkdd/plus/game.importing`.
Detailed pictures and their index live inside `lower-art/overview`. Only after all hashes and map
generation succeed does `DiscInstallCommit` publish the complete directory as `mkdd/plus/game`.
Failed/cancelled staging retains existing playable data. An interrupted rename can recover the
previous complete disc directory. Saves/settings/backups are outside this transaction.
Setup, save operations, offload, art repair, other installs and game launches use the reviewed
admission gates; running games must close before setup starts.

## Preparing a private build

Keep discs, generated C, art, fingerprint metadata, native libraries, APKs and saves outside the repo.
`prepare_plus.py` checks inputs and refuses repository output. The base header's adjacent JSON
receipt is required. Python dependencies match the existing standard art/overview builders.

```powershell
python -I experiments/mkdd-thor/wiids/prepare_plus.py `
  --disc '<owner GM4E01 ISO>' --patch '<MKDDPlus.xdelta>' --xdelta '<xdelta3.exe>' `
  --output '<private Plus folder>' --runtime '<pinned mariokartdd-recomp runtime>' `
  --base-art '<private standard lower-art>' --base-header '<private generated/wiids_hud_profile.h>' `
  --translate --bake
```

Keep the accepted flat-renderer widescreen patch applied to gcn-recomp
`b2193d7c7ce65c40959cf81859beaacafeebfd99`. Configure a separate native build with
`MKDD_PLUS_BUILD=ON`, `MKDD_RUNTIME` pointing to that runtime, and `MKDD_GENERATED` pointing to
the private Plus generated directory. Build `wiids_mkdd_game` and strip debug information.
Apply the optional overlay to an already integrated private WiiDS tree:

```powershell
python -I experiments/mkdd-thor/wiids/apply_plus.py '<Android tree>' '<private stripped Plus .so>'
python -I experiments/mkdd-thor/wiids/apply_release_setup.py '<Android tree>'
```

The overlay validates anchors and adds `libwiids_mkdd_plus_game.so` and its private Activity.
It copies current MKDD Java, without replacing the standard native library or content assets.
Compile MKDD Java with `-source 8 -target 8` before d8. The release setup overlay adds the portable
patch installer and Java course/overview baker. PC preparation remains a developer path for native
translation and reference output comparisons; users can prepare the supported disc/maps on device.
The original/patched disc and generated game art are not bundled in the APK.

## Profiles and switching

| Mode | External root | Preference prefix | Library |
|---|---|---|---|
| Double Dash | `mkdd/` | `mkdd_` | `wiids_mkdd_game` |
| MKDD Plus | `mkdd/plus/` | `mkdd_plus_` | `wiids_mkdd_plus_game` |

The current paths are relative to the app's external-files root:

| Profile | Disc | Generated detailed art |
|---|---|---|
| Original | `mkdd/game/game.iso` | `mkdd/lower-art/overview/` |
| Plus, portable setup | `mkdd/plus/game/game.iso` | `mkdd/plus/game/lower-art/overview/` |

Each profile also keeps its own `saves`, `save-backups` and `shaders.bin` beside its `game` directory.
Plus prefers committed `game/lower-art`; its legacy `lower-art` directory remains a fallback.
Within either art folder, `DuoArt` prefers nested `overview/`, then the legacy sibling `overview/`.
Existing PC-prepared `mkdd/plus/lower-art` and `mkdd/plus/overview` therefore remain supported.
Never copy the standard card into Plus. Plus inherits the display/control settings once, and then
keeps its own settings. Its in-game Saves page backs up and restores its own card. The global save
manager has separate `mkdd` and `mkdd-plus` profiles, each with the existing single `main` archive root;
the original profile's format is unchanged. Archive filename lookup chooses the longest stable game ID,
so Plus backups never enter the original game's list or export path.

Double Dash's shelf stack contains **two cases**. A opens the stack; Right selects Plus, then A opens
its case. Both have their own GameCube mini disc and cover/spine/disc artwork. Plus's private art uses
the existing owner-approved retail scans with only the actual mod-provided Plus lettering added.
`prepare_plus_case_art.py` reproduces those three private PNGs without generated artwork or APK assets.

The original case always sends stable game ID `mkdd`; the Plus case sends `mkdd-plus`. There is no
in-game mode selector. The launcher checks the explicit identity and verifies the pinned Plus executable.
Original runs in `:mkdd_game`, Plus in `:mkdd_plus_game`, and each loads only its own library. Restart and
restore return to that Activity's own identity. Frontend shortcuts/resume, companion labels and mini-disc
sizing use the distinct identity. The earlier AtomicFile mode preference is ignored and is not deleted.
Each case offloads only its own disposable game/art/overview paths; both cards and backup folders are
protected. Plus setup has its own patch picker and never redirects into the retail disc importer.

## Course-set support and limits

Plus reuses 16 race slot IDs across five sets. The lower native feed samples its actual set byte;
map keys are `set * 256 + slot`. All 80 race variants use their mod-provided name and minimap images.
Detailed previews are baked from each actual model. Bounds and orientations come from the pinned
native page-switch tables, including reversed corner pairs; they are never fitted to guessed routes.
Mirror maps remain disabled.

The mod rewrites 22 orientation instructions when the set changes. Translation patches load their
actual instruction halfwords instead of baking initial values into C. Added code receives translated
entry points, retaining the runtime's full CPU register state. HUD fingerprints include the mod's
replacement images and course maps for the accepted shape-preserving 16:9 projection handling.

`tests/plus_projection_test.py` extracts actual generated functions into a private host harness.
It tests both switch directions, wraparound, 74 path bytes, 88 projection values, 22 instruction
bytes and course-route UV projections across all 80 variants. Generated C never enters the repo.
These checks establish coordinate compatibility, not all-course gameplay or race completion.

The actual Kotlin installer passed **10 host checks**, including the full pinned patch plus 80-map
pipeline, busy refusal, cancellation, bad input and original/card preservation. The installed APK's
actual Android baker completed all **80 Plus courses** under a 256 MiB heap in 70.129 seconds.
After the raw BTI pixel correction, all 16 original plus 80 Plus overviews were compared with desktop
references: worst mean channel difference 0.005066/255, 99th percentile 0. Owner ISO hashes were unchanged.
Evidence is retained privately under `M:/WiiDS-release-wrap-2026-11-08/installer-host2.log` and
`device-art/{plus-fixed.log,plus-fixed-comparison.json,standard-fixed-comparison.json}`.

Fresh physical picker-to-install setup produced the expected Plus ISO and 350 map and overview
files, all matching the independent Android baker. The test restored the original identical disc
file and kept the validated nested artwork. Both cards and the tracked settings remained
unchanged. Battle, split-screen, mirror, long-play and every-course completion need separate
gameplay acceptance.
