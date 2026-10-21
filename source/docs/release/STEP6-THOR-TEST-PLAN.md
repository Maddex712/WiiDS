# Step 6: first run of WiiDS.apk on the Thor

`build/WiiDS.apk` installs as a new app (WiiDS, `io.github.maddex712.wiids`) beside the private
builds, so nothing existing is touched. Nothing below has run on a device yet.

## Install
1. Install `build/WiiDS.apk` (adb install, or copy it over and open it).
2. Open WiiDS. The Games screen should open by itself. Allow All files access when asked.

## Games screen
- [x] It finds the images in your ROMs folder and names each one (format, size).
- [x] Each card shows the exact disc; a wrong region/revision/NKit file shows the reason.
- [x] Import Mario Kart (PAL ISO or WBFS): progress, then its menus, sounds, cases and maps build (PAL ISO: under a minute).
- [x] Import NSMB (RVZ works); the included MSMBWii mod installs itself.
- [x] Retro Rewind: install from the launcher (official servers); its maps then bake in the background.
  First install on the Thor: 6.13.0 full (pinned SHA) + 6.13.1 delta, 4176 files, under 3 minutes;
  maps baked in about 4.5 minutes right after; 6.13.3 correctly held back (code not supported).
- [x] Import Sunshine (RVZ works; stored as an ISO).
- [ ] "Choose file" works for a single image without All files access.

Also passed: the Games screen opens by itself on first run; NSMB's lower-screen art built on the
device is byte-identical to the PC tool's output.

## Launcher
- [x] After Mario Kart's art is built, the Wii-menu launcher appears (menu art, sounds, title theme).
- [x] All five cases (incl. Retro Rewind) show composed covers and discs; selecting each launches its game
  (a disc imported while the launcher is open now appears on return).
- [x] Without Mario Kart imported, the fallback menu lists the other games and "Your games".

## Each game
- [x] Mario Kart: boots (game data rebuilt from the disc), lower-screen companion.
- [ ] Mario Kart: lower-screen map + HUD on a few courses (hands-on).
- [x] Retro Rewind: boots from the freshly installed pack (title screen, 60 FPS).
- [ ] Retro Rewind: lower-screen maps on a few retro and custom tracks (hands-on).
- [x] NSMB: boots from an RVZ import (BCA answer), lower-screen HUD, save created.
- [ ] NSMB: score farming fixed; release sound (hands-on).
- [x] MSMBWii: boots; its own save is created (separate from NSMB).
- [ ] MSMBWii: World 1-1 is the custom level (hands-on).
- [x] Sunshine: boots (relocated SDL2), lower-screen art loads (55 images).
- [ ] Sunshine: lower screen during play, controls (hands-on).
- [x] Leaving each game returns to the WiiDS launcher (Mario Kart, NSMB, MSMBWii, Sunshine); each game process ends.

## Fixed during the Thor pass (2026-10-20)
- NSMB/MSMBWii crashed on every launch after Sunshine had run (shared "settings" file, frame_rate
  int vs String). NSMB/MSMBWii now use nsmbw_settings_<variant>. Verified on the Thor.
- Sunshine's fallback process kill expected ":game" (now ":sms_game").
- NSMB's in-game import staged on external storage and deleted the disc first (now staged beside it,
  swapped with rollback, per variant).
- Disc titles/messages sanitised before NewStringUTF; Cancel during import preparation is kept;
  GameCube ISOs are always written full size (CISO/trimmed images).
- Case art reloads when a disc is imported after the launcher opened (checked every second while
  showing; the case manifest is replaced atomically); Y works from an open case.
- Review pass: per-variant staging for NSMB/MSMBWii imports, MSMBWii "mod not installed" wording,
  launcher resume hooks no longer tied to the device model, locked channel text dimmed.
- No Nintendo artwork or DriftDroid branding in the APK: drawn channel buttons, WiiDS wordmark,
  new icon, synthesized click sound.

## If something fails
Diagnostics in the launcher exports logs. Process names: main (Mario Kart), `:nsmbw_game`,
`:msmbwii_game`, `:sms_game`; `adb logcat` tags WiiDsArt, ThorRaceMap, NSMBW, SMS-Thor, SDL.
