# Sunshine native Thor adaptation

Requested: native Super Mario Sunshine on AYN Thor, opened from the existing
Mario Kart/Retro Rewind/NSMB carousel. Keep its design and existing games intact.

This uses the chasem-dev ARM64 Android source port, descended from the linked
doldecomp/sms decompilation. It compiles the game C/C++ for ARM64 and implements
the console's platform services. It does not launch Dolphin or translate PowerPC
instructions at runtime. Source pins, alternatives and input hashes are in
[MODDING_PLAN.md](MODDING_PLAN.md) and [RUNTIME_RESEARCH.md](RUNTIME_RESEARCH.md).

Separate app: `com.wither.sms.thor`. Existing Java/JNI implementation namespace
remains upstream's; the shared library uses the validated entry
`com.wither.sms.ThorLaunchActivity`, protocol1/game`sunshine`.
Read [LAUNCHER_INTEGRATION.md](LAUNCHER_INTEGRATION.md) for the complete contract.

The owner's RVZ is reconstructed/verified locally as the supported GMSE01 rev0
disc. No game data is downloaded or included here. All disc images, extracted
assets, compiled game libraries, APKs, saves and private receipts stay ignored
under build/ or reference/. Do not publish this private workbench.

## Local preparation

```powershell
python experiments/sunshine-thor/Prepare-Sunshine.py "C:\path\Sunshine.rvz" --extract
```

This uses the local pinned dtk tool and verifies the full reconstructed disc,
then converts to the raw ISO expected by the native port. An existing output ISO
is accepted only if its full SHA1 matches the supported input.

Read [ANDROID_BUILD.md](ANDROID_BUILD.md) for the build wrapper and the deliberate
local SDK/NDK/Gradle adaptations. Build completion and live play are separate.
The launcher builds preserve every native library from the previously installed
three-case APK; only its frontend is changed to append Sunshine.

After installing the separate Sunshine APK, stage its owner disc without opening
an Activity or changing another game's data:

```powershell
python experiments/sunshine-thor/Stage-Thor.py --serial DEVICE_SERIAL
```

Storage: Android/data/com.wither.sms.thor/files, with disc/game.iso, game/settings.txt
and saves/card-a. Original30FPS,1x resolution,4:3 defaults keep initial checks
conservative; physical controls are the game interface. Actual boot/input/save/
performance coverage must be established by private device evidence before claims.
