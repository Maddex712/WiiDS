# Double Dash for WiiDS

## 2026-11-03 — Plus case behind Double Dash

Owner clarified the expected UX: another case behind Double Dash, like existing mod stacks. Added
case10 with stable identity mkdd-plus and grouped9/10; library count11. Explicit case IDs replace the
in-game mode selector and old AtomicFile choice. Plus moves to its own :mkdd_plus_game process;
original stays :mkdd_game. Restart/restore keep their own identities. Added matching companion,
frontend resume/shortcuts, mini-disc sizing, own global save profile and independent offload plan.

Asset-pipeline target: existing private PNG cover485x680/spine52x680/disc160x160, upright cover,
clockwise spine lettering, round disc clipped/hole-punched by the existing renderer, alpha PNG.
Kept each base scan unchanged except compositing the actual Plus word from the mod's256x102 logo;
no fabricated lettering/images. Source hashes, crop/placement/scaling are in private provenance.json.
Three new private PNGs; original30 artwork records/images retained exact,33 records now. No APK assets.

Read-only review caught backup-prefix collision (mkdd vs mkdd-plus) and wrong retail setup routing.
Verified both and fixed: longest-ID archive lookup used for list/export, Plus setup shows private PC
requirement instead of original importer. Actual frontend identities/archive/resume36 checks pass.
Overlay reapply leaves38 staged paths byte-exact. Android release build passed,33 major52 MKDD classes.
Owner-signed/nondebug1112 APK df3b0c42d094b2f8d627ba7fa023d93f3f500d656a769c24c356a75596cceab6 installed exact;
all35 native libraries and642 content assets unchanged from previous accepted private preview.

Live Thor: two cases visible in stack, Plus case and companion art on both screens, correct original
andPlus disc boot identities/processes, Plus same-PID Home/library resume and normal Returns. All65
actually readable original+Plus save/settings files unchanged/22 historical inaccessible exclusions;
91 paused source hashes exact. Initial backups retained privately before automatic rotation. No race,
progress change, save export/restore or offload. Final closed Double Dash stack/original front/Plus behind,
both native game processes absent. Evidence plus-case-2026-11-03/verification.json. No commit/push/publication/Discord.

## 2026-11-03 — MKDD Plus private WiiDS mode

Owner requested starting Double Dash Plus. Applied pinned 3.3 NTSC xdelta to a private owner-disc copy;
never changed the original. Added code translates with the existing full-register gcn-recomp runtime.
The mod rewrites22 map-orientation instructions, whose translations now load their actual runtime halfwords.
Separate native library/profile preserves standard Double Dash. Back > Game stages a mode change; Restart
now commits it and waits for process exit. A device test exposed SharedPreferences caching across processes;
the selector now uses an application-private AtomicFile read freshly by the launcher. A restore resets
staged selection to the running profile. All other WiiDS menu pages remain available.

The lower feed includes the actual course-set byte. Each of80 race variants gets its mod name/minimap art
and a model-baked overview in exact native-table axes/bounds. Reflected corner pairs retain their direction.
Expanded course-name display to fit the mod's two-line art legibly. Added private preparation/overlay tools,
reproduction instructions in `wiids/PLUS.md`, and an actual generated-code projection harness. All assets,
translated C, libraries, patched disc and save artifacts remain outside this repository on M:.

Checks: native switch/projection181575 /29792 route samples across80 variants; scene53/lower53/native44/HUD57
pass. Durable preparation replay produced94 exact generated files. Source8/target8 Android build33 major52
classes passed. Owner-signed/nondebug APK SHA256
`6aaaef108f740b9832caa779f5ebb4197cc20d53a8a5f8e60704e5e48750f0f7` installed exact; old34 native libraries
and642 content assets unchanged. Read-only reviewer and AtomicFile follow-up found no important issue.

