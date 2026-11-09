# Double Dash lower screen: renderer ("duo screen")

Claude's visual side of the lower-screen split. Owner approved the layout in
`lower-mockup.html` on 2026-11-01 (WiiDS chrome + own-disc art). No Codex-owned
file was edited.

## Files (all new, package `dev.thor.mkdd`)

| File | Role | Android? |
|---|---|---|
| `android/.../DoubleDashLowerView.java` | `LowerScreenController.Renderer`; Canvas drawing | yes |
| `android/.../DuoArt.java` | Optional own-disc art loader (`duo_hud.json`) | yes |
| `android/.../DuoScene.java` | Per-state choices: mode kind, focus kart, lap, seat items | no |
| `android/.../DuoLayout.java` | 1240x1080 design grid, uniform scale, map fit | no |
| `android/.../DuoRowMotion.java` | Standings slide by stable kart id (160 ms) | no |
| `android/.../DuoNames.java` | Decomp-ID display text (characters, courses, modes, items) | no |
| `tests/DuoSceneTest.java` | 39 JVM checks for the pure-Java parts | no |

## Wiring

```java
lower = new LowerScreenController(this, MkddNative::readLowerState,
        DoubleDashLowerView::new);   // art from files/ThorHud/mkdd
```

`DoubleDashLowerView(Context, File)` takes another private art folder.

## Behaviour

