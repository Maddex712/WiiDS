# NSMB2-style lower screen

Owner correction: the bottom display should function like the 3DS game, beyond
duplicate counters. The owner explicitly selected also adding a stored power-up.
Original Wii movement/physics remain; reserve is an intentional new mechanic.
Physical controls remain the interface. Claude's shared launcher is untouched.

Reference: Nintendo's New Super Mario Bros. 2 manual, sections11/12 (PDF pages12/13):
https://csassets.nintendo.com/noaext/image/private/t_KA_PDF/manual-3DS-new-super-mario-bros-2-en?_a=DATAg1AAZAA0
The reference lower layout provides approximate course position, Star Coins,
score, lives and a stored item; map state provides a simple world overview.
Private reference-page renders: build/nsmbw-thor/e1/3ds-reference.

## Implementation

- Course snapshot schema2 has20words: previous12fields followed by approximate
  horizontal-area position, current/saved Star Coin masks, score, area ID,
  unavailable checkpoint position, player XQ4 and area widthQ4. Exact fields and
  source proof: COURSE_STATE_EVIDENCE.md. No invented checkpoint/goal coordinates.
- World snapshot header12/point12 carries actual X/Z graph, sparse IDs/links and
  player position. WORLD_MAP_OVERVIEW.md documents bounds/validity. Nodes use
  neutral styling; unlock/clear state is not inferred.
- Lower view uses native font/art and original Wii plates. Course mode has a
  moving marker, three Star Coins, score, large reserve slot and compact counters.
  World mode draws a route graph with interpolated marker. Polling is20Hz with
  display-frame interpolation. Lifecycle stop removes callbacks/presentation.
  The lower window cannot take focus or touches.
- R3 (click right stick) queues reserve redemption. Accepted original pickups
  fill one slot with overflow/displaced power-ups; small Mario's first upgrade
  creates no extra item. Redemption creates the original Wii FreeDrop actor above
  Mario. RESERVE_ITEM.md documents exact target/LR hooks, proof and exclusions.
- Reserve persistence is separate from the original save, keyed by full resolved
  NAND profile path and selected save0..2. Corrupt/unreadable files are preserved.
  A background writer coalesces changes. Automatic reset when a Wii save slot is
  deleted/recreated is not implemented; reset its reserve explicitly then.
- Extract-ThorHud.py now includes original stock-item icons and a window plate.
  Private handheld-assets/handheld-ThorHud.tar contain16images/3atlases/manifest.
  No game pixels are embedded in public source or APK artwork assets.

## Validation

Full native rebuild completed, including all translation objects affected by the
inline ABI hooks. Final runtime sources were recompiled after review. Disassembly
confirms both pickup and main-stage hooks in their actual translated callers.
No generated source was edited. Java/release packaging and signature checks pass.

Progress-verified release SHA256:
531b403e1b838c255583777341d6fc76c7a3e78f8f9b5197036da39aa271f589.
Current installed release a9498fbdd70a2163530ec9a361af7816c08342f8f30d3d98267cbef2f946e107
uses the same native library and adds reserve sequence/slot/readiness to existing
diagnostic log lines. This distinguishes an invalid context from a missing hook.
Device APK hash matches; package is non-debuggable. Prior first handheld build
e1d21c... is preserved as handheld/checkpoint-first.apk. All20 staged art/manifest
files match their host hashes. Previous ThorHud is preserved on-device as
ThorHud-before-handheld-20261019. Claude's launcher APK/source is unchanged.

Actual-source ARM64 checks:73 course snapshot,33 world graph,45 reserve including
real temporary sidecar IO and reload. These mocked guest tests do not establish
live pickup or drop behavior. Native test helpers were removed.

The first handheld APK displayed the actual35-node World1 route graph and moving
player marker, plus course counters/score matching the upper screen. Its progress
field was invalid: the actual course array is in cached MEM2 and a MEM1-only gate
rejected it. Preserved real memory and the owner's1-1 archive prove the cause.
Only the checked course-data pointer gate was expanded to mapped cached MEM2;
actor/manager gates remain MEM1. See handheld/progress-mem2-audit.json.
Corrected release PID30706 produced fields511, area0, progress34 then80 in1-1.
Concurrent progress-course-1 captures visibly show the marker and matching
lives5/coins1/time496/score100. World graph and nonzero progress are now live
verified. Current/saved Star Coin rendering is present, but a live nonzero Star
Coin transition still needs observation. Current APK PID595 verified normal
mushroom and Fire Flower reserve fills (stores2), and three R3 uses including the
initial controlled seed. The owner explicitly confirmed items dropped above
Mario and could be collected. reserve-used captures show Propeller Mario with
the stored mushroom, matching upper/lower lives5/coins4/time483/score2800.

Prior fce979... APK and hash-verified Config/banner/wiimj2d.sav were copied to
handheld/before. Earlier second-screen tar attempts are incomplete full backups.
The original game/NAND/Saves and installed MKW are preserved.

Controlled reserve test: a previously absent mod-only sidecar was staged with
one mushroom in slot0. This is a test fixture, not evidence of a natural pickup.
handheld/test-reserve-fixture.json records the exact path and cleanup requirement.
The initial seed was consumed at00:00:49; subsequent state came from two natural
pickups/redemptions. Pulled final sidecar is exactly slots[-1,-1,-1]. No synthetic
item remains. Keep the normal current sidecar and its normal earned-state backup;
do not wipe legitimate inventory as cleanup. No more device takeover was needed.

Final bounded receipt: handheld/verification.json and final-live-logcat.txt.
Original Wii .sav hash still matches its direct backup. Real process-restart
reserve persistence has source/IO tests, but was not separately checked after a
natural live fill. No full-level or broader-course playthrough is claimed.

The previous native run hits an original delayed disc/BCA fatal. Neither checks
nor metadata is changed here; live testing remains bounded by that existing
runtime failure. Never fabricate metadata or bypass a check.

## Companion design (2026-10-19, Claude)

`ThorCompanion` draws every lower-screen state on a 1240x1080 canvas (the Thor lower panel),
from the same parsed snapshots; `ThorLowerScreen` keeps lifecycle, display selection, 20 Hz
polling and the non-focusable/non-touchable window. All pixels come from the owner's SMNE01 Rev1
layouts through ThorHudAssets: the nine course-intro world strips (`preGame` worldBase) as headers,
the stock-item window corner (`frameCorner`) mirrored into windows, the stock-item rounded bar
(`pill`), the results-screen Mario portrait, original fonts (fill tinted by multiply, outline kept).
Course: WORLD w-c and lives; progress groove with the player head (start/area-end rings only, no
checkpoint or goal); three Star Coins (this run full, saved-only quieter, unknown faint); score;
large stored-item box with a discreet R3 pill only when `canUse`; coins and timer (red pulse <=100).
Map: actual graph in a window sized to its aspect, neutral stops, interpolated player marker, stored
item under the map when held. Title/menus/loading: strip, portrait, title window (loading adds three
pulsing coins). A screen change fades the new content in from the plain background, so earlier
course/world content is never left visible. Pause-related stop dims the course screen without text.
Receipt and screenshots: build/nsmbw-thor/e1/companion/.