Thor: both-direction mode switches with new PIDs, X/Y course-set changes, SNES Mario Circuit1 TT start/short
drive, matching detailed map/moving marker, live4:3/16:9 HUD, menus/manual Plus backup, two-screen pause hold,
same-PID Home/library resume and global Options verified. Lower Android UI720 frames/12.183s=59.10 updates/s,
recent119 frame-completion p95=3.724ms/no new janky frames; not guest FPS. All64 readable original files and91
paused source hashes exact;22 historical inaccessible exclusions. Initial standard card/five backups retained
privately before normal backup rotation. Only Plus's separate fresh card was created; no race/lap/record finished.
Final library/MKDD case/Plus next-launch mode, game PID absent. No publication/commit/push/Discord.
Fresh-device installer, Java overview baking, all-course live acceptance, live Plus restore, GP items/finish,
mirror, battle, split and long-play remain. Canonical private evidence: `plus-2026-11-03/verification.json`.

## 2026-11-03 — Peach Beach preview, all sixteen race-course maps

Owner requested the last missing course preview. Found a false negative in the road-line check:
Peach's retail map follows the coastline, while the race route crosses the broad sand. Confirmed the
native orientation-2 projection agrees with the existing axes/bounds and course-model bake. All 241
densified CPU-route samples fit the bounds and visible panel. Kept the retail coordinates unchanged.

Added a documented model-review exception in `stage_art.py`, enabled all sixteen COURSES in the Java
builder and bumped its marker to `.duo-art-3` so old manifests refresh. Rebuilt private art (all 127
existing PC PNGs unchanged), baked Peach with the existing script, and merged just its PNG/manifest
entry onto the Thor. The previous fifteen overview records and PNG hashes remain exact.

Owner-signed/nondebug private APK `1.1.2-mkdd-preview` / 1112, SHA-256
`ac2fb438fca4e635b89be003adf30c60192515d02531b782106c6c8cb5e65d6b`, installed hash exact.
Only classes.dex/baseline.prof changed from the widescreen preview; all 34 native libraries and 642
content assets remain exact. Build succeeds, 31 MKDD classes major 52. All-16 map check fails before
and passes after; existing 57 HUD + 44 native + 53 scene + 46 Java checks pass. Read-only review passed.

Device acceptance: old missing-map screen reproduced; new preview and moving P1 marker checked at
the start, first bend and beach in Peach Beach Time Trials. Art-marker migration observed. No lap,
race or record completed. Returned normally to library, MKDD selected, no game process left. All 64
readable external save/settings files unchanged (22 inaccessible excluded); 91 paused hashes exact.
Private evidence/rollback/APK: `M:/WiiDS-mkdd-2026-11-01/peach-2026-11-03/verification.json`.
No publication/commit/push. Detailed-overview Java portability, mirror/battle/split/live all-course
acceptance and the original opening movie remain separate; Peach placement is resolved.

## 2026-11-03 — full WiiDS audit and approved 16:9 race HUD

Owner approved moving the upper race HUD to the wide-screen edges and testing a private preview.
Installed owner-signed `1.1.2-mkdd-preview` / 1112 SHA-256
`9eef10815e9d858b589e41d1bb2ab16a01679074b57ff492f58e44669f872e74`, exact device readback.
Private APK, rollback snapshots, screenshots and receipts are in
`M:/WiiDS-mkdd-2026-11-01/audit-2026-11-03/`.

`WidescreenHud` recognizes owner-disc HUD textures using a privately generated fingerprint header.
It clones renderer states to move left/right HUD groups outward while preserving icon shape and
centred prompts. It also suppresses wholly offscreen 2D panes exposed by the existing wide projection.
4:3 and the existing runtime patch are unchanged. Added 57 actual GX-batch regression checks,
including shared-state isolation, live aspect switching, copy exclusions and clipping boundaries.
The initial legacy fixture fails; final implementation passes. Read-only review found no blocker.

