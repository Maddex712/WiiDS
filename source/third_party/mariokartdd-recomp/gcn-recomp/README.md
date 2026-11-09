# gcn-recomp

A toolkit for static recompilation of Nintendo GameCube games to native code: a Gekko
PowerPC → C recompiler, and a runtime that stands in for the GameCube hardware (CPU helpers,
OS context switching, graphics through an OpenGL GX pipeline, audio, DVD, controllers,
memory card) with an SDL desktop frontend.

It is the common layer under per-game projects, each of which supplies its own game's
layout and steering files and nothing else:

- [prime-recomp](https://github.com/ethannicholas/prime-recomp) — *Metroid Prime*
- [prime2-recomp](https://github.com/ethannicholas/prime2-recomp) — *Metroid Prime 2: Echoes*
- [bluestorm-recomp](https://github.com/ethannicholas/bluestorm-recomp) — *Wave Race: Blue
  Storm*, where this code was first written

**This repository contains no game code or data.** A game project builds from the user's
own disc image; the recompiled C lands in that project's build directory and nowhere else.

## Using it from a game project

Add this repository as a submodule and call `gcn_add_game()`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(prime C CXX)
add_subdirectory(gcn-recomp)
gcn_add_game(
  NAME prime
  TITLE "Metroid Prime"
  LABEL "Prime Recomp"
  DISC_ID GM8E01
  DOL_SHA1 39a2f928159ad46491e9b1ef0a72613d91e0cc40
  SYMBOLS ${CMAKE_SOURCE_DIR}/analysis/symbols.txt
  TABLES ${CMAKE_SOURCE_DIR}/recomp
  FPS 60)
```

That defines the game executable, a headless benchmark (`<name>_bench`) and a compile-only
`runtime_check` target. With `ANDROID_PACKAGE <id>`, an Android build (the NDK's toolchain
file, `-DGCN_BENCH_ONLY=ON`) also builds the headset app, `lib<name>.so`, which
`tools/package-apk.ps1` turns into an APK, and `<name>_egl`, the renderer headless on a
device. A game tells those what it knows -- when to show stereo, how big its world is --
through `vr::GameHooks` (`runtime/vr_game.h`) in its `SOURCES`.

TITLE is the game's name, used in messages. LABEL, optional, is what the window and the
headset app are called, so the app can carry a name of the project's own rather than the
game's trademark.

The game project provides:

- `analysis/symbols.txt` — the DOL's function layout in decomp-toolkit's format, either
  from `dtk dol split` (`tools/fetch_dtk.sh` downloads it) or from a decompilation project.
- `recomp/hle.txt` — functions whose body the runtime replaces (`OSSaveContext`,
  `OSLoadContext`, `OSReport`, `OSPanic` and `OSResetSystem` at minimum).
- `recomp/special_calls.txt` — the address of `OSSaveContext`, whose call sites become
  resume points.
- `recomp/idle.txt` — the backward branch of the SDK scheduler's idle spin (`SelectThread`
  waiting for a runnable thread), where the runtime skips guest time to the next event.
- `recomp/names.txt`, `recomp/patches.txt` — hand-named functions and instruction patches,
  usually empty.
- `recomp/steps.txt` — the game's per-frame steps, for running it at another frame rate
  (`docs/diagnostics.md`, "Finding a game's per-frame steps"); absent for a game that
  does not try.

## Layout

- `recomp/`: `recomp.py` drives the translation; `ppc.py` turns each instruction into C;
  `dol.py` reads DOLs, `.iso` and `.ciso` images and symbol files; `extract_dol.py` pulls
  and verifies the DOL.
- `runtime/`: the GameCube in software. `recomp.h` is the contract with generated code.
  `cpu.cpp`, `threads.cpp` and `hle_os.cpp` are the CPU and OS; `hw/` the hardware
  registers; `gx/` the graphics pipeline; `main.cpp` the SDL frontend and `bench_main.cpp`
  the benchmark. `input_map.*` binds any joystick or key to the pad from a `controls.txt`
  (wheels, pedals, button boxes), and its `--list-controls` prints what to write for each;
  `ffb.*` is the wheel's force feedback, from the game's `telemetry::Motion` and the pad's
  rumble motor.
- `openxr/`: the OpenXR frontend (the game on a theater panel or in stereo, the headset's
  controllers as the pad). `xr_frontend.cpp` is the core every platform shares;
  `xr_desktop.cpp` puts it on the desktop's GL context for PC VR (`--vr`, Windows).
- `android/`: the standalone headset's shells. `openxr_main.cpp` puts the OpenXR frontend on
  EGL and the NativeActivity, `egl_main.cpp` is the headless harness and `audio_aaudio.cpp`
  the audio device. The `vr.txt` settings
  (`runtime/vr_config.*`) and what a game tells the headset (`runtime/vr_game.h`) are in the
  runtime, so the desktop can check a game's hooks too.
- `tools/package-apk.ps1`: packages the app into an APK without Gradle, and installs it.
- `docs/`: working notes on diagnostics and the renderer.

## Requirements

CMake 3.20+, Ninja, Python 3, a C++20 compiler, SDL2 (via pkg-config; on Windows built from
source by the first configure, into `%LOCALAPPDATA%\gcn-recomp`, and shared by every
project's builds after that). On macOS: `brew install cmake ninja sdl2 python`.
