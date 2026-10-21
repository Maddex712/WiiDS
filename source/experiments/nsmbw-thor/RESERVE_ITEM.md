# Thor reserve item

The owner explicitly requested a stored power-up in addition to the lower-screen
NSMB2 adaptation. This is a gameplay addition; the rest of the Wii mechanics are
retained. The feature uses the original Wii item actor, graphics, collision and
power-up animation. It does not assign a power-up directly or change disc checks.

## Interfaces

`runtime/include/thor_reserve.h` in the isolated E1 runtime exports:

- `ReadWords()`: `[1, sequence, pickupKind, canUse, queued, saveSlot, uses, stores]`.
  Pickup kind `-1` is empty; `0` mushroom, `1` fire flower, `4` ice flower,
  `11` propeller mushroom, `13` mini mushroom, `14` penguin suit.
- `RequestUse()`: thread-safe, coalesces presses; no guest calls from Android UI.
- `Tick(CpuContext*)`: called by the original stage execution boundary below.
- `ObserveAcceptedPickup(CpuContext*)`: observes an already accepted collection.

The Android input bridge can map physical R3 to `RequestUse`. Polling native state
must not call `Tick` from VI, a display thread, JNI or a guest interrupt callback.

## Owner-input source proof

Addresses are from the strict clean USA E1 translation, not a PAL symbol guess.
The public community decomp and mkwcat project were consulted for field names;
their function addresses differ and are not used as runtime constants.

`generated/build_shards/base_common/shard_bca5d78e7c205b8eb40bd3b1.cpp` contains the
original successful collection path. At `loc_80A28688`, before the call to
`800E24B0`, it explicitly flushes r29/r31 into the CPU context and sets LR to
`80A286A4`. Only this exact target/LR pair invokes the observation hook. The
original function then continues. r31 is the item actor; r29 is 1 for a changed
form or 2 for a duplicate form. Kind is uint16 at item+0xDCA; recipient is signed
byte item+0xDFF. The hook also validates profile 60 and a unique ID.

The original `80145AC0` writes the pending power-up to player+0x14E0. The current
form at player+0x1090 is still the displaced form at the observation boundary.
Duplicate/overflow collection stores the picked-up item; changing forms stores
the displaced form when one exists. Small Mario's first upgrade creates no extra
item. A single slot retains the latest qualifying item. A redeemed actor cannot
refill the slot, including repeated notifications and multiple outstanding items.
The issued-ID set has a 4096-entry limit; hitting it disables further redemption
for the current process while retaining the stored item. This avoids unbounded
growth without forgetting potentially live issued actors.

`shard_e7c0ad04f3a530d2f98d2318.cpp` contains the normal stage manager execution
path. It explicitly sets LR `808307EC` before `800E3930` (stage timer execution),
followed by other game manager calls. The exact target/LR invokes `Tick` on the
guest thread. This avoids reentering actor creation from a VI interrupt.

`8005FFB0` counts entries in `80354E50[4]`; exactly one entry is required.
`8005F900` resolves that player's ID using the original game's object registry.
The player must have profile 13, valid current/pending forms, no pause/stop bits,
no out-of-play/stunned/disabled-state/goal/stop-execute flags and no demo mode.
Title/replay modes are rejected. Getter calls use an isolated CPU register file
and a checked scratch frame below the active stack.
`NetplaySession::Active()` rejects the entire reserve context, including a LAN
session with only one entered player. Host R3 requests are not lockstep inputs;
online collection and activation must not mutate offline reserve state.

The original item constructor `80064610` receives profile 60, raw item settings,
position 64 game units above the current player, null rotation and the player's
layer. Raw kind mapping is from original `80A2C050`: 0 mushroom, 9 fire, 14 ice,
21 propeller, 25 mini, 17 penguin. The original actor owns subsequent motion and
collection. The `create_type` field is 8 (settings `0x08000000 | rawKind`): the
original constructor switch at `80AF0E34[8]` points to `80A270B4`, which selects
the original FreeDrop state `80B168F8`. This makes the reserved item fall above
the player using the original state implementation. The original actor owns
collection. Reserve consumption occurs only after a non-null allocated actor is
returned. The live behavior of the original deferred creation still needs a
device check; allocation alone cannot establish successful onCreate/rendering.

## Persistence

`RuntimeNandPath::DiscoverNandRootPath()` is the exact NAND resolver used by
NAND/ISFS. `ThorReserve-<root-key>.bin` is stored beside that resolved NAND, with separate
slots for selected saves 0..2. The selected slot is read from the original save
manager at r13-22080, header+0x20, selected byte +6. The root key is a stable
64-bit FNV-1a of the complete normalized resolved NAND path in UTF-8. Configured
sibling roots such as `NAND` and `NAND-alt` therefore retain independent reserve
files even though they share a parent directory. The older unkeyed sidecar is
left untouched; it is not automatically imported into an ambiguous profile.

