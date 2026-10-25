# Rebalanced 2.1 USA patch → native engine

All selected XML groups (`KMGALL`, `KMGUEJ`, `KMGNTSC`) are inspected from the
fixed official ZIP. SHA256:
`b0b43ee39e3b2c2b7a3dcc4bc33d77718cb97da4b95f490146ec8a80ceddd548`.
USA DOL SHA256:
`2c680585a8f58e1cc9c5521b579057f12b124ff0ef409e470a57606c50a93c09`.

The original instruction bytes and branch targets were resolved using the
owner's extracted executable and SMGCommunity/Petari's RMGE01 symbols. Local
disassembly is `build/galaxy-kaizo/patch-disassembly.txt`; research bytes are in
`build/galaxy-mod-research/kaizo-ntsc-memory-patches.json`. No PowerPC code is
executed in the Android port. Every behavior below is gated by the package-private
Kaizo Activity's `galaxy-kaizo` profile before native startup; original Galaxy
uses the old branches and original tables.

| XML address | Verified original function / data | Native adaptation |
|---|---|---|
| 801DD690 | FlipPanelObserver::init, group capacity immediate 32 | 128 only in Kaizo |
| 801DCE48 | FlipPanel::init, same group | 128 only in Kaizo |
| 80153D84 | WaterBazooka::tryShotBullet, third bool argument | true only in Kaizo |
| 80001800 | Stub: first word is float -1500.0; entry is 80001804 | Shared semantic with next site; no executable stub |
| 800382B8 | BossKameckBarrier::init, after initDefaultPos | Add -1500 to position.y before model/collision initialization |
| 8007679C | SkeletalFishBaby::damage, call appearAirBubble | Omit air bubble in Kaizo |
| 8053AF78 | GrandGalaxy2 comet schedule | Import named native rows; proper null terminator |
| 8053B000 | GrandGalaxy3 comet schedule | Same |
| 8053B088 | GrandGalaxy4 comet schedule | Same |
| 8053B150 | GrandGalaxy5 comet schedule | Same; GrandGalaxy1/6 remain unchanged |
| 8053C800 | cGalaxyInfoTable / GalaxyID BCSV | Convert imported MapData.bin into this profile's sys/GalaxyID.bcsv |
| 8053BC94 | 24 GameEventFlag rows, original indices 121–144, 20 bytes each on Wii | Import numbers and resolve original Wii pointers to names using verified source DOL. Apply portable native structs to actual array before sort/index creation. Verify each old index/name before replacing it; native pointer arithmetic and save indexing remain valid. |
| 80054490 | DodoryuStateLv1::catchPlayerAttack | Replace PlayerHitAll predicate with JetTurtleAttack predicate |
| 800564C4 | DodoryuStateLv2::catchPlayerAttack | Same |
| 803F82D8 | MR::requestChangeStageAfterMiss epilogue hook | After requestGalaxyMove, call GameDataFunction::addStarPiece(-999) |
| 80002330 | Death-reset stub and restored epilogue | Same semantic as previous site; no code cave |
| 803F6298 | MR::getPlacedHiddenStarScenarioNo, fixed table call | Bypass vanilla override table and use the existing per-scenario bitfield scan |
| 803F5D40 | MR::isStageSuddenDeathDodoryu | Return false for Kaizo |
| 800D02C0 | BegomanBaby::control, lava-bound predicate | Skip this lava death predicate in Kaizo |
| 800D5DD8 | BegomanSpike::control | Same |
| 800D7D50 | BegomanSpring::control | Same |
| 80275440 | KinopioAstro::init, conditional Luigi supply branch | Select extended letter/reward branch for either character in Kaizo |
| 80275458 | KinopioAstro::init, message argument | 99 in extended branch for Kaizo |
| 802750F0 | KinopioAstro::eventFunc, reward loop bound | 99 in extended branch for Kaizo |
| 802750FC | KinopioAstro::eventFunc, requestOneUp count | 99 in extended branch for Kaizo |

Nine XML folder overlays are applied to the private extracted files before the
normal cooker and lower bake. The XML savegame directive maps to independent
native NAND/settings instead of an SD redirect. The supplied Luigi-unlocked
GameData.bin is deliberately not imported: normal progress belongs to the new
profile. Rebuilding the host-backed FST includes new paths; retaining the old
catalogue would make added models invisible. The native region detector retains
USA for this pinned variant because the patch also adds PAL-language folders.

Native regression uses the actual `kaizo.cpp` and `GameEventFlagTable.cpp` with
assertions enabled, compiled for Android x86_64. It verifies vanilla nonmutation,
all 24 guarded row replacements, four terminated schedules and idempotent
application. Full-game tests are recorded separately; this mapping is not an
all-course compatibility assertion.
