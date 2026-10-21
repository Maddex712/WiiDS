# Android: performance, heat and build notes

Notes for people building and measuring the Android port. Nothing here is needed to play.

## Why a phone gets slow

The port is CPU-bound, and the game thread is the limit. Phones do not throttle on CPU
temperature alone: many follow the temperature of the case ("skin"), and start cutting the fast
cores' clock speed at around 35-40 C on the case while the chip itself is still far from its
limit. Once that happens the game thread no longer fits in a frame and frames drop.

So the only real fix is to do less work. Asking the system for more speed does not help, and
testing over a USB cable makes things worse because charging heats the phone.

What has been done so far:

- **Shader replay.** After an update that changes generated shaders, every recorded pipeline is
  recompiled. This used to run on every core. Once the first frame is on screen it now uses one
  low-priority worker, and progress is saved every 1000 pipelines.
- **Display rate.** The surface votes for the game's frame rate
  (`Surface.setFrameRate`, fixed source), so a 120 Hz panel drops to 60 Hz.
- **Audio mixing** runs on a worker thread by default (`mix_worker`).
- **Profile-guided optimization** of the whole runtime, including translated game code.

## Measuring a release build

Release builds are not debuggable, but the app is marked profileable from the shell.

Frame rate, from the compositor:

```bash
LAYER='SurfaceView[com.driftdroid.android/com.driftdroid.android.MainActivity](BLAST)#0'
adb shell dumpsys SurfaceFlinger --latency-clear "$LAYER"
sleep 5
adb shell dumpsys SurfaceFlinger --latency "$LAYER"
```

The first line is the display's refresh period in nanoseconds (16666666 = 60 Hz); each
following line is one presented frame.

Threads (they are named: `SDLThread` is the game thread, then `GxWorker`, `FrameWorker`,
`Presenter`, `ShaderWorker`):

```bash
adb shell top -H -b -n 1 -p $(adb shell pidof com.driftdroid.android)
```

Where the time goes:

```bash
adb shell simpleperf record --app com.driftdroid.android --duration 20 -f 2000 \
    -o /data/local/tmp/race.data
```

Report it with the NDK's simpleperf and `--symfs <your-android-build-dir>`.

Temperature and throttling level: `adb shell dumpsys thermalservice` (read the "Current
temperatures from HAL" section, not the cached one), and
`/sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq` for the clock speeds.

A recorder started with `adb shell` is killed when the cable is unplugged. For an unplugged
session, connect adb over Wi-Fi first and keep the phone awake.

## Profile-guided build

1. Configure a separate build directory with `-DMKW_PGO=generate` and build `WiiCompiled`.
2. Package it as a **debug** APK (reading the profile needs `run-as`) and play a few races.
   `runtime/src/android_pgo.cpp` writes `files/WiiCompiled/pgo/game.profraw` every 20 seconds.
3. Pull it and merge it:
   ```bash
   adb exec-out run-as com.driftdroid.android cat files/WiiCompiled/pgo/game.profraw > game.profraw
   llvm-profdata merge -o android.profdata game.profraw   # llvm-profdata from the NDK
   ```
4. Configure the normal build directory with
   `-DMKW_PGO=use -DMKW_PGO_PROFILE=/path/to/android.profdata` and rebuild.

Things to know:

- Uninstall or replace the recording build when you are done. It keeps writing the profile for
  as long as it runs.
- If the raw profile holds more than one snapshot (the compiler runtime appends), merge only the
  last one, or early play is counted many times over.
- A profile recorded on the original game also speeds up Retro Rewind, since they share most of
  the translated code. Re-record after a change that re-translates game code.

## Build traps

- **Retro Rewind stops at startup with "Stale generated indirect dispatch winner".** The Retro
  Rewind dispatch table is generated from the mod's resolved profile, which only `translate-mod`
  refreshes. After adding a native override (`PPC_NATIVE_OVERRIDE`), re-run `translate-mod`
  **and** `emit-build-shards`; running only the second leaves Retro Rewind's table pointing at
  a function that is now native. On the phone this looks like a crash in graphics shutdown; the
  real message is in `files/WiiCompiled/android_runtime_attempt.log`.
- **Combined library.** After any C++ change, delete `libGameCombined.so` before relinking, and
  run `ninja -t cleandead` if objects were removed from the build, or the link fails on
  duplicate symbols.
- **Custom GPU drivers and PowerVR.** Release builds use Dawn built from source (commit
  13abc3bc, the same one as the prebuilt package) with two patches from
  `aurora-main/cmake/patches/`: `dawn-android-vulkan-library-override.patch` (lets the launcher's
  GPU driver picker load a custom Adreno driver through libadrenotools) and
  `dawn-android-imgtec-interstage-floor.patch` (from KartPad: accepts older PowerVR drivers that
  report 64 inter-stage components). Build it for Android with a host `protoc`, install it, and
  configure with `-DFETCHCONTENT_SOURCE_DIR_DAWN_PREBUILT=<install dir>` and
  `-DDawn_DIR=<install dir>/lib/cmake/Dawn`. With the stock prebuilt Dawn the picker has no effect
  and PowerVR BXM phones on the old driver get no Vulkan adapter.
- **Build paths in release libraries.** A from-source Dawn embeds its source paths in assert
  messages. Replace the home-directory prefix in `jniLibs` with a same-length neutral one before
  packaging a public APK.
