# Galaxy lower screen: the observatory field guide

## Purpose

The AYN Thor's lower panel answers the question a Galaxy player keeps asking: *what is left to
find, and where am I in it?* It adds what the upper HUD does not show:

- **In a galaxy:** its star chart. One node per Power Star the galaxy holds, in mission order,
  gold where this save has earned it, the mission being played ringed, comet, purple-coin, hidden
  and green stars marked as the game marks them. Beside it is a ledger of the missions with the game's
  own names for those already earned or being played, and "Not yet found" for the rest. On the
  mission select it shows the same chart before a mission is chosen.
- **In the Comet Observatory:** the save's galaxy ledger. Every open galaxy with stars owned out
  of total, unfinished galaxies first, finished ones after, as the game's own Power Star list counts
  them. This is the practical "which galaxy should I go back to" view.
- Live resources the current context needs: Star Bits this run against the banked total (the
  game's two Star Bit icons), coins, purple coins only in a 100-purple-coin mission, lives.

A flat X/Z "map" was rejected for Galaxy. Its planets are spherical with local gravity, so a
top-down dot would mislead. The chart is a ledger in mission order, and its decoration (the star-chart lines) makes no claim about stage layout.

## Visual direction

A celestial field guide from the game itself. Everything drawn comes from the owner's own copy of
the game, extracted privately into `files/ThorHud` and never packaged:

- **Backdrop: the game's live space.** It uses the method from the Sunshine companion. While the title is up, the renderer
  copies the game's 3D picture at the start of its 2D layer, so the title logo and "Press Start"
  are excluded. It takes the centre region in the lower panel's own shape (1240:1080) at 620x540 and
  scales it 2x. The sky is the title's comet-orbit sky actor (`CometNearOrbitSky`: its space panorama,
  night planet, sun and comet are tinted by material colours, so only the live picture is faithful).
  When the title gives way to the file screens (their 3D planets and character heads are not
  mirrored), and in loading and galaxies, capture stops and the last title picture stays, as in the
  Sunshine companion. Over it, the game's own `icon-imkira` twinkle sprites drift.
- **Text in the game's fonts:** `menufont64` for headings, `messagefont26` for body text (tinted,
  with a drop shadow), `numberfont` for counters, and `picturefont` icons (Power Star, empty star,
  comet star, Grand Star, Star Bits run and banked, coin, purple coin, Mario / Luigi, crown).
- The title, file select and loading screens show only the space (no logo: the logo belongs to
  the upper screen). Loading adds "Travelling..." and the credits add "Thank you for playing".

### Live space capture (`platform/src/gx/vk/vk_thor_capture.cpp`)

- The game marks only the title's frames. `FileSelector.cpp` flags each update in which its `WaitBind`
  or `Title` nerve runs. `ThorLowerState.cpp` takes that flag once a frame, and `GameScene::draw`
  then records `PORT_GX_MARK_THOR_SPACE` just before `PORT_GX_MARK_HUD_BEGIN`. The marker travels
  with the recorded frame, so the camera cut into the file screens is never captured.
- The title sky actor (`FileSelectSky`) pitches from 0 to 135 degrees and back over a
  3000-frame cosine. The held picture is therefore the horizon if A+B is pressed early, and open space
  after about a minute on the title, matching the upper screen at that moment.
- The render thread captures at that marker. It ends the EFB render pass,
  blits the crop into a 620x540 image, copies that into one of two persistently mapped host
  buffers, and resumes with the load pass, exactly like the game's own EFB copies. The result is
  published only after the renderer's existing wait for the previous submission's fence, so nothing
  ever waits for the capture. A double-buffered result with a claim flag (sequentially consistent)
  means the UI never sees a torn picture and the renderer never blocks on the UI.
- At most one capture every 30 ms, and none unless the lower screen polled in the last 1.5 s.
- A sky-only mode for galaxies was tried and removed. A sweep of all 231 stage archives found
  that the sky is drawn after the planets everywhere except the two stages that place
  `SphereAir`, so a clean sky picture is not available without an extra replay pass.

## State contract

