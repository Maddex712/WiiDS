# WiiDS 1.1 work plan

Status: local development, 2026-10-25. No publication is part of this task.
The existing installed build and private saves must remain recoverable.

October 24: owner resumed Android WiiDS 1.1 work, endorsed the quality-of-life
direction and requested individual game/mod shortcuts for Cocoon. The current
achievement browser and motion controls should be preserved. The uninstalled
Casual earning candidate and local World 1 evaluator remain paused. No device
install, public release, or iOS work is part of this shortcut change.

Later October 24: the owner brought the Thor back for a physical polish pass,
including authorized in-place development installs and save/shortcut/resume
checks. See [the live release record](release/1.1-THOR-POLISH.md). Publication
remains outside this pass.

Following shortcuts, the agreed direction is a shared save manager, investigation
of the recorded NSMB resume crash, finishing the existing Galaxy touch-pointer
acceptance checks, library favorites/recently played/last selection, easier shared
controller settings, and measured per-game graphics presets. Save management
comes first, then resume reliability. These are planned work, not completed
features or verified performance gains.

## 1. NSMB Wii and MSMBWii tilt

Confirmed cause: the Thor Java shell defaults `motion_tilt` to false, so the
accelerometer listener never starts. Motion already reaches `nativeSetTouchState`,
`VirtualRemote::Sample`, simulated Wii Remote acceleration and KPAD `acc_vertical`.
The right stick is an existing alternative to physically tilting the handheld.

Enable tilt and physical shake by default without overriding an explicit saved
preference. The owner confirmed physical tilt works and requested physical shake
throughout play as well; button shake remains available. Clear
queued samples and shake holds on pause/restart. Keep the sensor's display
rotation mapping and test both tilt directions at each rotation.

Acceptance: actual Thor sensor registration, genuine tilt platform movement in
both directions, right-stick alternative, no stuck motion after pause or Home,
and no save/profile replacement. Automated sensor math checks alone do not prove
the platform works. A later sensor-fusion/recenter option can improve flat grips.

## 2. Direct frontend launches

October 24 addition: provide **Options > Game shortcuts > [game] > Add shortcut**
using Android pinned shortcuts. Retro Rewind and the other mods have distinct
stable IDs and use the existing frontend/profile gate. Cocoon's documented Add
Shortcut support requires it to be the default launcher. The user chooses which
shortcuts to add; no duplicate app installation or ROM URI is needed. Physical
Cocoon 3.07 physically accepted Retro Rewind and NSMB pins. Retro Rewind reached
its license screen and returned through Main Menu; NSMB cold-launched and
reloaded the restored World 1 save. The later task-only resume correction has
93 policy checks and a same-PID Thor check. Other launchers/all-game acceptance
remain separate. See
[the shortcut instructions](FRONTEND_LAUNCHING.md).

Add a public Android entry point with stable IDs: `mkw`, `retro-rewind`, `nsmbw`,
`msmbwii`, `sunshine`, `eclipse`, `galaxy`, `kaizo`. Accept an explicit game ID,
`wiids://play/<id>`, or a local disc URI. Disc requests identify the real header
through the existing own-disc reader and check region/revision. A filename never
selects a game. A base disc cannot implicitly select its mod.

Reuse the library's existing launch, readiness, update/recovery and process/profile
gates. First launch still requires importing/preparing the player's disc. Do not
export the individual native runtime activities. Requests cannot supply arbitrary
components or alter profiles. Document the explicit intent for Cocoon and other
frontends. Cocoon's actual player configuration and return behavior need testing.

Acceptance: cold/warm launches, unsupported ID/disc rejection, missing-data UI,
separate original/mod profiles, two consecutive different games, Home/Back, and
the actual Cocoon launch configuration. Test the full route, not just parsing.

## 3. Galaxy touch Star Pointer

The upper screen already has native touch pointing; the lower screen currently
uses touch only to rotate/pinch its planet. Give the lower screen a visible mode
switch: **Star Pointer** or **Planet**. In pointer mode, map a fitted 16:9 rectangle
on the lower screen to the upper game's -1..1 pointer coordinates. Touching or
dragging points; it must not press A or B or shoot star bits automatically.

Pass coordinates through a thread-safe mailbox, consumed on the native input
thread. Touch takes priority over stick pointing while held. Release/cancel,
pause, dialogs and display removal clear it. Keep planet manipulation available
in Planet mode. A cursor over the 3D lower planet cannot directly select upper
screen objects: the cameras differ; pointer mode maps screen coordinates instead.

Acceptance: collect real visible star bits by sweeping, use pointer menus, retain
Mario movement/buttons, no ghost click/shoot, joystick handoff and cancellation,
and original/Kaizo isolation. Build and test the final native bridge, not only UI.

October 25 acceptance: original Galaxy's file-menu hover/A confirmation,
movement/spin, Planet rotation, touch/stick handoff, four sleep/wake cycles and
Home/shortcut return passed on dev15. Kaizo's separate save loaded and lower
touch alone collected three star bits in Castle Gardens. Pointing also worked
after Kaizo sleep/wake. All 48 checked external saves/settings remained exact.
The owner confirmed physical controls, NSMB tilt/shake and longer play work well.
Local 1.1/code1100 APK and matching source candidates are ready; publication is
still a separate action. See [the release record](release/1.1-THOR-POLISH.md).

## 4. Dolphin save transfer

