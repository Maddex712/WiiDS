# Steps 4–5: one APK, mod download

`scripts/Package-Thor.ps1` now builds **build/WiiDS.apk**: app id `io.github.maddex712.wiids`,
label "WiiDS", minSdk 30, signed with the WiiDS release key (153 MB, libraries compressed).
It installs beside the private standalone builds (different app id), so nothing on the Thor is
replaced. Build order:

1. `experiments/release/stage_merged.py`: strips and stages the other games' libraries and the
   NSMB runtime asset trees (needs the native builds below).
2. `experiments/release/stage_licenses.py`: licence notices for all four games and the disc/art code.
3. `scripts/Package-Thor.ps1 -Variant Release` (passes `-PwiidsApp=true`).
4. `experiments/release/scan_game_bytes.py build/WiiDS.apk`: 0/450 executable windows found.

## How the four games live in one app

| Game | Code | Native library | Processes | Data |
|---|---|---|---|---|
| Mario Kart Wii | com.driftdroid.android (host) | libWiiCompiled.so | main | files/WiiCompiled |
| NSMB Wii | com.wither.nsmbw | libnsmbw.so | :nsmbw, :nsmbw_game | external/nsmbw |
| MSMBWii | same classes, `MsmbwiiActivities` subclasses | libmsmbwii.so | :msmbwii, :msmbwii_game | external/msmbwii |
| Super Mario Sunshine | dev.chasem.smslauncher, com.wither.sms | libsms.so, libsmsbootstrap.so, libSDL2.so | :sms, :sms_game | external/sms |

- `NsmbVariant` picks NSMB or MSMBWii from the process name; `NsmbBuild` replaces the
  standalone app's BuildConfig. Both read the disc from `files/games/nsmbw`; MSMBWii also reads
  `files/games/mod` (overlay root and `<dvd_root>/../mod`).
- NSMB products are linked under their own names: CMake cache option `MKW_ANDROID_LIBRARY_NAME`
  (default `main`, so the standalone builds are unchanged). Rebuild script:
  `build/nsmbw-thor/rebuild-wiids-names.sh`.
- SDL: Mario Kart and NSMB share one SDL 3.4.4 Java glue (their changes merged: 64 MB SDL thread
  stack, guarded aurora surface hook, Mario Kart's accelerometer skip as
  `SDLActivity.mIgnoreDeviceAccelerometer`). Sunshine's SDL 2.32.10 glue moved to
  `org.libsdl.sdl2.app`; `libSDL2.so` is built from `build/sunshine-thor/deps/SDL2-2.32.10-wiids`
  (JNI prefix and RegisterNatives class names only; `build/sunshine-thor/build-wiids-sdl2.sh`).
- One `libc++_shared.so` (NDK 27) and one `libpng16.so` serve every game (checked: no missing symbols).
- The launcher starts the games in-app (`ThorGameLaunch`); a game counts as available once its
  disc (and for MSMBWii the mod) is imported, otherwise the Games screen opens.
- After each import the game's art is built from the disc in the background (`WiiDsArt`; see
  STEP3-ART-ON-DEVICE.md).

## Mods (all permissions granted, see PERMISSIONS.md)

- **MSMBWii** ships inside the APK (`assets/mods/MSMBWii.zip`, staged by stage_merged.py, exact
  known release). It installs itself once the NSMB disc is imported ("Install mod" on its card
  otherwise). Builds without the bundled zip fall back to the official download, then to a copy
  the player picks. Unpacks `msmbwiijala/`, `riivolution/` and the credits readme to
  `files/games/mod`, writes `riivolution/config/SMNE.xml`, keeps the mod's save on reinstall.
- **Retro Rewind**: `libRetroRewind.so` (the recompiled Code.pul) ships in the APK; the pack is
  installed and updated from Retro Rewind's official servers by the Thor updater
  (`ThorRetroRewindUpdater`). First install (2026-10-20): the newest release on a code lineage
  this build runs is found from the small update archives; the newest full archive on that lineage
  (today 6.13.0, checked against its pinned size and SHA-256) is downloaded, every used entry is
  verified, its RetroRewind6 tree is unpacked into a stage, newer deltas (6.13.1) are overlaid, and
  the pack is published by rename under the same journal/lock/game lease as updates; an
  interrupted first install rolls back to "not installed" and keeps the download for a retry.
  Host fixtures: `diagnostics/ThorRetroRewindUpdaterTest.kt` (113 cases, 40 for first install). Its case is composed from
  the Mario Kart disc, and its lower-screen maps are baked on the device from the installed pack
  (`BuildRetroRewindMaps`: every track in Tracks/, CT/Tracks/, BT/Tracks/; only changed tracks are
  rebaked after an update; 401 of 402 tracks map, about 4 minutes on the PC).
- Release build: `scripts/Package-Thor.ps1 -Variant Release -IncludeRetroRewind` (after
  stage_merged.py and stage_licenses.py).

## Not yet verified (needs the Thor)

Every game boots from the merged app; NSMB/MSMBWii BCA answer; Sunshine with relocated SDL2;
the Games screen, import, art build and mod download on the device.
