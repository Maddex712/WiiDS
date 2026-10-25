# Galaxy Thor journal

## 2026-10-21: initial research and source setup

Owner requested a Super Mario Galaxy port for the connected Thor. Owner then
confirmed that no Galaxy disc dump is available yet and asked for the format.
Recommended a clean original Wii ISO; RVZ/WBFS are upstream alternatives.

Cloned OpenPort-Labs/Galaxy-Android to `reference/galaxy-android`, pinned
`1abcb975927ecb4189949bd51f81e73a04d97566`. Compared its native C++/Vulkan
route with Nebula's documented Windows/Direct3D 12 runtime and Petari's reference
decompilation. Selected Galaxy Android for the first standalone slice.

ADB initially needed access outside the terminal sandbox to start its daemon.
Initial device list was empty; later read-only detection found wireless endpoint
`adb-6ab0800a-vKtfDD._adb-tls-connect._tcp`, AYN Thor/kalama/arm64-v8a.
Foreground was WiiDS ModePickerActivity and the OEM external-display shell.
No install, app launch or device inputs were sent.

Native CMake configuration started with the existing NDK29 and CMake/Ninja.
Upstream automatically started compiling its missing bundled shaderc library.
Build status must be recorded separately; source setup is not a working port.
The configure process is terminal session `73014`; it is compiling the NDK's
shaderc dependency, not the game yet. No native-build success receipt exists.
Upstream include-case preflight passed for all 4,044 build-relevant files.
The new PowerShell build wrapper passed syntax parsing.

Code audit found gamepad mappings inconsistent with upstream control docs,
global save/data mirror paths, and a required debuggable low-address preloader.
These are explicit adaptation tasks before a Thor install.

Next: finish native build prerequisites, record compile results; await owner's
disc dump path before conversion or any claim of native boot. Preserve beta5
and existing device sessions/data.

## 2026-10-21: candidate additions requested while Galaxy data is pending

Read-only research; no candidate was cloned, installed or selected for work.
Queries: native Android Wind Waker/Twilight Princess ports; HarbourMasters
Ghostship Android; PaperBoat Android; Kirby Return to Dream Land/Donkey Kong
Country Returns native Android; Super Mario Galaxy 2 native Android.

Primary-source findings:
- Super Mario 64: https://github.com/HarbourMasters/Ghostship/releases
  release 3.0.0 documents Android support and on-device extraction.
- Ocarina of Time: https://github.com/Waterdish/Shipwright-Android
  documents Android builds and game-data extraction.
- Majora's Mask: https://github.com/Waterdish/2ship2harkinian-Android
  documents Android builds, tested Android15 and ROM extraction.
- Paper Mario64: https://github.com/HarbourMasters/PaperBoat/blob/develop/docs/android-ios-web.md
  documents ARM64 Android build, app-scoped data and on-device Torch extraction.
- Wind Waker GameCube: https://github.com/zwaetschge/bluewake-android
  android-port branch documents ARM64/Vulkan and limited Fold7/Outset testing;
  requires owner GZLE01 rev0, locally generated game module. Early candidate.
- Twilight Princess: https://github.com/TwilitRealm/dusklight/blob/main/platforms/android/README.md
  documents ARM64 Android shell and CMake/Gradle builds.

Integration difficulty is an engineering estimate based on existing Android
support, not a measured schedule or a Thor compatibility result. Existing
Android support does not include WiiDS launcher integration or custom lower
HUDs. Proposed lower-screen map/counter/inventory views are future design ideas.
No ready native Android DKCR/Kirby RTDL/Galaxy2 port was established by the
bounded searches; do not infer that no such project exists. No screenshots or
new runtime tests were taken for this recommendation research.

## 2026-10-21: owner narrows candidate scope to Wii and GameCube

Owner explicitly wants Wii/GameCube only; exclude the earlier N64 candidates.
Additional bounded searches: GameCube native Android Luigi's Mansion; Metroid
Prime Android native; melee-pc Android; Animal Crossing Android native port.
Primary findings:
- Animal Crossing: https://github.com/zwaetschge/ACGC-Android-Port documents
  ARM64 Android, controller support, private saves and testing on AYN Odin3.
- Melee: https://github.com/999sian/melee-pc/releases supplies Android ARM64
  builds; source documents controller support. No Thor performance claim.
- Metroid Prime: https://github.com/Odrannnn/MetroidPrimePort documents native
  Android builds, GameCube disc inputs and controller presets. Distinguish it
  from AxioDL/metaforce, whose README says builds are currently unavailable.
