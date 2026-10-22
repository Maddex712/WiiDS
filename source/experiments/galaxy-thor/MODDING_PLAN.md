# Super Mario Galaxy on AYN Thor

Status: owner ISO verified, native build and WiiDS integration installed locally.
See README and latest MODLOG for live validation and remaining limits; the
initial reconnaissance below records the starting constraints.

- Target: native ARM64 Android on AYN Thor, with physical controls. Establish
  a standalone boot before integrating a WiiDS case or lower-screen companion.
- Starting runtime: [OpenPort-Labs/Galaxy-Android](https://github.com/OpenPort-Labs/Galaxy-Android),
  pinned to `1abcb975927ecb4189949bd51f81e73a04d97566`, GPL-3.0.
  The port builds reconstructed game C/C++ and a Vulkan/AAudio platform layer.
  Petari and GalaxyQuest are its upstream foundations; preserve their credits.
- Checkout: ignored `reference/galaxy-android`. Outputs: ignored
  `build/galaxy-thor`. Do not commit extracted game data or build artifacts.
- Host toolchain: Windows, CMake/Ninja, NDK `29.0.14206865`, ARM64/API 32.
  The CMake configuration builds the NDK's bundled shaderc when absent.
- Device observed 2026-10-21: AYN Thor `kalama`, `arm64-v8a`, connected through
  wireless ADB. WiiDS ModePickerActivity was foreground. Read-only inspection
  only; no installation, game launch, or control input has occurred.
- Disc intake: prefer a clean original Wii `.iso`; upstream also documents RVZ
  and WBFS. Use the owner's dump. Validate its identity, revision and hashes
  before extraction. Do not assume a filename establishes authenticity.
- Upstream runtime recognizes RMGE01, RMGP01, RMGJ01 and RMGK01 language layouts.
  Actual compatibility must be checked against the supplied dump.
- Conversion: upstream `tools/cook/cook.py` consumes extracted `sys` and `files`,
  converting mapped structures to little endian and extracting three data
  tables/archives from the owner's `sys/main.dol`. Retain cutscene movies.
- Upstream profile: package `com.openport.galaxyandroid`, private `files/nand`,
  shared `/sdcard/GalaxyData` data/save mirrors. A Thor variant needs isolated
  data and save roots before first launch, including mirror/export paths.
- Upstream APK uses a debuggable process and `wrap.sh`/`libreserve.so` to reserve
  a low address range before ART. A non-debuggable release requires a proven
  alternative; simply clearing the manifest flag is not a valid fix.
- Input audit: code and upstream documentation disagree for B, R1 and R2.
  The source currently makes analog R2 spin, digital R2 combine B and Z, and B
  combine jump and cancel/shoot. Verify intended Thor mappings explicitly;
  do not call the existing gamepad support validated.
- Lab: inspect and back up any existing Galaxy installation/data before
  writing. Use a separate package/profile for the adaptation and capture both
  screens. Existing WiiDS and other game saves must remain independent.

First vertical slice: build native libraries, prepare validated owner data,
install the isolated app, visually verify title and a controllable opening
scene, then test normal exit and save/reload. Only afterward add a WiiDS case
and design a second-screen view from actual game state. No publication requested.

## Research

- [Galaxy Android installation](https://github.com/OpenPort-Labs/Galaxy-Android/blob/1abcb975927ecb4189949bd51f81e73a04d97566/docs/INSTALLATION.md)
- [Galaxy Android source](https://github.com/OpenPort-Labs/Galaxy-Android/tree/1abcb975927ecb4189949bd51f81e73a04d97566)
- [Petari](https://github.com/SMGCommunity/Petari): reference decompilation,
  not itself an Android build.
- [Nebula](https://github.com/keepvaibin/nebula): alternative static-recompilation
  runtime currently documented for Windows/Direct3D 12; larger Android bring-up.
- [Universal Modder knowledge index](https://github.com/rehan-remade/universal-modder/blob/main/knowledge/INDEX.md):
  inspected; no Galaxy entry found on 2026-10-21.
