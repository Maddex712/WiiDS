# WiiDS release step 1: release basics (2026-10-19)

Done on the PC. Nothing installed on the Thor yet (it was away).

## 1. No game executable inside the APKs
- The translated games used to embed the game's own DOL/REL sections byte for byte
  (`generated/data_sections_init_blobs.S`, `.incbin`). Release builds now rebuild those sections at
  startup from the player's extracted disc and check every one against a pinned SHA-256:
  - `runtime/src/wiids_game_data.cpp` + `runtime/include/wiids_game_data.h` (identical copies in
    `reference/driftdroid`, `reference/nsmbw-android-e1`, `reference/nsmbw-android-msmbwii`).
    Mirrors the build tools exactly: DOL sections, `RelFile.BuildImage` (Mario Kart `StaticR.rel`),
    `unpack_game.py` LZ11 + `prelink_rels.py` (NSMB boot RELs), `prepare_msmbwii.py` (MSMBWii patch,
    read from `files/mod/msmbwiijala/AnotherRes/SystemUS_1.bin`).
  - `generated/data_sections_runtime.cpp` per game, from `experiments/release/gen_game_data_manifest.py`
    (`<repo> mkw|nsmbw|msmbwii`). Names, addresses, sizes and hashes only.
  - CMake option `WIIDS_RUNTIME_GAME_DATA` (default ON) in each `runtime/cmake/PublicProducts.cmake`.
  - A wrong or missing disc shows "This disc can't be used" with the exact disc each game needs.
- Proven on the PC: the C++ rebuilds all 11 sections of Mario Kart Wii, NSMB Wii and MSMBWii
  byte-for-byte from the owner's discs (test harness `experiments/release/game-data-test/`, built
  with MSVC against stubs). A byte-window scan of every shipped game library finds 0 / 100–200
  samples of the executable (it was 100% before).
- **Still to verify on the Thor:** boot each game once with the new libraries.

## 2. Release signing key
- Private key `%USERPROFILE%\.wiids-release\wiids-release.jks` (`CN=WiiDS, O=Maddex712`,
  SHA-256 `72:EE:F0:6B:95:6B:C1:C6:84:8A:09:BF:49:56:B1:58:09:57:36:A6:E8:1F:43:13:1C:F9:3C:B3:07:88:34:5F`),
  password in `keystore.properties` beside it. Never commit either. **Back them up** (losing the key
  means no updates for anyone who installed WiiDS).
- All four apps' release builds sign with it when that file exists, and fall back to the debug key
  otherwise. Existing debug-signed installs on the Thor must be uninstalled once (back up saves first).

## 3. One version
- `release/version.properties` (`0.9.0-beta.1`, code 900), passed by `scripts/Package-Thor.ps1`,
  `experiments/nsmbw-thor/Package-Nsmbw.ps1`, `experiments/sunshine-thor/Package-ThorApk.ps1` and the
  MSMBWii Gradle command (`-PwiidsVersionName/-PwiidsVersionCode`).

## 4. Licences in every APK
- `release/licenses/WIIDS-NOTICE.md` (credits, FreeType credit, no-Nintendo-affiliation, no games
  included) plus each app's GPL/MIT/CC0/zlib texts and third-party notices, staged by
  `experiments/release/stage_licenses.py` into `src/main/assets/licenses/` (NSMB apps now also read
  `src/main/assets`).

## 5. 16 KB pages (Android 15+)
- Mario Kart: `scripts/Configure-Native.ps1` adds `-Wl,-z,max-page-size=16384` (linker only; no
  recompile) and the app's placeholder `libwii.so` CMake links with it. A stale 4 KB `libwii.so` was
  moved out of `src/main/jniLibs` to `build/stale-jnilibs/`. Every library in all four APKs is 16 KB.

## Built (local, not installed, not published)
| APK | Version | Signer |
|---|---|---|
| `build/DriftDroid-Thor.apk` (MKW + Retro Rewind) | 0.9.0-beta.1 (900) | WiiDS |
| `build/nsmbw-thor/e1/NSMBW-Thor.apk` | 0.9.0-beta.1 (900) | WiiDS |
| `build/nsmbw-thor/msmbwii/MSMBWii-Thor.apk` | 0.9.0-beta.1 (900) | WiiDS |
| `build/sunshine-thor/Sunshine-Thor.apk` | 0.9.0-beta.1 (900) | WiiDS |