Wind Waker and Twilight Princess remain candidates from the prior research.
All candidate difficulty and proposed lower-display uses are estimates/ideas;
no code build, device test or installation was performed for this shortlist.

## 2026-10-21: Galaxy 1 local build and integration

Owner supplied Galaxy 1 and 2 ISOs and explicitly requested Galaxy 1 for WiiDS.
Only Galaxy 1 was processed. USA RMGE01 revision 0, 4,699,979,776 bytes:
SHA-1 `a36105c9fbfff6041dc5babf4318c6a216ad470a`, SHA-256
`b47683440931bcf1a604c20fbad22b918977e20db4965df108050342f4857b8e`.
Full DTK disc verification passed. Fixed pinned cooker import/spawn regressions
(cook/archive circular import and missing converter helpers). Converted 2,371
disc files into 2,375 output files, retaining movies; structural validator
checked 2,229 archives/24,800 members with zero problems. Every device data
file SHA-256 matched before atomic readiness commit. Raw wireless tar stream
was interrupted; sync-pushed repair archive plus complete hash verification
recovered staging without accepting partial data.

NDK29 ARM64/API32 native build completed. Windows shaderc MRI quoting repaired
by combining its nine already-built static archives with llvm-ar. Native
libraries packaged into an isolated Galaxy helper, without any disc files.
All global data/save mirrors now point into the helper's own external profile;
private NAND is independent of existing games. Actual non-debuggable probe
confirmed ART mappings in the required address window. Keep the debuggable
wrap/preloader strategy only in this explicit development runtime. Unsafe
MAP_FIXED reservation fallback removed.

WiiDS version906 non-debuggable local build adds seventh case/provider readiness
and explicit helper launch, retaining old indices and all 13 native libraries
and 1,452 bundled game assets byte-identical to beta5. Owner-signed APK installed
through PackageManager after reconstructing and SHA-verifying it from an exact
old-APK ZIP delta, avoiding repeated failed 1.8GB wireless transfers. Existing
18 saves/settings match the before snapshot. Galaxy cover/disc/spine added from
GameTDB with provenance; all prior 18 artwork hashes/metadata retained.

Gamepad mapping corrected: B/RT Wii B, LT Z, L C (0x4000, not Minus), X/Y/R1
spin; A jump/select. Pointer velocity now integrated per frame after the UI
update, preserving valid physical-pointer state despite touch clearing it.
Native Back intercept returns to WiiDS and ends only the helper game process.
46 actual-source ARM64 mocked-input assertions passed. Actual case/title/A+B,
right-stick pointing, normal empty-slot creation and fresh-process reload passed.

Starting PeachCastleGardenGalaxy crashed. Signal registers initially implicated
DisplayListMaker for Kinopio; LLDB stopped a later reproduction in DSP mailbox
deque::push_back. The corrupt values were the exact DSP reply words
0xDCD10004/0xF355FF00. The persistent deque grew on a game thread using the
current temporary scene heap, leaving stale storage after FileSelect teardown.
Added PortHostAllocScope around mailbox growth. Actual-source ARM64 regression
routes 320,000 messages: baseline 313 wrong-heap allocations (fails), patched
zero wrong-heap/313 host allocations (passes). Patched device now initializes
PeachCastleGardenGalaxy successfully and renders opening story pages.

Final bounded check: opening story/Peach letter and first controllable Star
Festival area rendered; physical left-stick movement, visible airborne A jump,
Start pause/resume and Back to WiiDS passed. Final game PID4511 log has no
native fatal; prior diagnostic reproductions remain separate evidence. Both
installed APK hashes match local metadata, readiness=1, 18 prior save/settings
files unchanged and all 21 artwork files match, including 18 retained images.
Normal zero-star starter NAND backed up privately. Device left in WiiDS with
Galaxy selected and no Galaxy game process. Final receipt device-verification.json.

Lower screen is a control guide, not a live HUD/map. No audio/haptic/long-run/
stage-sweep or sustained-FPS claims. Upstream gear/SAVE overlay and Android
gesture indicator remain visible. No publication, push or external sharing.

## 2026-10-21: owner requests Claude bottom-screen design handoff

Prepared CLAUDE_BOTTOM_SCREEN_HANDOFF.md and Get-ClaudeHandoff.ps1. The brief
asks Claude to match the Sunshine companion's polish with a Galaxy-specific
design, real state, and a substantial mission/progress/navigation or accurately
mapped pointer function beyond duplicated HUD counters. Includes exact current
runtime ownership, state API starting points, special native loader/JNI and
heap/lifecycle constraints, existing save/art preservation, actual screenshots,
build commands and bounded on-device acceptance criteria. Opened the brief in
the editor; 29 referenced local paths verified and PowerShell loader parsed and
loaded successfully. No Claude message sent, clipboard changed, game code
edited, build installed or device input performed in this handoff-preparation turn.

