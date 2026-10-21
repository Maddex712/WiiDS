# Eclipse native ARM64 build

Local isolated native source: `reference/sms-eclipse-thor`, upstream
`chasem-dev/sms-pc-port` commit `bb9fd1d0fbd113cbe37e16a37091238026ff25f0`.
This is the `sms-port` pin of official Android launcher
`chasem-dev/sms-android` commit `da6e917d40e78e33b6572a2442c7a2ccf55d685f`.
Decomp submodule stays at `967436b8c623834d38a57d34923775f45eca8861`.

Build with `Build-Native.ps1` after preparing the source and source-only Thor
patch. Uses NDK 29.0.14206865, API 29, ARM64, SDL 2.32.10, at most eight jobs,
`SMS_ECLIPSE=ON`, and `RelWithDebInfo`. Pinned GPL Eclipse/BSE/moveset/SHI source
repositories are fetched by upstream CMake; no game disc or assets are fetched
by this build. Keep the mandatory `sms_abi_check` enabled. Unstripped binaries
and logs stay privately under `build/eclipse-thor/android-native`; stripped
APK inputs go to `build/eclipse-thor/jniLibs/arm64-v8a`.

## Thor adaptation

The existing Claude Sunshine lower-screen renderer is retained by the app.
Native code carries its read-only 64-word snapshot, scene-path reporting,
per-frame publication, and both `ThorSunshineScreen.nativeSnapshot` and
`nativeSky` JNI entry points. The title's 3D sky capture remains before the
logo's 2D pass. Original episode/held-Shine tables are consulted only for
retail-range stages with a recognizable original scene basename; added stages
retain real scene/counter/FLUDD state and have no invented eight-episode data.
No game mechanics are changed by this adapter.

The current upstream Android patches already include the relocated-MEM1 J3D
array and draw-sync-pointer repairs from the prior Thor work; do not apply the
old patches a second time. Keep upstream's newer library-span-based low-address
reservation rather than copying the old fixed-128-MiB bootstrap wholesale.

Native output names remain `libsms.so`, `libsmsbootstrap.so`, `libSDL2.so`, and
`libc++_shared.so`. JNI still uses `dev.chasem.smslauncher`, independent of the
separate Eclipse Android application ID. No existing Sunshine source, package,
disc, settings, or saves are changed by this isolated build.

Build and device verification outcomes are recorded separately in the private
build receipts. A successful compile/ABI check alone does not prove gameplay.

## Current bounded build outcome

Native build and full ABI check passed on 2026-10-20. The complete check had
669 direct game-call functions, 483 game virtual functions, 213 overrides and
391 patch sites; 155 sites were not compared by the upstream analyzer. The
wrapper exposes NDK llvm-nm on PATH and refuses zero direct-call coverage.
Both lower-screen JNI exports and the native snapshot/sky exports exist in
the linked AArch64 libraries. See `build/eclipse-thor/native-build-receipt.json`.

Upstream's save-state range generator does not parse lld's map format here:
it reports zero ranges, so optional emulator-style save states are position
only. This is separate from normal game memory-card saves and no regular
Sunshine save data was touched. Do not describe full save states as verified.
No device installation, game boot or playtest was performed by this native
build task; those require the app/disc integration and separate evidence.
