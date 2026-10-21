# Shared launcher transport for Claude

Latest: Claude's third NSMB case is present in the launcher source. Temporary
Options/two-case notes below describe the initial transport handoff. The E1
game now owns the NSMB2-style companion, including R3 reserve activation; see
HANDHELD_SCREEN.md for current schemas, live evidence and limits. The earlier
SECOND_SCREEN.md records the first passive HUD pass and
the later original disc/BCA fatal that limits gameplay.

> 2026-10-18 update: NSMB is now the third library case (no longer in X > Options). See
> build/nsmb-case-verification.json and the AGENTS.md entry of the same date.

Keep the existing Mario Kart launcher layout. This work adds a launch transport
and a temporary functional NSMB entry in X > Options. It does not create a finished
NSMB case. The renderer currently hardcodes two cases; Claude can move the
`nsmbw` action into the library when updating that design. These launcher source
changes are compiled, but the installed MKW launcher has not been replaced.

## Launcher side

New transport source in the existing launcher:
`reference/driftdroid/android/app/src/main/kotlin/com/driftdroid/android/ThorGameLaunch.kt`.

The stable game ID is `nsmbw`. From an Activity in `com.driftdroid.android`:

```kotlin
when (ThorGameLaunch.launchNsmb(activity)) {
    ThorGameLaunch.Result.OPENED -> Unit
    ThorGameLaunch.Result.NOT_INSTALLED -> /* show native NSMB runtime missing */ Unit
    ThorGameLaunch.Result.UNAVAILABLE -> /* restore the launcher selection */ Unit
}
```

Call it once, at the existing launch animation's commit point. Until then, B should
cancel normally. `nsmbInstalled(context)` means the target Activity is installed
and exported, not that the owner's game data has been imported. The NSMB entry
point handles that check in its own package. Do not route NSMB through
`MainActivity.EXTRA_PRODUCT`: that still belongs to MKW/Retro Rewind.

The temporary Options row calls the same transport directly using the existing
menu activation behavior. There is no NSMB disc animation until Claude adds its
case. The first two MKW/RR action indices remain unchanged.

The launcher manifest adds visibility only for `com.wither.nsmbw.thor`.
No changes to ThorLauncherView, artwork, menu geometry or music were made here.

## Runtime side

Package: `com.wither.nsmbw.thor` (build with `-PthorBuild=true`).
Component: `com.wither.nsmbw.ThorLaunchActivity`.
Explicit intent action: `dev.thor.launcher.action.PLAY_GAME`.
Extras: `thor_protocol` = integer 1, `thor_game` = string `nsmbw`.

The entry point checks the imported disc header and executable hash before
opening GameActivity in `:game`; otherwise it opens the existing import screen.
The native runtime never loads in the shared launcher's process. Builds without
the Thor property retain upstream's package and USA revision-1 contract.

For a session launched here, the existing in-game Android Back menu labels its
return action `Main Menu`. It opens the fixed shared Thor launcher component,
then finishes the NSMB game Activity/process using upstream's lifecycle. Import
and session-restart paths retain the return destination. No arbitrary component,
filesystem path or callback intent is accepted from launch extras.

## Validation status

Both NSMB Java and MKW Kotlin compile checks passed with Android API 34. This does
not validate cross-app launch or return on device. Packaging requires the selected
revision's exact native library and staging receipt; clean E1 is now explicitly pinned.

## Lower-display state

The NSMB package now has `com.wither.nsmbw.ThorGameState.read()`. Call it after SDL
loaded its native library, from the game Activity's process. It returns null until
the guest publishes a snapshot. Native Capture runs on the guest thread and JNI
copies a complete snapshot under a short mutex; Java never reads guest RAM.

Inspect `state.has(ThorGameState.COURSE)` (and PLAYER_ONE/TIMER/STOP_FLAGS) before
using optional fields. Unavailable numeric fields are -1. `sequence` increases
per snapshot. `scene` is the raw scene profile; title/replay is excluded from
course data. `world` and `stage` are zero-based internal IDs, so special stages
need real labels rather than blindly displaying stage+1. `character`, `lives`
and `coins` refer to player one's selected character, not array element zero.
`seconds` matches the game's Q12 timer rounded up to its displayed second.
`gameStopped` is a boolean for the two known pause-related stop bits (1 and 4).
It does not expose the raw stop word or every stop cause, and is not a precise
standalone pause-menu detector.

Current course snapshot schema2 has20 words. Progress, current/saved Star Coins,
score and active area extend the original fields; use their validity bits.
Checkpoint position remains unavailable. Progress is an approximate horizontal
position in the actual current zone. World-map schema1 has a12-word header and
12 words per point; ThorWorldMap.read() returns actual X/Z graph nodes and links.
Read COURSE_STATE_EVIDENCE.md and WORLD_MAP_OVERVIEW.md for exact schemas.

ThorReserveState.read() returns8 words: schema1,sequence,item,canUse,queued,
selectedSave,uses,stores. Item-1 is empty; supported kinds0/1/4/11/13/14 use the
original Wii item identities. GameActivity.nativeUseThorReserve() queues a request
for the current offline single-player course; it never invokes guest actors on
the UI thread. The physical R3 handler uses that bridge. RESERVE_ITEM.md records
this owner-authorized gameplay addition and persistence. The existing world-map
item system is unchanged. Upper HUD remains present.

ThorLowerScreen owns the game-side passive Presentation and original-art loader.
It must remain in the game process, independent of the launcher. HANDHELD_SCREEN.md
and its private receipts distinguish actual live verification from mock tests.