## 2026-10-21: lower screen live space, title glare fix, left-stick menu pointer

Claude session (owner-directed).

- **Lower screen.** It now shows the game's own title space, captured live through Vulkan without
  waiting (`vk_thor_capture.cpp`; centre 1240:1080 crop at 620x540; at most one capture every
  30 ms; none unless the lower screen polled in the last 1.5 s). Text uses the game's fonts.
  The logo was removed from the lower screen at the owner's request. On the save-file screens the
  lower screen holds the last title picture, frame-bound via `PORT_GX_MARK_THOR_SPACE`. A
  sky-only mode for galaxies was removed: the sky is drawn after the planets in 229 of 231 stages.
  The JNI natives now register one at a time, and the picture handoff is sequentially consistent.
- **Title glare.** The sun glow over the horizon showed blocky, stair-stepped cut-outs. Root cause:
  `vk_shadergen_vs.cpp` forced every sky vertex to one constant depth (`clipPos.w * 0.99999`)
  even on a flat screen. The `CometNearOrbitSky` layers (planet, horizon band, sun glow) then
  tied in the D32F depth test, and rounding decided each tie. Sky depth is now flattened only for
  `stereoDraw`. On the Thor, the title draws the glow cleanly and also shows the planet's
  clouds below the horizon (evidence `build/galaxy-thor/lower-screen/evidence/glare-before-after.png`).
  Galaxies' skies have not yet been rechecked on device after this change.
- **Left-stick menu pointer** (owner request). `MR::isStarPointerModeMenuForPort` covers pointer
  modes HomeButton..PictureBook plus the dome's SphereSelector modes. It is published each frame
  through `port_input_set_menu_pointer`. In those menus a gamepad's left stick moves the pointer
  (same speed as the right-stick pointer, `starStickSensitivity`) and is not forwarded as the
  Nunchuk stick; gameplay is unchanged. Seen working in the owner's own session (file 1 chosen
  with the cursor). Stick-paged menus (pause star list, storybook, Mii select) page with the
  d-pad or on-screen arrows while the stick steers.
- **Patch export.** `Export-Source.py` now adds untracked new files to `galaxy-android.patch`
  without touching the index. Re-exported: 48 files, forward/reverse checks PASS.

Follow-up the same evening, after a design review of the cursor:

- **Menu check.** `isStarPointerModeMenuForPort` is now an allow-list: FileSelect, ScenarioSelectScene,
  PauseMenu, ChooseYesNo, PictureBook, DemoWithHandPointerFinger (story and staff-roll saves) and the dome
  selector modes. SaveLoad counts as a menu only when the mode underneath is a menu (or Logo/Title, or
  nothing): the port's quick save during play keeps the stick on Mario. -1, HOME, error, Logo and
  Title are excluded (the pointer is hidden there).
- **Hand-back latch.** A stick still held when a menu closes is withheld from the game until it is
  let go, so Mario does not lurch. With the right stick on the camera (rightStickMode 0), the pointer
  hides as soon as play resumes (TouchInputHandler::releasePointer). In menus the cursor carries on
  from the shared pointer position.
- **Verified on the Thor** with a guard that stops if anyone else presses a button:
  - file select: the cursor moves, and file 1 and Play This File are chosen with the left stick
  - prologue gameplay: the left stick still walks Mario
  - pause: Paused on the lower screen, the cursor moves
  - latch: the scene changed 1.2% with the stick held after pause closed, against 25.9% in a control walk
  - Back to the WiiDS library, and relaunch
  - sleep and wake
  - title performance with the live capture: 30.0-30.6 fps, renderer CPU 0.8-1.5 ms; before the
    lower-screen work: 30.6 fps, 1.1 ms
  - depth format 130 (D32F_S8)

  Receipt: `build/galaxy-thor/lower-screen/verification-2026-10-21-cursor-glare.json`. Patch
  re-exported (50 files, PASS).

Not done: Observatory ledger and galaxy chart (file 1 has 0 Power Stars); galaxy skies beyond the
prologue after the depth fix; mission select, galaxy map, dome selector and yes/no with the cursor.
No publication, push or commit.

## 2026-10-21: lower screen goes black with the upper screen and through cutscenes

Owner request, following the Sunshine and NSMB companions.

