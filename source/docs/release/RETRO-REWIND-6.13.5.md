# Rebuilding the Retro Rewind 6.13.5 runtime

WiiDS 1.2.3 updates the ARM64 Retro Rewind library. The base Mario Kart Wii library and the other native libraries retain their 1.2.2 bytes. See the source manifest for the APK and runtime hashes.

1. Obtain the supported PAL Mario Kart Wii `RMCP01` revision 0 disc and extract `main.dol` and `StaticR.rel` into `reference/driftdroid/Assets`. The existing translation script checks both hashes.
2. Download the official 6.13.5 full and delta ZIPs. Run `python scripts/Prepare-RetroRewindRelease.py 6.13.5 <full.zip> <delta.zip> <new-output-folder>`. Compare the resulting pins with [the 6.13.5 input inventory](../../release/retro-rewind-6.13.5-inputs.json).
3. Build the DriftDroid translator and use the generated `RetroRewind6` directory as the pack root. In an existing translation checkout with its generated tree and prior fallback APK, run `scripts/Generate-RetroRewind.ps1 -PackRoot <RetroRewind6> -EnableRetroWfc`. That script preserves a fallback before replacing the generated tree.
4. Configure `reference/driftdroid/runtime` with the Android NDK 27.2.12479018 toolchain and Ninja. The release used `arm64-v8a`, `android-29`, `Release`, `c++_shared`, `WIIDS_RUNTIME_GAME_DATA=ON`, `MKW_ANDROID_CUSTOM_GPU_DRIVERS=ON` and `-Wl,-z,max-page-size=16384`. Use the pinned dependencies for the Android Vulkan runtime. Build the `RetroRewind` target and strip it with the NDK's `llvm-strip --strip-unneeded`.

The source archive includes the changed runtime and Aurora source inputs. Disc data, generated game translations, the official Retro Rewind pack, and user-supplied Lossless Scaling files are obtained separately. The release verification covered the shipped library and selected source inputs, and a clean-machine rebuild was outside that verification.