WiiDS global settings and MKDD Controls/Display/Saves remained intact. Tested live aspect switching,
normal library return, GP and Time Trials on Luigi Circuit, lower standings/map/held mushrooms, and
menu pause on both screens (identical images two seconds apart). No race/lap/record completed.
The lower renderer was unchanged; fresh Android UI sample gave 59.13 updates/s over 12.109 seconds,
p95 completion 6.71 ms and no increase in janky frames. This does not measure guest FPS/all courses.
44 native lower-state + 53 DuoScene + 46 Java lower checks also pass; 31 MKDD classes are Java 8.

All 33 other natives and 642 content assets match preview-11. Final 64 readable external save/settings
files unchanged (22 inaccessible excluded), including the MKDD card; 91 paused-source hashes exact.
Original five backups retained privately before automatic startup rotation; five remain on device.
Final Thor at WiiDS library, Double Dash selected, game process absent; legacy add-ons retained.
Upper GP roulette/item pickup, race finish, all-course and split/battle acceptance remain untested.
No publish/commit/push; release, overview-portability and existing movie/Peach Beach items remain.

Started 2026-11-01. Owner requests Double Dash integration; Codex owns lower-screen
functionality and Claude owns visual design. Local work only.

## Inputs and route

- Reference: https://github.com/doldecomp/mkdd at
  `b56cc7677fa23058eff8f38784479c3b4b854060`, CC0, private checkout
  `M:/WiiDS-mkdd-2026-11-01/mkdd`.
- Native runtime candidate: https://github.com/ethannicholas/mariokartdd-recomp
  (pin recorded in build receipts), MIT; gcn-recomp submodule
  `b2193d7c7ce65c40959cf81859beaacafeebfd99`. Has ARM64/GLES/AAudio support,
  but its app frontend requires a headset. Thor needs a flat Android frontend.
- Choose static recompilation using the runtime candidate. The linked matching
  decomp supplies reference structures; it is not itself a complete Android port.
- Owner Downloads partial file `Unconfirmed 20772.crdownload` has GM4E01/rev0
  header. Its complete executable was read without modifying the transfer and
  matches upstream required SHA-1 `e389e50357d4a5076ab3d38672204658f411123d`.
  This proves executable compatibility, not a complete/verified disc.
- C: nearly full; every large checkout, generated file, dependency and build goes
  to `M:/WiiDS-mkdd-2026-11-01`. No owner files removed to make space.

## Scope and preservation

- Standalone lab before registering a playable case in the shared launcher.
- Lower screen: coherent passive race snapshots, eight kart positions/standings,
  two-character identity, laps/time, both riders' held items where verified,
  map coordinates, explicit unavailable state and stale-frame expiry.
- Claude owns colors, typography, artwork, layout and final lower renderer.
- Do not edit paused achievement sources or existing game natives/saves.
- No device mutation, installation, gameplay, save operation or publication yet.
- Existing issues 9/10 device acceptance remains separate and incomplete.

## Evidence

Upstream README initially suggests debug-only decomp support; checked configure.py
also includes Release_eu. Neither is the USA retail runtime; runtime metadata and
the actual supplied executable establish the separate USA profile.

## First compiled milestone

- Runtime pin `bd36eb982ea58c6651584cc48264876d1aba9546`; source DOL translated
  into 13,434 functions. Existing one upstream non-code branch warning retained;
  no instruction checks bypassed or speculative runtime fix made.
- `prepare.py --dol ... --build` passes end to end. ARM64 GLES harness and
  `libwiids_mkdd.so` link, with the JNI export and guest-frame hook confirmed.
- Implemented native passive reader, both rider slots/held items with ownership
  and roulette guards, immutable Java model/feed, course-space map projection,
  lower-display lifecycle and renderer factory. No renderer design chosen.
- PASS: 44 host native checks, 46 JVM model/map/feed checks, 24 exact retail
  field-displacement checks, 11 synthetic ISO guards, Android API34 Java compilation.
  All 91 paused-source hashes match their recorded baseline. ARM64 native tests
  compiled, not device-executed. These are not game-play evidence.
- Retail constructor/results disproved initial inferred Course/checker offsets;
  corrected +44/+48 used. Pointer fields remain bounded and live acceptance open.