- **Brightness probe** (`vk_thor_capture.cpp` `thorProbeRecord`). At the end of each draw pass
  while the lower screen polls, the finished picture (2D and wipes included) is blitted to 64x36,
  copied to a mapped buffer and read after the existing fence wait, so nothing waits. The bright
  level is the luma the brightest 0.5% of samples reach. It is delivered as `nativeSpace`
  mode[1] (`port_thor_top_level`).
- **Java veil** (`GalaxyLowerScreen.veil`). Bright level 6 and below is black, 36 and above is
  clear. The bottom also stays black through every cutscene phase (opening fly-in, mission card,
  event demos, Power Star get, staff roll) and the prologue's picture book (`THOR_FLAG_STORYBOOK`,
  set by `ProloguePictureBook::control`). It darkens with a time constant of 0.08 s and returns
  over 0.15 s, or 0.9 s for 6 s after a cutscene.
- **Verified on the Thor:** starting file 1, the top's fade to black takes the bottom with it.
  Through the storybook, Peach's letter and bubble, and Mario's run to the castle, the bottom stays
  at mean 0 while the top shows. At gameplay it returns (mean 60). Evidence:
  `evidence/cutscene-veil.png`, `evidence/veil/`. Patch re-exported (PASS).

## 2026-10-21: HUD split like Sunshine (owner request)

- **Upper screen:** while the lower screen is up (`port_thor_lower_visible`, it polled within 1.5 s),
  `CoinCounter`, `StarPieceCounter` and `PurpleCoinCounter` skip drawing (TARGET_MOBILE `draw()`
  overrides). The Power Star count, life meter and lives stay. If the lower screen goes away, the
  counters come back on top.
- **Lower screen:** the lives counter is removed. Coins, Star Bits (run and banked) and purple coins
  stay. While paused, the lower screen shows only Paused, because the pause menu above shows the counters.
- **Verified on the Thor** in Castle Gardens gameplay: `evidence/hud-split.png`. Patch re-exported (PASS).

## 2026-10-21: Galaxy settings menu on Back (owner request)

A Galaxy-flavoured twin of the Mario Kart Thor Settings.
- `GalaxySettingsMenu.java` and `GalaxySettingsDialog.java`: a focused full-screen Dialog over the
  game, drawn at 1920x1080 with Galaxy colours. Rows are white/cyan pill buttons; the focused row is
  gold with the game's Power Star icon. Titles use menufont64, rows use messagefont26, and the A
  icon is the game's. The two new icons are staged by Extract-LowerArt.
- Pages: Display (resolution 480p-1440p, 60/30 FPS, smoothing, Show FPS); Controls (right stick
  camera/pointer, pointer speed, stick response, invert camera); Game (language, skip movies,
  infinite lives/health/Star Bits); Resume; Return to WiiDS.
- Opening: the Thor's Back key (`MobileApp.cpp`, `GalaxyActivity.dispatchKeyEvent`) and the top-left
  gear (`UIOverlay.cpp`) both open it. Without the helper's menu, Back still returns to WiiDS.
- `platform/mobile/thor/ThorSettingsBridge.cpp`: JNI nativeMenuState/Set/Values. Requests are
  queued and applied on the native main thread. Opening freezes the game and audio
  (`port_set_paused`) and publishes a neutral pad; closing saves and resumes; APP_CMD_RESUME no
  longer unpauses while the menu is open. Two new persisted settings, `post_process` and
  `camera_inverted`, are applied at load; `haptics` is now loaded too.
- **Verified on the Thor:** Back opens it and the game pauses. The Display page shows current
  values. Show FPS was changed and reverted (applied and saved). B goes back and closes, and the
  game resumes. The gear opens it. The lower screen shows 'Settings / The game is paused'. Return to
  WiiDS opens the library. Patch re-exported (PASS).

## 2026-10-22: flat map on the lower screen (Castle Gardens)

The owner chose the flat map first.
- **Baking:** `Bake-LowerMaps.py`, with readers in `lowermap/` (smgarc, kcl, raster; written during
  the map research). It bakes each flat stage's collision floor from the owner's extracted data, top
  down, Sunshine-style: surface colours from the collision sound codes, height tint, slope shading,
  a dark rim and pale glow, and a transparent background. It writes the image plus a
  world-to-pixel frame to `build/galaxy-thor/lower-art/maps`, which is staged privately to
  files/ThorHud/maps and never packaged. So far: PeachCastleGardenGalaxy (3,195 floor triangles).
- **Snapshot version 2:** `marioPos` and `marioUp` (MR::getPlayerPos / getPlayerUpVec) are appended,
  with `THOR_VALID_MARIO`. The earlier offsets are unchanged.
