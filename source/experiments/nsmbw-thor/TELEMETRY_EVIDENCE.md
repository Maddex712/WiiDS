# Read-only lower-screen state

These fields have source evidence; live upper/lower HUD comparison is still a
separate verification step. Sample on the
guest thread at a frame boundary, publish a complete immutable snapshot, and let
Java read that snapshot. Do not expose arbitrary guest-memory reads to the UI.
All fixed addresses below are unchanged by the E1-to-E2 map.

| Field | E1 location (also unchanged in E2) | Evidence |
|---|---|---|
| Scene profile | low 16 bits of u32 `0x80428730` | `runtime/src/netplay/game_menus.cpp::CurrentScene` |
| Game mode | u32 `0x80315B98` | linked PAL reference `nsmbw_controls.cpp::CurrentContext` uses StartGameInfo +8; P1-to-E1 maps its base to `0x80315B90` |
| Shown world/course | u8 `0x80315B9C`, `0x80315B9D` | owner's canonical E1 `dInfo_c::getWorldNo` `0x800B2350` and `getCourseNo` `0x800B2360` directly read StartGameInfo +12/+13; existing warp implementation distinguishes the loaded pair |
| Loaded world/course | u8 `0x80315B9E`, `0x80315B9F` | same warp implementation |
| Character per player | u32[4] `0x80354E60` | owner's E2 translated `getCoinAll`, function `0x80060200` |
| Lives per character | i32[4] `0x80354E90` | owner's E2 `decRest`, function `0x80060600` indexes through player character |
| Coins per character | i32[4] `0x80354EA0` | owner's E2 `getCoinAll`, function `0x80060200` |
| Timer instance | pointer at `0x8042A070` | owner's E2 `createInstance`, function `0x800E3870`, stores at r13 -22032 |
| Remaining game timer | i32 at timer +4, arithmetic shift right 12 | owner's E2 `setTimer` `0x800E3910` and `execute` `0x800E3930`; timer +8 is initial i16, not current time |
| Pause/stop | u32 `0x80429F48`; pause masks 1 and 4 | owner's E2 `execute` inlines `isGameStop` `0x800B3B50`, r13 -22328; linked reference documents mask semantics |
| World-map player | pointer at `0x8042A1A0`, three floats at +0xAC | `runtime/src/netplay/world_map_nav.cpp::PlayerPos` |
| World-map connections | map pointer `0x8042A18C`; subindex +0x338C; connect +0x5C7F4, stride 0x3F08 | `world_map_nav.cpp::ConnectBase` and `ReadPoints` |

Conservative course snapshot gating: scene 5, a successful mode read, mode 0 or 1.
Modes 2 (title) and 3 (title replay) retain their identity in snapshot slot 4,
while all course/player/timer/stop fields stay unavailable. Other modes remain
unavailable. This matches the linked reference's existing
input context classifier. Keep pause separately so a paused course can retain
its valid lower HUD. Scene 3 is the world map; the current snapshot deliberately
publishes only its scene and clears all course-only fields. Gate data with scene and pointer
validity; do not render stale values from a previous scene.

For these cached MEM1 objects require four-byte pointer alignment,
`0x80000000 <= ptr < 0x81800000`, and `Memory::Contains(ptr, requiredBytes)`.
Read player-character first, validate its range 0..3, then index lives and coins.
World/course values are zero based. Pointer/window checks cannot establish object
lifetime across threads, hence guest-thread capture rather than UI memory reads.

The second-screen implementation now exports the **shown** world/course pair in
slots 5/6, matching the game's `dInfo_c` getters. Earlier snapshots exported the
loaded pair; those can differ for an internally redirected course. Numbered
course IDs 0..8 may display as 1..9. Special course IDs must not be blindly
incremented: until a particular special label has direct evidence, use a generic
course label. Existing warp comments name 20 ghost house, 21 tower, 23 castle and
37 airship, but this audit did not establish every presentation name/icon.

Player-one coins are **per selected character**. `getCoinAll` adds four selected
character entries, so the per-character snapshot is not a claimed multiplayer
total. Do not label it as a shared total. Lives and coins are one validity group;
an invalid character or failed/range-invalid member makes the complete group
unavailable. Original HUD stays intact.

Canonical clean E1 source was re-extracted and hash-verified from
`reference/nsmbw-android-e1/generated/base_translation_sources.bin` into private
`build/nsmbw-thor/e1/telemetry-audit/`. Timer `0x800E3930` passes
`int16((remaining + 4095) >> 12)` to `dGameDisplay_c::setTime` `0x80159AC0`;
`setTimer` clamps to 999. The snapshot's 0..999 validation and ceiling match that
normal timer path. Stop bits 1/4 represent pause-related game stops, not all
timer stop causes or proof that a pause menu is visible.

`tests/Test-ThorSnapshot.ps1` now defaults to the E1 checkout and writes
`build/nsmbw-thor/e1/snapshot-test/verification.json`. The current run passed 41
actual-source ARM64 checks with mock memory, including differing shown/loaded
courses, title/replay identity, world-map invalidation, failed mode reads, timer
rounding/bounds, player indexing and concurrent snapshot copies. Runtime source
SHA-256: `3c5069ee9c1e1b9d8fb3f704583c64458b79b15c88efbfd6f82fd7857fb8cd31`.
This is memory/ABI verification, not live gameplay evidence. The exact temporary
shell test binary was removed and its absence verified; no Activity or input
operations were performed.

Star coins are deliberately deferred. The owner's `setCollectionCoin` at
`0x800CEB80` reads three u32 markers at `0x803741B0`, compares each against 4,
then uses the value as a player index. This is not evidence for treating the
array as a three-bit collected mask.

`inspect_translation.py` extracts selected functions from the owner's private
source bundle and verifies each function's SHA-256 before writing it under
ignored `build/nsmbw-thor/telemetry-research/`. Do not publish those C++ files.