- `compile-verification.json`, `field-verification.json`, `native-build.log`,
  `prepare.log` and `download-status.json` in M: hold local receipts.
- No device access in this task. Updated issues9/10 checkpoint is incorporated:
  that task's cleanup/restoration is complete; reporter-scene acceptance remains
  unverified. Do not redo its cleanup from older checkpoints.

Next: complete ISO validation, own-disc course maps, flat Thor game Activity and
WiiDS import/launch/save-profile wiring, Claude renderer, real-game acceptance.
The current shared library has no flat Activity startup API; the EGL executable
is a test harness. No APK or runnable WiiDS case has been built.

Download remained 1,296,826,080 bytes, `.crdownload`, last modified 22:02:04 local,
through the bounded 22:09-22:10 monitor. Canonical full ISO is 1,459,978,240 bytes.
C: remains nearly full. User was asked to free space or finish onto M:. No owner
files removed/moved. Continue checking Downloads; never boot the incomplete image.

## 2026-11-02 (Claude): maps, flat Thor frontend, first boot

- Map placement from the retail minimap table (RenolY2/mkdd-track-patcher addresses), read from the
  disc's own DOL and checked against each course's .bol lines: 15/16 verified, Peach Beach not
  (design/README.md, design/stage_art.py, DuoMapAxes).
- `native/flat_main.cpp` + CMake target `wiids_mkdd_game`; `android/.../MkddGameActivity.java`;
  `lab/build_lab.py` -> private `dev.thor.mkdd.lab` (debug key), installed beside WiiDS.
- d8 34 workaround: no enums or inner classes in the Java (DuoScene kinds are ints;
  LowerScreenController.Panel is static; LowerState.Kart / MapTransform.Point ctors package-private).