- **Lower screen:** on a stage that has a map, the map replaces the star chart, and Mario's
  pictograph marker eases after his live position with a pulse ring.
- **Verified on the Thor:** the Gardens map shows with Mario at the start plaza, and the marker
  follows him up the road (`evidence/flat-map.png`, `flat-map-walk.png`). Patch re-exported (PASS).
- **Next:** more flat stages, which need gravity checks one by one, then the 3D planet prototype.

## 2026-10-22: the game ran at half speed (30 fps): VI retrace clock fixed

**Owner report:** the frame rate was slow. The port had been at about 30 fps since bring-up, with the
game's work only 2-4 ms, the renderer around 1 ms and no GPU wait.

**Diagnosis:** new counters in platform/src/vi/vi.cpp showed exactly 30 retraces raised per second.
viClock's fallback clock (59.94 Hz) skipped a tick whenever any retrace had been raised in the last
1.05 periods, including its own. A period is shorter than 1.05 periods, so it skipped every other
tick. The display's raise (ScenePresenter after each new frame) landed within the 11 ms refusal
window and was refused (about 300 of 300 per 10 s).

Galaxy advances one frame per retrace (MainLoopFramework mRetraceCount 1), so the whole game ran at
half speed.

**Fix:** the clock now stands down only when the display itself raised a retrace within the period
(sDisplayRaisedAt). Measured on the Thor: 59.9-60.0 retraces/s; no perf line below 58 fps on the
title or in 20 s of Castle Gardens gameplay; the prologue now plays at its real speed. The
diagnostic is kept, logged only below 58/s.

## 2026-10-22: 3D planet view (first version)

- **Baking:** `Bake-LowerPlanets.py` (lowermap/bdl.py from the research) places every planet of every
  galaxy's zones (layers common + layera) in world space. Each planet uses the game's Low model,
  else its display model if 12000 triangles or fewer, else its collision coloured by surface.
  Output is files/ThorHud/planets/<Galaxy>.planets (37 galaxies, 28 MB, private).
- **`GalaxyPlanets.java`:** picks the nearest planet to Mario (with hysteresis) and draws an
  orthographic orbit view with Canvas.drawVertices: back-face culled, depth sorted, lit. It
  follows Mario from above; drag turns it, pinch zooms, double-tap recentres, and it returns to
  auto 3 s after the last touch.
- The lower screen uses it during play on stages without a flat map. The lower window is now
  touchable (still NOT_FOCUSABLE).
- **Verified on the Thor:** the Gardens world rendered as a 3D diorama with Mario's marker (flat
  map temporarily hidden for the test, then restored).
- **Not verified on device:** a round planet (Gateway or later, which needs story progress) and the
  touch controls.

**Update (2026-10-22): the planet view is verified on a round planet.** The game was played to Gateway
Galaxy with a closed-loop navigator: `mario x y z` log lines twice a second during play
(GalaxyLowerScreen), and A* over the baked map's floor. The lower screen showed Gateway's starting
planet with Mario's marker in place (`evidence/gw-dual.png`). Touch was tested with adb swipes on
display 4: a drag turns the planet, the marker fades on the far side, and the view returns to Mario
after 4 s. The game kept input focus (`evidence/gw-touch.png`).

## 2026-10-22: 3D worlds for every playable galaxy (owner: add for all stages)

- **Baking:** `Bake-LowerPlanets.py` now writes GPL2. It covers every layer, and any placed object or
  map part with collision at least 300 units across, not just the game's planet table (invisible
  walls excluded). Each piece carries its zone and layer, plus every mission's per-zone layer masks
  from the scenario data. Result: all 44 playable galaxies, including the Comet Observatory and the
  map-part stages that had nothing before (BeltConveyerEx, BreakDownPlanet, CocoonEx, FlipPanelEx,
  KoopaBattleVs2, Surfing Lv1/Lv2). 117 MB, private and staged.
- **`GalaxyPlanets`:** only pieces active in the current mission are shown. The piece Mario is on is
  drawn with its active neighbours as one diorama, nearest first, within a 36k-triangle budget.
- **Evidence:** offline previews of six stages in `evidence/worlds-preview.png`.

## 2026-10-22: GPU planets with the game's textures (owner: smooth them, more game-like, keep 60 fps)