The sidecar is exactly 20 bytes: eight-byte `THRRSV1\0` magic and three little
endian signed item values. One bounded read validates its exact length, magic and
item kinds. Invalid input is preserved as `.invalid-N` before allowing writes;
if preservation fails, writing is disabled. Changed state is coalesced to a
background writer; an existing unreadable sidecar also disables writes. The
changed state is flushed to `.tmp`, the prior complete file copied to `.bak`,
then atomically renamed on Android. No `.sav`, NAND contents, checksums or
permissions are edited. Requests are bound to the selected save so a switch
cannot consume another save's reserve. Paused gameplay keeps its visible item.

Automatic handling of deleting/recreating a Wii save slot is not implemented;
the sidecar belongs to that profile and slot. Reset it explicitly when replacing
a save with an unrelated new game. No complete-level or power-loss durability
claim is made.

## Verification

`tests/Test-ThorReserve.ps1` compiles the actual reserve source for ARM64 with mock
guest memory and original-function invocations. It runs outside the game process
with no Activity launch or input, using real temporary sidecar IO. It tests
collection, duplicate suppression, spawn settings/position, rejection states,
failed allocation retention, single consumption, separate saves, queued save
switch cancellation and a fresh-process persistence reload. Temporary files are
removed afterward. This does not prove live addresses or in-game rendering.

Receipt: `build/nsmbw-thor/e1/reserve-test/verification.json`.

Bounded live verification,2026-10-19: installed releasea9498fbd... PID595 on Thor.
A controlled mushroom seed was consumed, then normal gameplay stored a mushroom
and a Fire Flower (stores2). Physical R3 used each, with uses3 total including the
seed. The owner explicitly confirmed items visibly dropped above Mario and could
be collected. Concurrent reserve-used capture shows Propeller Mario and the
stored mushroom. Final pulled sidecar is empty in all3 slots; no test inventory
remains. Current sidecar/backup now represent normal earned gameplay and remain.
Original Wii .sav hash still matches its pre-test backup. Actual live restart
persistence and all6 item variants have not been exercised. Source/IO tests
remain the evidence for those paths. See handheld/verification.json.

Changing `abi_bridge.h` invalidates all generated shards because the original
generated static calls inline `ApplyRuntimeCallOptions`. Registry-only native
overrides cannot intercept these direct same-shard calls. Rebuilding all affected
translation objects is required; no generated shard was patched.

References consulted (public source only):
https://github.com/NSMBW-Community/NSMBW-Decomp
https://github.com/mkwcat/nsmbw-project

## Same-form rule (owner choice, 2026-10-19)

The spare is only dropped when it would change Mario: not when it is his current form, and a
mushroom not once he is big or powered up (small and Mini Mario still use it). Otherwise R3 or a
touch does nothing and the item stays in the slot, so it can no longer be caught and stored back
in a loop. `published[3]` (canUse) carries the same rule, so the lower screen hides the R3 hint.
The lower screen's stored-item box is touchable (window stays non-focusable) and requests the
same queued use as R3.

## NSMB2 behavior (owner choice, 2026-10-19; replaces the same-form rule above)

The owner asked for the box to work exactly as in New Super Mario Bros. 2 (Mario Wiki "Item
storage", NSMB/NSMB2 sections):

- Storing: a duplicate goes into the box, including a caught spare (it returns to the box, as on
  3DS). Changing power-ups stores the displaced one, except Super Mario's mushroom. Mini Mario
  turning Super keeps nothing. Small Mario stores nothing. A Super Mushroom never replaces a
  better stored item but does replace a stored Mini Mushroom (`Accepts`). The redeemed-ID block
  and its 4096 cap are gone; the per-notification duplicate guard remains.
- Using: R3 or a touch always drops the item in a course, even one Mario already has.
- Drop: the item is created at the top of the visible area (dBgParameter_c instance at
  r13-22656, screen top +0x3C, height +0x44; never below the old point 64 units above Mario),
  follows Mario's x for 45 frames, then falls with item gravity through anything in its way
  (position written each frame, speed zeroed) until it is 24 units above Mario, when the
  original FreeDrop state takes over.
- Lower screen: a stored item warps in as twelve yellow/white sparkles (380 ms) before the pop.

Tests: `tests/Test-ThorReserve.ps1` on the Thor: 54 checks plus reload/sibling/corruption,
including priority, same-form use, top-of-screen spawn, hover/follow, fall-through and hand-off.
Device, release d4f692d4...: in 1-1 a stored mushroom dropped from the top of the screen above
Mario, fell and was caught (small to Super, nothing stored). Owner sidecar restored byte-identical
(slot 0 mushroom), wiimj2d.sav hash unchanged (3b2eb6c0...).
