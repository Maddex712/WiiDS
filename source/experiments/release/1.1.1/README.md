# WiiDS 1.1.1 native builds

Recorded build steps for the libraries that changed in 1.1.1. Paths refer to the maintainer's
workbench; the steps and options are what matter.

- `libWiiCompiled.so`, `libRetroRewind.so`: `build_native_drivers.py` (custom GPU drivers, renamed
  Dawn, Lossless Scaling, rumble glue). Retro Rewind 6.13.4 is translated from the player-independent
  mod payload; translated game code is not published.
- `libnsmbw.so`, `libmsmbwii.so`: `build_nsmbw_rumble.py` reproduces the shipped libraries from the
  accepted link first, then recompiles only the unit holding `hle/input/wpad.cpp`.
- `libgame.so`: Galaxy's ninja build after `platform/mobile/core/MobileApp.cpp` (sustained rumble).
- `libSDL2.so`: `third_party/SDL2-wiids` with Android joystick rumble (`SDL_android.c/.h`,
  `joystick/android/SDL_sysjoystick.c`).
- `libbloodframe.so`: `third_party/bloodframe/build_bloodframe.ps1` with the WiiDS patch.
- `libwiidsdisc.so`: `wiids/disc` (GameCube import validation).
- Release APK: `build_release_1_1_1.py`; release assets: `prepare_release_1_1_1.py`.