Sampled on the game thread once per frame after `update()/calcAnim()`
(`GameSystem::frameLoop` -> `thorLowerPublish`, `decomp/src/Game/System/ThorLowerState.cpp`),
committed under a seqlock (`platform/mobile/thor/ThorLowerBridge.cpp`), and copied by the UI at 15 Hz
through `GalaxyActivity.nativeLowerState` (registered with `RegisterNatives`; the game image is
`RTLD_LOCAL` at 0x98000000). Layout `platform/include/port/thor_lower.h` (version 1, 6340 bytes,
little-endian, offsets asserted at compile time). There's no heap allocation, no game pointers leave the
game thread, and nothing is written to game state.

| Field | Source | Units / meaning | Valid when | Rate |
|---|---|---|---|---|
| phase | scene controller: `isChangingScene`, `ScenarioSelectScene::isScenarioSelecting`, scene name `Logo`/`Game`/`Intermission`/`PlayTimer`, stage `FileSelect`, `isSceneInitializeState(End)`, GameScene nerve (`thorGameScenePhase`), `MR::isDemoActive` | `ThorLowerPhase` | always | per frame |
| generation | changes with stage, scenario, or entering/leaving loading | counter | always | per frame |
| stage | `mCurrSceneControlInfo.mStage` | internal name, **diagnostics only** | galaxy phases | per frame |
| galaxyName | `MR::getGalaxyNameOnCurrentLanguage(stage)` | UTF-16, tags stripped | `V_STAGE` | per frame |
| scenarioNo / selectedScenarioNo | `getCurrentScenarioNo` / `getCurrentSelectedScenarioNo` | 1-based | `V_SCENARIO` | per frame |
| scenarioName | `MR::getScenarioNameOnCurrentLanguage(stage, mission)` | UTF-16 | `V_SCENARIO` (not in the Observatory) | per frame |
| galaxyStars / galaxyStarsOwned | `GalaxyStatusAccessor::getPowerStarNum` / `getPowerStarNumOwned` | count | `V_STARS` (galaxy has 1..24 stars) | 2 Hz, and at once on change |
| starFlags[i] | `hasPowerStar`, `isValidNormalComet`, `isHiddenStar`, `isValidCoin100`, `GameDataConst::isGrandStar`, `MR::isPowerStarGreen`, selected mission | bits | `V_STARS` | 2 Hz |
| starNames[i] | `getScenarioNameOnCurrentLanguage` only for owned or selected missions | UTF-16 | `V_STARS` | 2 Hz |
| totalStars | `MR::getPowerStarNum` (`calcCurrentPowerStarNum`) | save's Power Stars | `V_COUNTERS` | per frame |
| starBitsRun | `MR::getStarPieceNum`: this run (`ScenePlayingResult`); in the Observatory it reads the bank | count | `V_COUNTERS` | per frame |
| starBitsBanked | `MR::getStockedStarPieceNum`, added at the stage result | count | `V_COUNTERS` | per frame |
| coins | `MR::getCoinNum` (this run) | count | `V_COUNTERS` (`ScenePlayingResult` exists) | per frame |
| purpleCoins | `MR::getPurpleCoinNum` | count | `V_PURPLE`: selected mission `isValidCoin100` | per frame |
| lives | `MR::getPlayerLeft` | count | galaxy phases | per frame |
| ledger (Observatory) | `MR::makeBeginScenarioDataIter`, open galaxies with stars (`isOnGameEventFlagGalaxyOpen`), `getPowerStarNumOwned/getPowerStarNum/isCompleted`, `getGalaxyNameShortOnCurrentLanguage` | per galaxy | `V_LEDGER` (`isStageAstroLocation`) | 2 Hz |
| flags | `isStageAstroLocation`, `isPlayerLuigi`, `port_is_infinite_starbits`, `isScenarioDecided` | bits | galaxy phases | per frame |

Stale state: the UI drops its picture when the game stops publishing for 2 s, when the activity
pauses (the presentation is dismissed and polling stops), and whenever the phase leaves the galaxy
phases. Title, file select, loading and credits show only the space; pause shows the chart under
a "Paused" ribbon.

## Coverage and limits

See `build/galaxy-thor/lower-screen/` for receipts and paired captures. Verified so far is the
logo -> file select -> Peach's Castle Gardens prologue -> Star Festival gameplay slice.

The 3D planet view (round galaxies, during play): baked by `Bake-LowerPlanets.py` (GPL3, the game's
own textured models), drawn by `GalaxyPlanetRenderer` (OpenGL ES 3, its own thread, a transparent
TextureView between the canvas view and the `Over` layer). The game's frame rate is unaffected:
on the title with the planet redrawn every vsync, the game logged no frame below 58 fps.