- **Baking (GPL3):** `lowermap/j3dmesh.py` reads each model's own normals, first texture coordinates
  (with the material's static texture matrix), vertex colours, joint transforms and per-material
  draw state: alpha test, blend, two-sided. `lowermap/gxtex.py` decodes the GX texture formats with
  numpy and matches the old decoder on 361 textures. `Bake-LowerPlanets.py` writes welded, indexed
  meshes plus each galaxy's textures (deduplicated, at most 256 px, PNG). Display models of up to
  60k triangles are kept, and `DimensionAir` gravity shells are left out.
  Size: 44 galaxies, 207 MB, private, staged to files/ThorHud/planets.
- **Approximations:** one texture per material, from the first TEV stage that samples one.
  Environment-mapped textures are folded to a flat colour. Textured materials whose vertex shade
  tops out under 140 are lifted at most x2. A few planets that rely on TEV registers stay dark
  (BigRelayPlanetA, CocoTwinB).
- **`GalaxyPlanetRenderer`:** OpenGL ES 3 on its own thread (nice 2) into a transparent
  TextureView. It uses a depth buffer, 4x MSAA, mipmaps and 4x anisotropic filtering.
  - Lighting: smooth per-pixel. Key light, sky/ground ambient, soft specular, a blue rim, mild
    aerial depth, and a highlight shoulder.
  - Cost: it draws only when the view changes, and VBOs and textures upload once per galaxy.
- **`GalaxyLowerScreen.layered()`:** three layers. The canvas view (space, chart, maps), the GPU
  planet above it, and `Over` on top (marker, counters, veil, settings note), so cutscenes still
  black out the planet. Touches pass through to the canvas view as before.
- **`GalaxyPlanets`:** loads GPL3 memory-mapped. Its camera is now perspective (30°, zoom narrows the
  lens). The triangle budget per diorama is 250k.
- **Debug:** `files/ThorHud/planet-preview` containing a galaxy name shows that galaxy's largest world
  turning while not playing. Read at launch; removed after testing.
- **Verified on the Thor, at the title, with the preview turning continuously (a new frame every
  vsync, the worst case):**
  - Renderer: 60 frames/s at 0.45-1.0 ms CPU per frame.
  - Game: no `perf: game` line (logged only below 58 fps) and no `vi:` line (below 58 retraces/s)
    over about 2.5 minutes.
  - Evidence: `evidence/gpu-preview-lower.png`, `evidence/gpu-preview-honey-lower.png`, and
    renders of all 443 pieces with the same shader in `evidence/planets-gpu/`.
- **Not yet verified on device:** the in-play follow camera and Mario marker, which need gameplay.

## 2026-10-22 (night): live world state, scenery, glass fix, Quick Save in the menu

Owner: "verify everything is 100% accurate" (a cutscene-only stone ring was drawn on Gateway's
first planet; the black hole was missing) and "make sure these models are dynamic".

- **Live world state (game side):** `decomp/src/Game/System/ThorWorldState.cpp`, `port/thor_world.h`.
  - Registration: `PlacementInfoOrdered::initPlacement` notes every placed LiveActor by zone and l_id.
    A flower or bush group's members (`PlantGroup::mMembers`) are noted as children 1..N.
  - Teardown: `GameSystemSceneController::destroyScene` clears the table under a spinlock before the
    scene is deleted, since that runs on another thread.
  - Publish: every frame after `thorLowerPublish` (`GameSystem.cpp`), each actor's alive and hidden
    flags, model base matrix and scale go out. It is read-only. The bridge is
    `nativeWorldState` (seqlocked, in `ThorLowerBridge.cpp`).
- **Bake GPL4 (`Bake-LowerPlanets.py`):**
  - Meshes are stored once in model space, and each placed object is a mesh plus its placement.
    Pieces carry zone names, l_id, gates (SW_APPEAR, cutscene group, placed at run time) and child.
  - Every object is classified by the game's own `NameObjFactory` table, by its class and folder.
    Ground is planets and solid parts. Scenery is MapObj/Map/Ride models (flower fields, trees, black
    holes, launch stars, cages, Observatory minis).
  - Left out: skies, gravity shells, kill cubes, mirror copies, effects, pickups and critters.
    Enemies, NPCs and the player are also out; they need animation.
  - The per-object audit is `build/galaxy-thor/lower-art/planets/audit.json`. Size: 44 galaxies, 163 MB.
- **App:**
  - `GalaxyPlanets` applies the live state each frame. It matches pieces by zone name, l_id and child;
    hides dead or hidden ones; and draws each from its live matrix. Without live data, pieces stand at
    their placements and gated ones stay hidden. Scenery larger than 2.5x the world is not part of a diorama.
  - `GalaxyPlanetRenderer` has one VBO per model, a per-object model matrix, and is capped at 60 Hz
    (the lower panel runs at 120).
- **Glass and env maps (top screen):** `platform/src/gx/vk_shadergen_vs.cpp`.
  - Texgens now take the vertex's raw object-space position, normal, binormal and tangent, as GX
    does and Dolphin does. They previously took the transformed values. The game's texture matrices
    already include the model-view, so env maps and projection maps (CapsuleCage glass, crystal and
    metal sheens, the `Vanish` alpha test) were applied twice, giving the ragged white smear.
  - A/B at Gateway's start shows identical results for UV-mapped materials.
- **Top-screen dock removed:** `kThorHidesDock` in `TouchInputHandler.h`; it is neither drawn nor touchable.
  Back opens the Galaxy menu. **Quick Save** is a new root row (ThorSetting 13); it closes the menu,
  then `triggerSaveAnywhere` runs once the game runs again.
- **Verified on the Thor:**
  - The live state matched 105 of 109 Gateway pieces.
  - The game had no frame below 58 fps, in play and at the title with the planet redrawn continuously.
  - The renderer runs at about 63 frames/s and 0.5 ms CPU per frame.
  - The menu row shows; the dock is gone.
  - Evidence: `evidence/live-title-upper.png`, `menu-quicksave.png`, `ab-old-upper.png`, `planets-live/`.
- **Not seen on device:** the lower panel itself during play. The AYN dual-screen assistant panel
  stayed over it all night, and it is the owner's system app, so I left it alone. The CapsuleCage
  glass also hasn't been seen after the fix, because it is a level away.

## 2026-10-22 (night, continued): button remapping, WiiDS integration, releases removed

Requests from the owner: "add a real key bind changing settings for controllers in each game that
doesn't already offer it", "make sure Mario Galaxy gets added to these" (the Options collage and
the import page), "delete the other releases off the WiiDS repo", and a Codex handoff
(`CODEX_HANDOFF.md`).

