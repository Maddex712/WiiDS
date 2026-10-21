# NSMB2-style course companion telemetry

The canonical clean E1 owner-generated source bundle was read with
`inspect_translation.py`, which verifies each selected function's SHA-256.
The private extracted functions remain under ignored
`build/nsmbw-thor/e1/progress-audit/`. This document records interfaces and
observations, not game-derived function bodies.

Schema 2 preserves the original slots 0 through 11 and appends:

| Slot | Value | Validity bit |
|---|---|---|
| 12 | Player-one horizontal position within the active area, 0..1000 | 32 |
| 13 | Three current-run Star Coin bits | 64 |
| 14 | Three previously saved Star Coin bits | 256 |
| 15 | Current shared game score | 128 |
| 16 | Active area/zone ID | 32 |
| 17 | Checkpoint position, currently unavailable (-1) | none |
| 18 | Player-one world X multiplied by 16 | 32 |
| 19 | Active area width multiplied by 16 | 32 |

Unavailable values are -1. Course extras share the original scene-5, mode-0/1
gate. Transitions/title/replays clear the complete previous course snapshot.
Capture remains on the guest thread, with immutable mutex-protected copies to
Android. No gameplay memory is written by this telemetry module.
Every extra field also requires a valid active-stage pointer and the actual
registered player-one object. Scene 5 can be selected before actors and run
globals finish initialization; zero-initialized ownership words must not be
shown as three collected coins during that interval.

## Actual player and area bounds

`daPyMng_c::getPlayer` (8005F900) reads player IDs from 80354E10 and resolves them
using 80162D00 and 80162C30. The registry has eight buckets at 80377A70, selected
by `(id & 7) * 8`; each list node has next at +4 and object at +8. The pointed
object's first word must equal the requested ID. The snapshot follows this
actual registry, with pointer checks, cycle rejection and a finite traversal
limit. It does not interpret a player ID as a pointer.

`dGameDisplay_c::AreaCheck` (80158910) reads actor positions at +172/+176/+180.
The snapshot reads finite X and Y there after resolving player one.

`dBgGm_c::InitialScrollSet` (80154A70) reads the stage pointer from SDA -21688
(8042A1C8), selects the course file from stage +4622 and the active area ID from
+4623. It reads dCd from SDA -22504 (80429E98), with 944-byte file records.
`dCd_c::createInstance` (8008EB20) allocates four such records; `loadCourseData`
(8008EC40) loads all four. `getAreaDataP` (8008E410) reads the zone-array pointer
at file +44 and count at +156, traversing 24-byte zone records by ID byte +12.
X, downward-positive Y, width and height are unsigned 16-bit fields at +0/+2/+4/+6;
the game's world-coordinate top is the negated Y value.

Actual World 1-1 crash-memory inspection exposed an important allocation detail:
the zone array lives in cached MEM2, although its stage, dCd and player objects
are in MEM1. PID23897's preserved MEM1 has stage 815E4478, dCd 80F65ACC,
player 8154B804, file 0, area 0, zone pointer 91032CDC and zone count 1.
The initial MEM1-only zone-pointer gate rejected this legitimate array and made
progress unavailable. `CourseDataPointer` now accepts either cached MEM1 or the
runtime's cached MEM2 range, using `Memory::Contains` for the full array window;
actor and manager gates remain MEM1-only. Physical/uncached aliases are rejected.
The owner's unmodified `Stage/01-01.arc`, `course/course1.bin` block 9 has a
single 24-byte zone record: X528, Y256, width6800, height385, ID0. The preserved
player position X3451.913330078125, Y-496 therefore represents 430 permille.
Private input: `build/nsmbw-thor/e1/handheld/old-course-mem1.bin` (24 MiB).

The companion computes `(playerX - areaX) / areaWidth`, clamped at the modest
entry/exit margins. This represents the active horizontal area, not a route
distance across pipes, separate areas or secret exits. Vertical/square areas,
unmatched IDs, invalid objects and out-of-area coordinates make progress
unavailable. The value is not derived from the camera or a percentage guessed
from the level number. Area end is not proof of the flagpole's position; a UI
must describe any end marker as a presentation adaptation.

## Star Coins and score

`dGameDisplay_c::setCollectCoin` (80159AF0) is the decisive original HUD source.
It reads each current-run value in 803741B0[3] and treats the sentinel 4 as
uncollected. Values 0 through 3 are player ownership and show a collected coin;
the snapshot validates this complete range before making a bitmask. It does not
mistake the ownership values for already packed flags.

In normal mode, the HUD also reads the save header at 8042A040 (SDA -22080),
selected file byte +38, and the selected 2432-byte save record at header +1728.
Loaded world/course come from stage +4620/+4621, independently of the shown
course name. Course flags are at save +108 + world*168 + course*4; low three bits
represent saved Star Coins. Super Guide does not use the saved styling branch.
The snapshot conservatively accepts normal save slots 0..2 and exposes saved
and current masks separately. UI may use their union for filled icons and retain
a distinct saved/current style. No retained host-side coin cache is used: guest
checkpoint/death/reset behavior changes the current ownership array directly.
`dCyuukan_c::setCyuukanData` (8008EF30) copies the three current ownership words
to checkpoint state, corroborating their run/checkpoint role.

`daPyMng_c::addScore` (80060690) adds and clamps the shared score at SDA -22976,
80429CC0. This is independent of character-indexed lives/ordinary coins. The
snapshot rejects negative or greater-than-nine-digit values.

Checkpoint storage contains a captured entry/restart position. This audit did
not establish the course's physical midpoint-flag position, so slot 17 stays
unavailable. Do not place a fictional checkpoint at 50%.

## Verification scope

`tests/Test-ThorSnapshot.ps1` passed 73 checks linking the actual `thor_state.cpp`
for ARM64 and running it as a temporary shell binary on Thor. Tests cover the
existing fields, correct ID-registry lookup, real active-area selection,
progress movement/clamping, vertical-zone rejection, invalid/cyclic pointers,
NaN rejection, each ownership value, separate saved/current masks, current-run
reset semantics, invalid save slots, Super Guide gating, scene clearing, and
stage/player readiness before publishing ownership or score. The MEM2 regression
uses the real 1-1 archive location/bounds and captured position, then moves the
mock player by 680 units to verify an increase of 100 permille. End-of-MEM2 and
misaligned MEM2 arrays are rejected.
Receipt: `build/nsmbw-thor/e1/snapshot-test/verification.json`. The exact temporary
binary was removed and absence verified. No Activity launch or input operation
was performed for these tests. Live gameplay is a separate required check.