- **Race (GP/Versus):** header with lap `n / totalLaps`, course, mode and the focus kart's
  time, plus a lap-progress bar. Left: standings rows (rank, **back rider's face first**, as the game's
  own standings and minimap do, then the driver; names; the back rider's item). Right: map panel above
  a duo card (DRIVING / BACK). No speed readout: the feed's km/h ran ~3x the game's speedometer.
- **Time Attack:** the left panel shows lap, lap progress and time. The header shows only the course and mode.
- **Battle:** no laps. The timer is hidden when the kart has no published time.
- **Demo:** the duo card follows the rank-1 kart and is labelled LEADER, with a "DEMO" tag and no gold row.
  A null `playerKart()` (spectating, or local multiplayer until a target is chosen) also shows LEADER.
- **Paused:** the screen dims under "PAUSED". `available=false` shows the title panel, with no stale values.
- **Items:** `itemN` belongs to `characterN` (ItemObjMgr `mEquipItem[kart][driver_index]`;
  swaps move the item). The back-seat rider's item is the usable one. -1 is drawn as a faint dashed well, never a
  guessed item. 16 is drawn as an empty well. Without disc icons, a known kind shows its decomp name. Kinds 1, 6, 9,
  10, 14 and 20 are only inferred, so they read "Item" until confirmed live.
- **Map:** blank "Map unavailable" until `duo_hud.json` lists the course with
  `"alignment":"verified"`. Then the art is drawn as the game draws it, and kart X/Z go through
  `DuoMapAxes` (the texture's own axes) into `MapTransform` (`mirror` false). Only `positionKnown` karts get
  markers, and off-map karts are omitted. Mirror Mode hides the map ("MIRROR MODE") until a mirrored race is checked.

## Art manifest (`<external files>/lower-art/duo_hud.json`)

```json
{"schema":1, "game":"GM4E01",
 "faces": {"7":"duo_face_07.png"},
 "items": {"0":"duo_item_00.png"},
 "item_variants": {"11@12":"duo_item_11_c12.png"},
 "maps":  {"36":{"png":"duo_map_36.png", "axes":["-x","-z"], "bounds":[minU,minV,maxU,maxV], "alignment":"verified"}}}
```

Face keys are ECharID 1..20. Item keys are raw kinds, excluding 16. Map keys are decimal ECourseID.
File names must match `duo_[a-z0-9_]+.png`. Every entry is optional. Anything missing stays as a
neutral disc or an empty well. Nothing is substituted. The art must come from the owner's disc and stay private.

## Staging the art (`design/stage_art.py`)

```powershell
& 'C:/Users/super/AppData/Local/Programs/Python/Python312/python.exe' -I `
  experiments/mkdd-thor/design/stage_art.py --iso '<owner GM4E01 .iso>'
```

The script checks the exact disc SHA-1 and reads the image in place. It refuses to write inside this
repository and writes `M:/WiiDS-mkdd-2026-11-01/lower-art` by default. It produced 77 PNGs there on 2026-11-01:
- 20 portraits (`race2d` `<name>.bti`, 64x64) and 20 map icons (`*_mapicon32`).
- 15 item icons for the kinds with direct evidence.
- Birdo's egg (`11@12`) and Luigi's green fireballs (`21@8`), which `DuoArt.item(kind, holder)` prefers.
- 4 probable icons under `unverified_items`: Bowser's shell, Star, Lightning and Heart. These are ignored until seen live.
- 16 course maps from `<course>_map.bti`. These are IA4 textures of mixed sizes, such as 128x256 on Luigi Circuit.

It also writes `duo_hud.json` and `staging-receipt.json` (PNG hashes). The faces were checked by eye:
Mario 7, Paratroopa 3, Birdo 12 and Petey 20 match ECharID.

**Map placement comes from the game's own table.** `Race2D::getMapPos` is a stub in the decomp, but the
retail US minimap table is public. [RenolY2/mkdd-track-patcher](https://github.com/RenolY2/mkdd-track-patcher)
`src/resources/minimap_locations.json` lists four corner floats per course plus the address of a `li r0, N`
orientation (0-3). `stage_art.py` reads those from the disc's own executable and checks every instruction is
`li r0` with N <= 3. The orientation-to-axes mapping (0 `x,z`; 1 `-z,x`; 2 `-x,-z`; 3 `z,-x`) was derived
independently, by fitting each course's checkpoint line to its map image in all 8 layouts. Every decisive
fit agreed with the table.

Each placement is then checked. Both of the course's own `.bol` lines (checkpoint centres and the CPU racing
line) are projected onto the drawn road, and a map is "verified" when 90% of either line lies within 3 texels.
`checks/fit_<id>_<course>.png` shows each overlay. Result after the 2026-11-03 review:
**all 16 race courses verified** (14 automatic checks and two recorded reviews).
- Mushroom Bridge scores 0.88. It was accepted by a recorded review (`REVIEWED_MAPS`): only the bridge section,
  which the art simplifies, is off the road.
- **Peach Beach reviewed 2026-11-03.** Its white minimap line follows the coastline, while the racing
  route runs across the wide sand. The road-line gate was a false negative. The original retail
  orientation and bounds match the course model; all 241 densified CPU-route points lie in them.
  Keep those coordinates: fitting the route to the white line would shift markers away from their
  terrain. Five checkpoint midpoint samples extend about 2% outside the left bound; they are not
  used as live kart positions. Its textured overview uses the same model baker and unchanged
  coordinates as the other courses.
- Battle courses are not staged (their course IDs aren't mapped to names yet).

`lower-art/preview.html` is a private browser preview of the mockup with this art. Keep it on M:.

The Java builder's `.duo-art-3` marker refreshes existing art manifests to enable Peach Beach.
The detailed overviews remain private PC-built files under `<external>/mkdd/overview`; the Java
overview builder is still an open item. This change enables all sixteen race-course minimaps in
the on-device builder and adds the sixteenth detailed overview to the owner's Thor.

## Lab app (`lab/`)

`lab/build_lab.py` packages a private, debuggable `dev.thor.mkdd.lab` without Gradle. It installs beside
WiiDS and never replaces it. The game is `native/flat_main.cpp` (target `wiids_mkdd_game`): EGL on the
NativeActivity window, upstream's frame loop, and a positional pad (bottom A, left B, right X, top Y, R1 Z,
analog triggers L/R). `MkddGameActivity` adds the duo screen. Files go in
`/sdcard/Android/data/dev.thor.mkdd.lab/files/`: `game.iso`, the memory card, `shaders.bin` and `lower-art/`.

d8 34 (R8 8.2.2-dev) crashes on javac 21's MethodParameters for enum and inner-class constructors. So the
sources avoid enums and inner (non-static) classes. `DuoScene` kinds are ints and `LowerScreenController.Panel`
is a static class.

## Verification so far

- JVM: 46 DuoSceneTest checks pass (now including the map axes), and the existing 46 LowerStateTest checks still pass.
- `javac --release 17` against API 34 `android.jar` compiles every source in `android/`.
- The lab APK builds. On-device results are recorded in MODLOG.md as they happen.

Outputs: `M:/WiiDS-mkdd-2026-11-01/java-tests-design`, `.../android-java-design`.
