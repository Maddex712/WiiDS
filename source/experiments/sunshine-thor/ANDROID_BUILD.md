# Thor Sunshine Android build

The isolated source checkouts are `reference/sms-thor` (native source port) and
`reference/sms-launcher-thor` (Android host).
Their original Java namespace stays `dev.chasem.smslauncher` for the existing
SDL and native bridge; the install identity is separately `com.wither.sms.thor`.
The exported wrapper is `com.wither.sms.ThorLaunchActivity`.
The actual native SDL activity remains private.

`Build-Thor.ps1` configures the exact native source with NDK 29.0.14206865,
ARM64/API 29, Release optimization and a bounded eight-job pool by default.
It packages the separately built libraries with AGP 8.5.2, Gradle 8.8, SDK and
build-tools 34, JDK 21, then signs a non-debuggable release with the existing
host development keystore.
This is a local toolchain adaptation from the upstream Android preview.
The first SDL configuration performs many compiler feature probes.

SDL 2.32.10 is official source, downloaded from
`https://www.libsdl.org/release/SDL2-2.32.10.tar.gz` and verified SHA-256
`5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165`.
Its source is kept in ignored `build/sunshine-thor/deps/SDL2-2.32.10`.
Windows tar reports two unsupported legacy Android symlinks; neither is used
by the CMake target or the Java sources in `android-project/app/src/main/java`.

Run from the workspace root:

```powershell
./experiments/sunshine-thor/Build-Thor.ps1
```

`-PackageOnly` reuses already built native libraries.
Outputs and logs stay under ignored `build/sunshine-thor`.
Do not distribute the native library or APK from this private workbench.
The app does not contain a disc image or extracted game assets.

## Storage and launch

The Thor variant uses its own app-scoped external files root consistently for
`disc/game.iso`, `game/settings.txt`, `game/cache`, `game/game.log`,
`controller-mappings.txt` and `saves/card-a`.
On Thor this is `/sdcard/Android/data/com.wither.sms.thor/files`.
Preferences remain Android-managed private preferences.
No MKW, Retro Rewind or NSMB data tree is reused.

The versioned explicit launch accepts only the contract documented in
`LAUNCHER_INTEGRATION.md`.
It checks disc size, GMSE01 revision-zero header and GameCube magic before
starting the private game activity.
Host input verification records the full-disc checksums separately; this entry
check is an availability/version check, not a repeated full-disc hash.
Unknown protocols reject without booting; missing data opens the importer.
The entry never accepts caller filesystem paths or environment variables.

Fresh defaults are 30 FPS, one-times resolution, original 4:3 framing, and
performance experiments off.
Existing saved user settings and controller profiles are preserved.
The upstream physical-controller/SDL input route is retained without a global
AYN button swap or touch-controller overlay.

Android Back from a shared-library game launch returns through the explicit
`com.driftdroid.android.thor/com.driftdroid.android.ModePickerActivity` component
with `REORDER_TO_FRONT`.
The separate Sunshine settings/import screen also has a Main Menu button.
Normal native destruction terminates the game's own `:game` process as upstream
requires for its global heaps and threads.

## Terminal session teardown guard

The first live build rendered the native opening/title, but an activity teardown
left its original game process alive and the next activity could not initialize.
SDL 2.32.10's `onDestroy()` sends a quit event and joins its SDL thread without
a timeout.
That thread waits inside the bootstrap for the native game thread.
The existing upstream process termination placed after `super.onDestroy()` is
unreachable while that join is stuck.
The precise native wait below that join is not established by the initial logs.

The Thor host now logs lifecycle entry/exit and arms a daemon immediately before
calling SDL's terminal `onDestroy()`.
Normal SDL cleanup gets two seconds.
If it does not return, the daemon logs the Java UI/SDL thread stacks and ends
only the verified `com.wither.sms.thor:game` process, matching the upstream
requirement that the next session use fresh native globals.
Home, pause and sleep do not arm the guard.
No SDL/native game code or gameplay state is changed by this adaptation.
Logs remain private/app-scoped and can be explicitly exported with the existing
Export game log document-picker action; no automatic public export is added.

The card service writes synchronously via a temporary file, `fflush`, `fclose`
and per-file rename.
There is no additional exit-only save queue to drain and no `fsync` claim.
A forced stop can leave the previous committed file and an unfinished temporary
file; the guard never deletes saves or temporary files.
Leaving the game is not a substitute for its normal in-game save procedure.
Actual Back/relaunch and save validation must be recorded separately.

Build success alone is not device, boot, gameplay, audio, save or performance
evidence; consult the private verification receipt for the checks actually run.