- THOR 2026-11-02 20:47: boots from files/game.iso (owner's verified GM4E01). GL ES 3.2 Adreno 740,
  window 1920x1080, AAudio 48 kHz playing, opening movie, then the title screen drawn 4:3 pillarboxed and
  "Now creating data" (fresh memory card in the lab folder). Lower display shows the duo screen's
  title panel. Not yet checked: controls, a race, the race lower screen, maps on device, performance.
- THOR 2026-11-02 21:00-21:20, controls + Grand Prix race (lab/drive.sh drives the real "Odin
  Controller"; lab/peek.sh reads live guest RAM via run-as): menus, character select, d-pad, A/B/Z,
  Luigi Circuit race all work. Upper frame is in sync with game state (simultaneous device-side
  captures: timers 7 ms apart). Lower screen verified against the game's own HUD: course, lap 1/3,
  lap progress, race time, all 8 pairs and ranks, map markers where the game's minimap has them.
- FIXED item manager: 0x803cbec0 -> 0x803cbf40 (r13-21728, as retail getKartEquipItem callers load
  it; the evidence's "-5560" was read as hex). Live: stock 16 = empty, equipped objects, shuffle
  managers. Now shows Banana, Fake Item Box, triple Green Shells, Mushroom; roulette = unknown.
  Host native tests updated (44 pass).
- Z swap: driverSlot follows live (duo card flips). The game's standings and minimap feature the
  BACK rider, so rows and map markers now lead with the back rider too.
- Speed: the feed's x2.16 "km/h" read ~3x the game's speedometer (36 vs 12 mph, 20 vs 6 mph,
  simultaneous). The speedometer code is not decompiled, so the readout was removed rather than
  shown in a unit that disagrees with the game.

## 2026-11-02 (Claude): WiiDS case 9 installed on the owner's Thor

- Base: Codex's android-latest (installed 1.1.2-issues9-10-preview, SHA 7f7ff324..., kept as rollback
  M:/WiiDS-issues-9-10-2026-10-31/WiiDS-1.1.2-issues9-10-preview.apk). wiids/apply_mkdd_case.py (46
  anchored edits, 16 files) + Gradle assembleRelease. Installed 1.1.2-mkdd-preview/1112,
  SHA 404708aeeff56958faba58e07d8730db1a6056f4da6e77b499945a6ac9aa2447, same WiiDS certificate.
  Versus the original: only libwiids_mkdd_game.so added; manifest, classes.dex, 2 baseline profiles changed;
  all 33 other libraries and 642 other assets byte-identical.
- Review (subagent, read-only): no critical issues. Fixed: background pause (frame-hook gate in
  flat_main.cpp; Home mid-race paused at ~20 s and resumed there, same PID), key/axis trigger merge,
  deadzone rescale, input cleared on focus loss, Back ends the process (quick relaunch: fresh PID, no
  re-entry), configChanges as Brawl, art repair skips ready art, import hash cancellable with clearer
  messages, library package from getPackageName, art reload retry.
- THOR: Your games found /sdcard/Download/Mario Kart Double Dash (USA).iso and imported it; on-device
  MkddArt output equals stage_art.py (manifest identical; 73 PNGs, alpha exact, RGB <=3/255); race lower
  screen live from device art; memory card at mkdd/saves; Back returns to the library; other games'
  data sizes unchanged. JVM 46 + 46 and host native 44 checks pass.
- Open: case art (GameTDB download needs owner OK), AAudio opens "normal latency" inside WiiDS
  (lab got low latency), Back has no confirmation, the Download ISO copy duplicates mkdd/game.
- Case art (owner OK): GameTDB GM4E01 full cover + disc, cut by wiids/prepare_case_art.py and added to
  custom/launcher (manifest backup on M:). Hashes verified on device. The shelf, the open case and the
  lower-screen disc show it after a WiiDS restart.
- Duplicate /sdcard/Download ISO deleted (owner OK) after verifying WiiDS's mkdd/game/game.iso SHA-1 34bdef70... and the PC original. The lab app (dev.thor.mkdd.lab) now has no disc on the Thor; push files/game.iso again to use it.

## 2026-11-02 (Claude): duo screen v2 (UNSLOP pass, disc-only material) + WiiDS menu

- Owner: "use UNSLOP to fix up that bottom screen" + "full WiiDS version" = settings menu + rough edges.
  UNSLOP audit of v1: template navy dashboard, system fonts, uppercase tracked labels, eight equal
  cards with empty item wells, gold on everything, the duo card repeating the player's row, a timer
  format that disagreed with the game's own. Mockup approved ("Build this one").
- v2: every letter, number, portrait window, P1 tag, map icon and the idle logo is the owner's disc
  texture (race2d.arc + English MRAMLoc.arc), recoloured J2DPicture-style (DuoLettering). Graphite
  ground, one raised row for the player, hairlines. Map fitted by its drawn area (DuoLayout.contentFit).
  Rows show the usable item and the driver's waiting item only when held. Clock in the game's
  "00:17:067" format. P1 red = Race2D::mPlayerNumberColor[0]; LAP/TIME/rank tints sampled from the top HUD.
  MkddArt/stage_art.py stage 50 HUD textures; marker .duo-art-2 so devices rebuild (Thor did in seconds).
- WiiDS menu (DuoMenu in MkddMenuDialog: NativeActivity owns its surface, so a transparent Dialog):
  Back opens it over the frozen game (native pause = activity paused OR menu open; ALooper_wake),
  lower screen held + dimmed. Pages: Controls (face buttons GameCube/Standard, native B/X swap),
  Display (render scale 1x/2x/3x via GCN_SCALE, applies on start, Restart now), Saves (auto backup
  each start keeping 5 in mkdd/save-backups, Back up now, restore applied at next start after backing
  up the current card), Return to WiiDS. Restart: MkddLaunchActivity waits for the old pid to go.
- THOR (1.1.2-mkdd-preview/1112, cert 72eef06b...): demo + Luigi Circuit GP race match the mockup;
  menu pages, layout/scale changes, Back up now, restore -> restart (pid 30608 -> 31276, pre-restore
  backup made, marker cleared), menu mid-race freezes the clock on both screens and Start resumes.
  `input gamepad keyevent BUTTON_L2/R2` reaches on_input (key triggers path live).
- Audio "normal latency" is system-wide, not WiiDS: audioserver restarted 21:39:22 with JamesDSP's
  global effect (21:39:23); since then MMAP opens fail (-38) and FAST is denied for this stream. The
  lab's low-latency run was ~21:00, before that. Deferring the stream open didn't change it (reverted).
- JVM: DuoSceneTest 53, LowerStateTest 46 pass.
- Review (subagent, read-only) fixed: restoring the oldest backup pruned it before applying (now the
  target and the new backup are never pruned; empty target keeps the marker and fails loudly); backup
  folders named in UTC (labels local, to the second); only *.raw cards copied; settings commit()
  before Restart/Quit kills the process; a lower panel recreated while held gets the frozen state;
  volume keys pass through the menu; no re-pause once quitting; looper published atomically for JNI;
  menu art preloaded off the main thread; old lettering-less art is replaced once rebuilt; relaunch
  gives up (toast + library) if the old process outlives 3 s; storage exceptions around backups caught.
  Map parity: stage_art verified the same 15 courses (all but Peach Beach) Java marks verified.
- THOR, final build (WiiDS-1.1.2-mkdd-preview-6.apk, SHA-256 fc18fc08..., cert 72eef06b...): restore
  of the oldest of 5 backups kept it, pruned the next oldest, made the pre-restore copy, cleared the
  marker, restarted (fresh pid); labels distinct; menu pause during the opening movie and at the title
  resume normally.
- Open: the opening movie shows black in WiiDS until Start (already so before today's native changes;
  the lab log saw it play). Low-latency audio needs JamesDSP's global effect off (owner's setting).

## 2026-11-02 (Claude): 16:9 option + textured course overview; UNSLOP check of the other games

- 16:9 (Display > Aspect ratio, live): native/runtime-widescreen.patch on the pinned gcn-recomp
  (b2193d7). Flat renderer only: perspective draws squeezed 3/4 in clip x (wider view), 2D draws too
  (kept in shape in the middle 4:3), output fills 16:9. Draws that put back a copy of the frame at
  (nearly) full width are not squeezed: Double Dash composites a downsampled frame copy, which
  otherwise left a seam at the old 4:3 edge. No game-memory patch. Seen on the Thor in a Luigi
  Circuit race and at the title; two small sprites past the 4:3 edge of the title now show.
- Overview: design/bake_overview.py draws each course's own model (BMD: VTX1/SHP1/MAT3/TEX1/JNT1/INF1)
  from above, in the retail minimap's axes and bounds, so markers need no new placement. Layers: an
  intensity texture first is a detail map times the next layer (the desert's sand). Left out: anything
  over the road more than 1200 above the racing line (roofs, canopy), anything 3000 above the course's
  highest road (sky domes). 15 courses, Peach Beach excluded as before. Private output
  M:/WiiDS-mkdd-2026-11-01/overview, pushed to <ext>/mkdd/overview (survives art rebuilds). DuoArt
  uses one only when its axes/bounds equal the verified map's. On the Thor: markers on the road.
- Not yet: an on-device (Java) port of the baker, so other players' Thors build it from their disc.
- UNSLOP check (owner asked "just check"): MKWii race map, NSMBW, Brawl and Galaxy lower screens are
  already the games' own art and lettering: no slop. Small optional refinements only, owned by those
  ports: Galaxy's transient "Touch: Trackpad" chip is the one system-font label on its screen; the
  library companion repeats its control hints (body text and footer) with trailing periods.

## 2026-11-03 (Claude): release check + Codex handoff

- Thor runs WiiDS-1.1.2-mkdd-preview-11.apk (SHA-256 78e13dff..., same as installed base.apk, cert 72eef06b...):
  one APK with every WiiDS game incl. Galaxy, Brawl, Double Dash. MKDD lib build ID matches the current build;
  no source newer than the APK. JVM 53 + 46 pass. Handoff for Codex: CODEX_HANDOFF.md (release blockers listed).
