# Super Mario Eclipse on AYN Thor (private local integration)

Owner-authorized Eclipse Team v1.1.0 expansion, alongside regular Sunshine.
This is the existing native ARM64 port's Eclipse implementation, not Dolphin.
No publication is authorized; game images, extracted art, native libraries,
APKs and save backups stay in ignored `build/` or `reference/`.

- Separate app: `com.wither.sms.eclipse.thor`.
- Explicit entry: `com.wither.sms.ThorLaunchActivity`, action
  `dev.thor.launcher.action.PLAY_GAME`, protocol 1, game `eclipse`.
- Storage: `/sdcard/Android/data/com.wither.sms.eclipse.thor/files`.
- Disc: `disc/game.iso` (verified GMSE04), settings/cache: `game/`, card:
  `saves/card-a`, original companion art: `ThorHud/`.
- Return package accepts only WiiDS or the prior Thor library package.
- Existing integrated and standalone Sunshine profiles are backed up and
  compared with `Verify-Isolation.py`; no save migration or shared paths.

The sixth case uses the Eclipse designer's cover/disc. Its two art entries are
merged into the device's existing custom manifest; all ten prior artwork images
and prior manifest entries are preserved. Case animation/geometry is unchanged.
Claude's existing Sunshine companion layout, counters and FLUDD are retained;
Eclipse-only menu/custom-area adapters avoid incorrect retail stage artwork and
episode slots. See `COMPANION.md` and native source/build details in `NATIVE_BUILD.md`.

## Preparation/build helpers

1. `Prepare-Eclipse.py`: resumable official mod download, verified owner-image
   VCDIFF patching, exact GMSE04 output hash, original input rehashed unchanged.
2. `Prepare-App.py`: isolated host derived from current Thor host; never replaces
   the original checkout.
3. `Build-Native.ps1`, then `Package-App.ps1`: native Eclipse plus compatible host.
4. `Stage-Eclipse.py --serial SERIAL`: verified disc/art staging to Eclipse only.
5. `Stage-Launcher.py --serial SERIAL`: two additional case images, manifest last.

Private receipts: `build/eclipse-thor/final-verification.json`, the native build,
mod provenance, art-staging and isolation receipts in the same folder. Both signed
non-debuggable release APKs are installed and their device SHA-256 values match.
The prior eleven WiiDS native libraries remain byte-identical. Original standalone
Sunshine's APK and both Sunshine profiles (saves, settings, controller mappings)
match their backups.

Bounded final release checks covered actual case launch, the tutorial/dialogue,
physical Odin controls, pause/Exit Area, Eclipse title and file selection, new
slot A, Peach's Castle opening, normal Save completion and fresh-process reload
of the same slot back into Peach's Castle. Main card data/stat hashes matched
before and after reload. Android Back returned to WiiDS; the final foreground is
the library with Eclipse selected. A zero-Shine starter save is retained in
Eclipse only. No fixture, replacement progress or save-state load was used.

The initial hardware lower drawing path produced startup ANRs. Eclipse's lower
Board now rasterizes the same artwork in software; observed Canvas=false,
window=true. A diagnostic comparison measured initial sky/card preparation at
58.96/5.35 ms and the final release had no matched scoped fatal/JNI/ANR messages.
The exact GPU/driver failure is not established. See `LOWER_RENDERER.md`.
The debug-only startup watchdog was removed before the final release build.
Broader stage-by-stage testing, long-session stability, audible audio/haptics and
sustained 60/120 FPS performance are not claimed. Video defaults are 1080 rendered
lines, 16:9 and 60 FPS target; the existing 120 option remains a setting.

Upstream optional save states have a position-only fallback in this toolchain;
normal game memory-card saves are a separate service. Save-state use is not tested.

Sources: [Android host](https://github.com/chasem-dev/sms-android),
[native port](https://github.com/chasem-dev/sms-pc-port),
[Eclipse Team mod](https://gamebanana.com/mods/536309),
[Eclipse source](https://github.com/JoshuaMKW/Super-Mario-Eclipse).