October 24: the [shared save manager](SAVE_MANAGER.md) implements all eight
standard game/mod profiles for WiiDS-format backups, export, restore with a
verified previous-save backup, and interrupted-restore recovery. Android source
and local packaging pass. Actual NSMB backup/export/same-save restore and
fresh-process reload passed on Thor, as did Galaxy and Mario Kart backups.
October 25: dev17 adds explicit file-picker Dolphin imports for all eight
standard profiles, using supported Wii raw saves/SD exports and GameCube GCI
entries. It validates and converts the payload, takes a dated backup and merges
through the existing transactional restore. All 124 adapter checks passed on
host and isolated Android production paths; actual NSMB same-save file-picker
import preserved all 48 accessible save/settings files. See [supported formats
and limits](SAVE_MANAGER.md). Whole cards/NAND, arbitrary archives, save states
and automatic raw Dolphin export remain outside scope. Galaxy also gains
Original/neutral Smooth/optional Smooth + glow Picture choices; full reporter
scene equivalence has not been established. The
[test checklist](TESTING_1_1_PREVIEW.md) also covers shortcuts and the native
NSMB/MSMBWii inactive-frame copy guard. A deeper sleep/wake test reproduced the
crash; diagnostic host offsets identified an ImGui frame-lifetime violation in
the native menu overlay. Both corrected entries pass 58 actual-ImGui Android
checks; dev15's physical acceptance is recorded separately.

Inventory each engine's actual on-disk format first. Wii NAND payloads and
GameCube GCI/memory-card exports require different adapters. A Dolphin save state
is a snapshot of emulator execution and is not interchangeable with native-port
execution. Region/revision and mod compatibility must be checked per game.

Start with ordinary in-game saves, explicit game/profile choice, size/structure
validation and copy-only export. Import requires a stopped game, a hash-verified
backup, staging, atomic replacement, rollback and a clean-process reload test.
Do not silently scan arbitrary Dolphin folders or replace a profile. Keep any
WiiDS-only companion/reserve metadata separate. No blanket compatibility claim.

## 5. Achievements and optional RetroAchievements progress

**Current 1.1 preview:** preserve Claude's dev7 achievement library, base sets
plus subsets, mod-specific sets, existing account completions, daily/manual
refresh and badge downloads. No new earning client or local World 1 evaluator
is enabled. All 91 recorded paused experiment files remain unchanged, and the
private preview restores the 12 exact dev7 Android source files. The research
and older local slice below are historical context, not active preview behavior.

**October 23 correction:** the moderator's October 22 reply in
[topic 37862](https://retroachievements.org/forums/topic/37862) permits existing
console sets when Casual-only without approval. The earlier policy interpretation
below is superseded for that case. [The client work](RA_CASUAL_CLIENT.md) uses
rcheevos and authentic disc identification with Hardcore disabled. NSMB currently
needs a supported revision 2 image and matching runtime; no live earning is claimed.

Local NSMB slice implemented: four versioned rules, isolated atomic ledger,
achievement screen and optional Keystore-backed RA history import. See
[the preview guide](ACHIEVEMENTS.md) for precise conditions and test limits.

Official RA support is externally constrained, not just a programming task.
Its standalone policy excludes unofficial ports; its community manager states
recomp/decomp use of existing console sets is not endorsed and Hardcore should
not be expected. Do not impersonate Dolphin, spoof a supported client, or submit
WiiDS local progress as official RA unlocks.

Investigate in this order:

1. Document the runtime memory models, title/disc identity and frame hooks. A
   static recompilation may preserve guest RAM; native Sunshine/Galaxy objects
   do not preserve all original console addresses. Login alone cannot bridge this.
2. Prepare a precise technical proposal for any future discussion with RA. Sending
   that proposal requires the owner's explicit instruction. Official support is
   unavailable until accepted; it is not a promised 1.1 feature.
3. Build WiiDS's own offline achievement ledger with original, versioned
   definitions and per-game/mod/profile isolation. Unlock from verified game
   events, not inferred filenames, a loaded completed save, or a single counter
   sampled after load. Persist atomically; preserve progress across updates.
4. Optionally read a player's RA achievement metadata and existing completions
   through the official **Web API**. This API requires a web API key from the
   player's control panel, distinct from emulator Connect API login. Do not
   collect passwords in chat or embed a shared API key. Store any per-user key
   through Android Keystore; support disconnect and account switching.
5. Keep three concepts separate: RA metadata, **earned on RetroAchievements**
   completions, and **earned in WiiDS** completions. A verified mapping can show
   an imported RA completion alongside a corresponding WiiDS achievement; an
   untested description match must not mark a different achievement earned.
   Local progress never writes back to RA. Cache metadata, sync on demand with
   backoff, and tolerate offline use without losing either source of progress.

The read-only progress mirror is technically viable under the documented API.
It does not provide RA's private achievement evaluation logic or make the native
games compatible with console memory rules. Importing completed badges and
implementing correct local unlock conditions are separate work items.

## Verification and release boundaries

Preserve the current physical APK (version code 906) and use a same-code local
candidate so an in-place rollback remains possible. Keep a before/after native
and asset inventory, owner signing identity and nondebuggable flag. Never clear
app data or uninstall. Do not replace the published 1.0 APK/source or rewrite Git
history. The earlier personal-path audit remains a separate cleanup item.

## Primary references

- [RA standalone policy](https://docs.retroachievements.org/general/standalone-support.html)
- [RA community manager on recomp/decomp clients](https://retroachievements.org/forums/topic/37862)
- [rcheevos integration](https://github.com/RetroAchievements/rcheevos/wiki/rc_client-integration)
- [RA Web API access](https://api-docs.retroachievements.org/)
- [RA game metadata and user progress](https://api-docs.retroachievements.org/v1/get-game-info-and-user-progress.html)
- [Cocoon emulator setup](https://cocoon-shell.com/wiki/emulator-setup/)
