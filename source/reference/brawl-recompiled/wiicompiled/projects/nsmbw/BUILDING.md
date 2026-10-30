# Building New Super Mario Bros. Wii from source

Building it yourself still needs your own **New Super Mario Bros. Wii (USA, SMNE01, revision 1)**,
extracted with Dolphin's **Extract Entire Disc**. These are the steps and scripts the 0.0.1 release
was built with, on Windows 10 x64.

The PowerShell scripts in [`tools/`](tools) were written for this folder layout. Change the paths at
the top of each script if yours differs:

```
E:\NSMBWPort\
  wiicompiled\        this repository
  game\               the translator's inputs (step 2)
  toolchain\          dotnet\, llvm-mingw\, CMake\, Ninja\ (step 1)
  build-nsmbw\logs\   build logs
```

## 1. Tools

- The same native toolchain as WiiCompiled: .NET 8 SDK, CMake, Ninja and llvm-mingw 20260616.
  [`Launcher/Prepare-PortableTools.ps1`](../../Launcher/Prepare-PortableTools.ps1) downloads the
  pinned versions.
- The pinned third-party sources in `Dependencies/`:
  [`Launcher/Prepare-Dependencies.ps1`](../../Launcher/Prepare-Dependencies.ps1). Android also needs
  `Dependencies/dawn_prebuilt_android` (Dawn `v20260603.191052`, `dawn-android-aarch64`).
- Python 3 for the scripts in `tools/` (standard library only).
- Android only: Android SDK (platform 34, build-tools 34), NDK 29.0.14206865, JDK 17, Gradle 8.7.

## 2. The game's code

```
python projects/nsmbw/tools/unpack_game.py <your extracted game folder>
python projects/nsmbw/tools/prelink_rels.py
```

`unpack_game.py` checks the disc (SMNE01 revision 1, clean `main.dol`), copies `sys/main.dol` and
unpacks the four boot RELs (`files/rels/*.rel.LZ`) into `..\game`. `prelink_rels.py` links the RELs
offline the way the game links them at boot, into the one `nsmbw_linked.rel` the translator reads
([`recomp.yml`](recomp.yml)).

## 3. Translate

```
dotnet build translator/Translator.sln -c Release
```

Then the four translator steps, as [`tools/rebuild-all.ps1`](tools/rebuild-all.ps1) runs them:
`translate-recursive`, `emit-base-manifest`, `generate-data-init` and `emit-build-shards`. The output
goes to `generated/` (ignored by git: it is the game's code).

## 4. Windows

```
projects/nsmbw/tools/configure-native.ps1
projects/nsmbw/tools/build-native.ps1
```

The game is `native-build/WiiCompiled.exe`, next to the DLLs and data files it needs. Name your
extracted game folder `game` and put it next to the exe, or let the exe ask for it.

## 5. Android

```
projects/nsmbw/tools/configure-android.ps1
projects/nsmbw/tools/build-android.ps1
projects/nsmbw/tools/stage-android.ps1 -NativeDirectory <this repository>\android\native
gradle -p android assembleRelease
```

`build-android.ps1` makes `android-build/libmain.so`; `stage-android.ps1` strips it and copies it,
with the runtime's data files, into the app (`android/native`, ignored by git). The release APK is
signed with the build machine's Android debug key (see `android/app/build.gradle`).
`deploy-android.ps1` builds, stages and packages a debug APK and installs it on a connected phone.

## Other tools

- `build_function_map.py` made [`MAP.txt`](MAP.txt), the function map the translator follows.
- `ppcdis.py`, `symhash.py`, `nsmbw_versions.py`, `hook_size_check.py`, `verify_runtime_addrs.py`:
  the reverse-engineering helpers used to move WiiCompiled's Mario Kart Wii hooks to NSMBW.
- `capture-window.ps1`, `keyboard-run.ps1`, `capture-session.ps1`, `run-android.ps1`,
  `android-ui.ps1`, `gfx-test-android.ps1`: test drivers for the Windows and Android builds.
