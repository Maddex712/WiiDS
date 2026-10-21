
# DriftDroid

A native Android port of [WiiCompiled](https://github.com/patchzyy/Wiicompiled), a static
recompilation of Mario Kart Wii to native code. This fork adds everything the game needs to be a
phone app: touch controls, motion steering, a mobile settings sidebar, a launcher, and an in-app
way to turn your own game disc into a playable install, with no PC step in between.

There's no emulator in the loop, no interpreter, no JIT, no PowerPC anywhere at runtime - the game
runs as translated native ARM64 code, same as the original desktop project.

> [!IMPORTANT]
> There is no Nintendo code, no assets and no game data anywhere in this project or its releases.
> You need your own legally dumped copy of the PAL version of the game. Nothing in this repo, in
> any release APK, or in the build process contains or downloads Nintendo IP.

---

## Status

DriftDroid is playable end to end: install the app, point it at your own disc
image once, and it builds a working game on the device. No PC step, no manual
file staging.

- **Races, menus, audio, saves and online play work**, with touch controls or a
  physical controller.
- **Both Original Mario Kart Wii and Retro Rewind** are supported, chosen from
  the launcher screen. Retro Rewind 6.13.0 and 6.13.1 are the versions this
  release runs.
- **Getting the game onto the device is done in-app**: pick your disc image and
  DriftDroid verifies it's a clean PAL `RMCP01` disc and extracts it for you.

It is still an active, personal fork rather than a finished product. Expect
rough edges, and expect performance to vary a lot between devices - see
[Performance](#performance).

## What it does

**Native rendering via aurora.** The graphics layer is built on
[aurora](https://github.com/encounter/aurora), a source-level GameCube & Wii
compatibility layer, running on Vulkan through Google's Dawn.

**An OpenGL ES fallback.** Some phones ship a Vulkan driver too broken to start
the game at all. The launcher's **Graphics...** screen can switch the renderer
to OpenGL ES for those devices. It is slower than Vulkan by nature - OpenGL only
lets one thread talk to the GPU, so shaders compile on the render thread and the
multithreaded graphics path is unavailable - so leave it on Automatic unless
Vulkan doesn't work for you.

**Touch controls, tuned for Mario Kart Wii specifically.** Drag-anywhere
steering joystick, GameCube-diamond-style face buttons, D-Pad (needed for bike
wheelies), and L/R for items and drift. Every control can be repositioned,
resized, or hidden from a long-press edit mode, and double-tapping A can hold
acceleration for you.

**Motion steering.** Tilt-to-steer, with its own calibration and sensitivity,
for anyone who played the original with a Wii Wheel.

**Real controller support.** Controllers are fed to the game as a GameCube-style
pad. Mappings are positional (`south`, `east`, `west`, `north`) rather than
Xbox-labelled, so one config makes sense across Xbox, PlayStation, Nintendo and
generic controllers.

**Unlocked framerate with interpolation.** The original game is hard-locked to
60 fps. The runtime can generate interpolated frames in between for a smoother
feel on high-refresh displays.

> [!WARNING]
> Interpolation is experimental and will show artifacts in specific scenarios.

**A 30 FPS mode for phones that can't hold 60.** With **30 FPS races** turned
on, every race uses the game's own 30 FPS mode - the one it uses for three- and
four-player split-screen - so it plays at normal speed while drawing half as
many frames, instead of running in slow motion. It works in the original game
and in Retro Rewind, offline and online. Menus stay at 60, and Retro Rewind's
time trials and ghost races stay at 60 because the mod requires it there.

**It updates itself.** When a newer release is out, an **Update available**
button appears on the launcher screen. DriftDroid downloads the new APK, checks
it against the release's checksum and against its own signing key, and hands it
to Android, which asks you to confirm. Saves and game data stay. (New in 1.3.0,
so it first gets used by the release after it.)

**It behaves like a phone app.** The game pauses when you background it instead
of fast-forwarding through everything it missed, it can drop its render
resolution when the device gets hot rather than sliding into a slideshow, and it
mutes its own music while you play something else - without stealing audio
focus from your music app.

**An in-game settings sidebar**, reachable from the gear button or the Back
button:
- Internal resolution, display mode, and FPS counter
- Frame interpolation, copy filter, and shader-compilation behavior
- 30 FPS races, and simpler computer karts for lighter races
- Thermal resolution scaling and experimental multithreaded graphics
- Touch control visibility and double-tap auto-hold
- Controller assignment and full per-button mapping, with presets
- Volume sliders and instant mute

Everything you change is saved to `Config.toml` on the device and restored on
the next launch.

## Performance

This is native ARM64 code, not emulation, but Mario Kart Wii still asks a lot of
a phone, and the honest summary is:

- A cold phone holds 60 fps on the devices this has been tested on. A hot one
  does not - sustained play throttles the SoC, and the port is CPU-bound, so
  lowering the resolution only helps so much.
- **Thermal resolution scaling** (on by default) trades pixels for frame rate
  automatically as the device heats up.
- **30 FPS races** (off by default) is the setting for a phone that can't hold
  60: the race stays at normal speed and the phone has half as many frames to
  draw.
- **Simpler computer karts** (off by default) gives computer players the
  lighter kart models the game uses in split-screen.
- The first few minutes after installing are the worst, because shaders are
  still compiling. It settles down. After an update the shaders are rebuilt in
  the background on a single low-priority thread, so the phone stays cool while
  it happens.
- The display is held at the game's frame rate, so a 120 Hz phone does not
  redraw twice per game frame.
- Release builds can be compiled with a recorded profile of real play
  (profile-guided optimization), which cuts the work per frame. See
  [`docs/android-performance.md`](docs/android-performance.md) for how to
  record one and how to measure a release build.
- Debug builds are much slower than release builds, so don't judge the port by
  one.

## Requirements

- **Android 10 (API 29) or newer**, on a 64-bit ARM (`arm64-v8a`) chip. There is no 32-bit or
  x86 build.
- A working Vulkan driver for the best experience. Devices without one can fall back to OpenGL ES
  from the launcher's **Graphics...** screen.
- Enough free storage for the extracted game data (a few GB) plus headroom during the one-time
  extraction step.
- A clean, unmodified **PAL `RMCP01`** disc image of Mario Kart Wii, dumped by you. Only the clean
  PAL revision is supported; other regions or modified executables are rejected.

> [!NOTE]
> Nobody here will tell you where to get the game. Dumping your own disc is on you, and links to
> game files won't be provided or tolerated.

## Installing

Grab the latest APK from this repo's [Releases](https://github.com/dorPXP/driftdroid/releases)
page and sideload it - it isn't on the Play Store. On first launch the app asks you to pick your
Mario Kart Wii disc image, checks it really is a clean PAL disc, and extracts it on the device.
That's the whole setup.

To update, install the new APK over the old one - saves, the extracted game and Retro Rewind all
stay. From 1.3.0 on the app can do this itself: tap **Update available** on the launcher screen
when it appears.

> [!CAUTION]
> Only take builds from this repository's own Releases page. Don't install an APK someone shared
> through Discord or a random download site.

## Building from source

Owning the game is still required even if you compile everything yourself. You'll need:

- Android Studio / the Android SDK and NDK (this project targets `arm64-v8a`)
- CMake and Ninja
- A JDK compatible with the Gradle version in `android/`

Build the native runtime and the app:

```bash
# native runtime (produces libWiiCompiled.so / libGameCombined.so)
cmake --build <your-android-build-dir>

# copy the built .so files into android/app/src/main/jniLibs/arm64-v8a/, then:
cd android && ./gradlew assembleRelease
```

Build `assembleRelease` for anything you intend to measure. Debug builds enable Android's runtime
checks and are substantially slower.

Launch both the original game and Retro Rewind once before calling a build good - they use
different libraries. Profile-guided builds, measuring and the build traps that have bitten this
project are written up in [`docs/android-performance.md`](docs/android-performance.md).

This project also still carries the original desktop build (Windows, via the .NET translator and
Launcher under `translator/` and `Launcher/`) from upstream WiiCompiled - see
[`translator/README.md`](translator/README.md) if you're interested in that side instead.

## A note on related projects

WiiCompiled, KartPad, Wheel Wizard, Retro Rewind, and other related projects are developed
**independently** and each has its own contribution rules. What applies here does not
automatically apply there, and vice versa.

## Retro Rewind

[Retro Rewind](https://wiki.tockdom.com/wiki/Retro_Rewind) is a separate, optional community mod -
nothing in this repo or any release bundles it, the same way nothing here includes the base game.
The app's first screen lets you choose Original Mario Kart Wii or Retro Rewind; picking Retro
Rewind before it's installed walks you through getting it set up, three ways:

1. **Download it** - the app asks the mod's own official update service
   (`update.rwfc.net`, the same one Wheel Wizard and the Retro Rewind updater use) which release
   is newest, and fetches it straight from there: the newest full download, then any small
   updates released on top of it. Nothing is mirrored or re-hosted here, and the game code in
   every download is checked against this build before it's installed.
2. **Select the `.zip` file** you already downloaded - the app extracts it for you, no separate
   unzip step needed.
3. **Select a folder**, if you've already extracted it yourself. Use this one if you're bringing
   an install over from Dolphin with your own mods in it - your files are copied as they are.

Either way the pack lands in place (~2GB, so it takes a few minutes) and you're done: no manual
file management, no computer required. Downloads resume where they left off if your connection
drops, and the copy keeps running while you're in another app.

### Retro Rewind versions

Retro Rewind releases more often than this app does, so the app checks what's actually out there
rather than knowing about one hardcoded release, and **"Check for updates"** in the Retro Rewind
menu applies the mod's own small per-version update archives - a few megabytes instead of
re-downloading the whole pack.

There is one real limit worth understanding. Retro Rewind's *game logic* lives in
`Binaries/Code.pul`, and because DriftDroid compiles everything to native code ahead of time
rather than emulating a Wii, that file has to be compiled into the app itself - it can't be picked
up on your phone at runtime. So:

- A Retro Rewind update that changes **tracks, characters or other assets** works immediately, with
  no app update at all.
- A Retro Rewind update that changes **game code** needs a new DriftDroid build. The app detects
  this before downloading anything large, tells you, and leaves your working install alone rather
  than replacing it with something that would crash. When that new DriftDroid build is out, the
  **Update available** button on the launcher screen gets it.
- After DriftDroid itself has been updated for a newer Retro Rewind, an install still on the old
  one is brought up to date when you tap Retro Rewind, before the game starts.

Mods that replace Retro Rewind's own menu or race files are tied to the Retro Rewind version they
were made from. One made for 6.12.8 can crash on 6.13 (for example when opening Retro WFC) until
it is rebuilt.

This is also why importing a Retro Rewind copy from somewhere else can be refused: it's checked
against the code this build was compiled for. Character/track mods in an otherwise-supported
install are fine - those are assets, and they're preserved.

Both the Riivolution-style file-overlay system this depends on, and a network stack built
specifically to talk to the community Retro-WFC service, are shared with upstream WiiCompiled's
runtime and already wired up on Android.

## FAQ

**Is this an emulator?**
No. Everything is compiled to native code before you ever press play. At runtime there's nothing
emulating a Wii CPU or GPU.

**Do you provide the game?**
No. Don't ask. Nothing in this repo or any release contains Nintendo code or assets.

**Which game version works?**
Clean PAL `RMCP01`. Other regions and modified executables are rejected outright.

**Can I use the newest Retro Rewind version?**
If it only changed assets, yes - the app picks it up on its own. If it changed game code, it needs
a DriftDroid update first; the app will say so instead of installing something broken. See
[Retro Rewind versions](#retro-rewind-versions).

**How do I update DriftDroid?**
Install the new APK over the old one, or tap **Update available** on the launcher screen when it
shows up. Nothing is lost either way.

**Is there gonna be a Switch version?**
There is one: [DriftDroid for Nintendo Switch - Beta 1](https://github.com/dorPXP/driftdroid/releases/tag/switch-beta1)
(a pre-release, original game only). Retro Rewind on Switch is being worked on.

**Is it done?**
No - this is an active, personal work-in-progress fork. It's playable start to finish, but
performance work, device compatibility and bug fixing are ongoing.

**It won't start / the screen stays black.**
Most often a Vulkan driver the device can't actually deliver on. Open the launcher, tap
**Graphics...**, choose **OpenGL ES**, and try again. Report it either way - which renderer worked
is exactly the information that's useful.

**It runs at 60 fps and then gets slower.**
That's thermal throttling, not a bug in the build. See [Performance](#performance). Leaving
"Lower resolution when the phone gets hot" enabled keeps the frame rate steadier as the device
heats up.

**It never reaches 60, and everything looks like slow motion.**
Turn on **30 FPS races** in the settings sidebar. The race then runs at normal speed with half
the frames. It applies from the next race.

**Retro Rewind crashes when I open Retro WFC or a menu after updating.**
Most likely a mod that replaces Retro Rewind's own files and was made for an older Retro Rewind.
Turn your mods off in **Mods...** and try again; if that fixes it, the mod needs a version made
for the Retro Rewind you have now.

**Characters or menu icons are stretched across the screen.**
A driver bug on Snapdragon 8 Elite class GPUs (Adreno 8xx). The fix turns itself on for those
GPUs; if you still see it, set **Fix stretched graphics (Snapdragon)** in the settings sidebar to
**Everything**, or try a different GPU driver (next question).

**Can I use a different GPU driver?**
On Snapdragon phones, yes: **Graphics...** -> **GPU driver...** installs an AdrenoTools driver
package such as Turnip. DriftDroid does not ship drivers; where to get them and how to check which
one is active is in [`docs/android-gpu-compatibility.md`](docs/android-gpu-compatibility.md).

**I picked Vulkan but it says OpenGL ES.**
The game shows a notice at start when the renderer you chose could not start and another one was
used. Please report the device and GPU - that is exactly the list that needs fixing.

**Some characters or objects are invisible.**
A few Adreno GPUs miscompile one of the shaders. Enable **Fix missing characters (some Adreno
GPUs)** in the settings sidebar. It turns itself on for the Adreno 750 (Galaxy S24 class).

**The game crashed / stopped with an error.**
Open an issue with the crash template. If you're on a debug build you can pull the run log with:
```
adb shell run-as com.driftdroid.android cat files/WiiCompiled/android_runtime_attempt.log
```
(Release builds aren't debuggable, so `run-as` won't work on them.)

**How do I open the settings if I hid the gear button?**
Press the phone's Back button.

**Why doesn't my Switch Pro Controller rumble?**
Confirmed at the kernel level (`adb shell getevent -pl`): stock Android's Bluetooth HID gamepad
driver doesn't expose any force-feedback capability for this controller at all - Nintendo's
rumble motor uses its own proprietary HID protocol, not the generic one Android supports. This
isn't something the app can fix from user space; it would need a from-scratch low-level HID
output-report driver specifically for Nintendo controllers. Controllers with real Android-exposed
vibrator support (most Xbox/PlayStation-style pads) should rumble correctly.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Short version: changes to the Android app, touch controls,
launcher and phone-specific rendering belong here; changes to the recompiler itself or to game
behavior belong upstream in WiiCompiled. Anything touching input, rendering or audio needs a test
on a real device.

## AI usage
AI coding tools were used extensively during development of this fork, including for debugging,
implementation, and this README. Physics accuracy inherited from upstream WiiCompiled is proven
synced across Wii, Dolphin, and WiiCompiled via matching ghost ​replays (input-based, not
position-tracked).

## Credits

- **[patchzyy/WiiCompiled](https://github.com/patchzyy/Wiicompiled)** - the static recompilation
  project this Android port is built on top of.
- **[aurora](https://github.com/encounter/aurora)** - the GX rendering/windowing backend this
  project's whole graphics layer sits on. MIT licensed.
- **[Dawn](https://dawn.googlesource.com/dawn)** - Google's WebGPU implementation, powering
  aurora's Vulkan and OpenGL ES backends on Android.
- **[Dolphin Emulator](https://github.com/dolphin-emu/dolphin)** - an invaluable reference for Wii
  hardware behavior, plus the source of the free DSP coefficient ROM and the unmodified default
  WiiConnect24 bootstrap tree bundled with the runtime.
- **[Retro Rewind](https://wiki.tockdom.com/wiki/Retro_Rewind)** by ZPL and team - the mod
  distribution the underlying runtime supports.
- **[KartPad](https://github.com/chrissotraidis/kartpad)** by Chris Sotraidis - the iOS/Android
  sibling port this fork's settings sidebar layout, touch control styling, and motion (tilt)
  steering calibration curve were directly modeled on, and the source of several GX command
  processor fixes and the in-app updater this fork carries.
- Everyone in the static recompilation community.

Bundled third-party components and their licenses live in
[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).

## License

WiiCompiled is free software: you can redistribute it and/or modify it under the terms of the
[GNU General Public License, version 3](LICENSE) as published by the Free Software Foundation.

WiiCompiled is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

Any mkwii distribution making use of WiiCompiled must be licensed under GPL v3.0.

Not affiliated with, endorsed by, or associated with Nintendo. Mario Kart Wii is a trademark of
Nintendo. No Nintendo intellectual property is contained in, distributed with, or obtainable
through this project.