- **Galaxy:** Controls > Buttons sets what each of the Thor's 12 buttons does: Jump, Shoot Star
  Bits, Spin, Crouch, Camera, Pause, Centre the pointer, or nothing. The triggers are included.
  `GamepadInput` is now table-driven; the layout is saved as `button_map_0..11`; the defaults equal
  the old layout.
  - Verified on the Thor: change A, Reset, then reopen the menu. The native layout really changed,
    then really reset.
  - A display race after Reset (the menu read values before the native thread applied them) is
    fixed: the menu now shows the standard layout at once.
- **NSMB / MSMBWii:** a Controller tab in the game's own settings window, which until now offered
  only keyboard Keybinds. On the Thor, "F10 > Controls" doesn't exist. Built and packaged; not seen
  on the device (see CODEX_HANDOFF.md).
- **Mario Kart / Retro Rewind:** already offer controller remapping (Back > Controller); unchanged.
- **Sunshine / Eclipse:** already had `ControllerConfigActivity`, but nothing on the Thor reached
  it. WiiDS Options > Controller buttons now opens it; verified on the Thor.
- **WiiDS:** Galaxy is in the Options collage (seventh cover, two rows) and on the import page
  (RMGE01 rev 0, ready state from the add-on). It is also in the new Controller buttons chooser.
- **GitHub:** all four WiiDS releases (v0.9.0-beta.2..5) deleted at the owner's request. Tags are
  kept; metadata is backed up in `build/release-deletion-2026-10-22/`.
- **Installed:** helper `debf24be…35cf7`, WiiDS `55a2716b…69af`. The Thor was left on the WiiDS
  library.

## 2026-10-22 (morning): device checks done, Thor labels, on-device import

- **AYN dual-screen assistant:** it is a BOOT_PROGRESS-layer overlay on display 4 (above every
  app). `KEY_F24` on `/dev/input/event0` (gpio-keys, the Thor's dual-screen key) hides it; a
  force-stop just respawns it.
- **Verified on the Thor:**
  - The GPU planet on the lower screen in play: Gateway, Luigi's marker, the flower field, the
    standing stones as on the top screen. Live counters too.
  - The WiiDS Options collage with all seven covers.
  - NSMB's Controller tab: a real rebind round-trip (Run to Y, then back to X).
- **Thor button names:** in the Thor's Nintendo layout (`temp_abxy_layout_mode=1`) each printed
  letter sends the Android key of that letter, so A jumps and X runs (the owner's report). NSMB's
  Controller tab now shows the printed names (A, B, X, Y, L1, R1, L2, R2, L3, R3, Start, Select)
  instead of the shared table's Xbox position words.
  - Mario Kart's controller page still uses those words. It is a prebuilt library
    (libWiiCompiled, 20 Oct) and needs its own native rebuild.
- **On-device import:**
  - `Build-Importer.py` builds upstream's installer bridge (`libinstaller.so`) with its Python 3.10
    and nodtool, and bundles OUR cooker scripts with three changes: threads instead of processes,
    native Yaz0, and the crater-collision rebuild that upstream turns off on Android.
  - `GalaxyImportActivity` (process `:import`) asks for All files access once, checks free space
    (it leaves out the movies if space is short), extracts, checks the disc is RMGE01 rev 0, cooks
    into `GalaxyData.pending`, then swaps only `files/` and `sys/` (saves kept) and writes `.ready`.
  - WiiDS's Galaxy card: "Set up Galaxy" or "Import again (keeps your saves)" with the found or
    chosen disc's path.
  - Verified on the Thor (test mode, `verify_only`): ISO to cooked data in 105 s. All 2,375 files
    byte-identical to the PC-cooked data (`data-manifest.sha256`). The handoff from WiiDS and the
    access screen were verified via the UI.
  - Test files and the ISO were deleted afterwards and the add-on's file access reset to default.
    The installed game stayed ready.

## 2026-10-22 — local Kaizo Rebalanced 2.1 engine slice completed

All25active USA patch sites have native semantics, with imported24event rows,
4comet schedules, GalaxyID table and private modified assets. One WiiDS APK adds
case7 in the Galaxy family; game/import processes and both storage roots are
separate. Exact creator ZIP plus verified owner USA rev0 ISO passed physical
setup (2368cooker files without movies;157reported lower files/44planet sets).
ARM64+x86 builds,67transaction assertions, actual native flags and actual native
NAND/profile mirror regressions passed. Native/frontend patches forward/reverse
checked against pinned sources without changing indexes.

The first physical candidate exposed an inherited hardcoded external save mirror.
Its task-created file was preserved locally and renamed intact on device; it did
not replace an existing owner/helper save. New profile roots now route mirror/
restore/quick saves, exchange, textures, shader manifests/caches and crash logs.
The final packaged ARM64 library was checked for corrected staged bytes and no
fixed original-profile storage paths. Final APK owner-signed/nondebuggable/code906:
`13ad3816d77b2b27ade61066241713d112bf49df175e328cd8b0e3111ded02b9`.

Before the path-only final fix, Kaizo Gardens movement, airborne jump, live flat-map
marker, pause/resume, Home/recents resume and clean Return were observed. Final
PID5826 committed a normal new zero-star Yoshi-icon save to the Kaizo mirror while
original mirror stayed absent; freshPID6993 reloaded it. Original profile copy
verified2545backupfiles and preserved helper NAND/settings. FinalPID7545 loaded
the old Luigi file (Mario icon/0stars/1bankedbit/oldtimestamp), entered Gateway and
rendered the GPU planet:195liveobjects,105/109piecesmatched; movement sampled.
All3finalprocesses exited cleanly. Kaizo mirror hash remained unchanged afterwards.

All50readable old save/settings/art and6helper NAND hashes are unchanged, as are
all13previous installed native libraries and1472other assets versus release
baseline.22protected paths/main nondbg internal remain inaccessible to shell; no
uninstall/clear. Source provenance of the retained historical Sunshine binary is
not newly established. No physical Kaizo 3D-planet/per-boss/all-stage/FPS/audio/
haptic claim; its case uses text fallback. The device Gateway mod GPL4 matches
host geometry/placements/decodedtextures after PNG encoding normalization.

Thor left library/no games, temporary input JAR removed. No push/upload/publication;
published1.0 untouched. Detailed Claude handoff: `kaizo/CLAUDE_HANDOFF.md`.
Receipt: `build/galaxy-kaizo/physical-final-verification.json`, supporting native,
preservation, source and screenshot records alongside it.

## 2026-10-22: Kaizo presentation polish (Claude)

- **Kaizo case art.** Built from Galaxy's retail art with the mod's own title logo (credited
  to TheDes7royer) by `kaizo/Compose-KaizoArt.py`. Staged by `kaizo/Stage-KaizoArt.py`; the
  21 existing images were unchanged.
- **Setup wording.** The setup text now names each card's own action (`kaizo/Polish-Frontend.py`).
- **APK.** Rebuilt as code 906 (`8cecb458…`); only the dex and baseline profile changed. Installed in place.
- **Checked on the Thor.** The collage, the fanned stack (cover and spine) and the open case
  (cover and disc).
- **Kaizo planet framing: still unverified.** Injected-input play reached the Gardens
  invasion, but not the first galaxy.
- **Clean-up.** Thor left on the library; the temporary input JAR was removed.
  `Export-Source.py` passed. Nothing was pushed or published.
