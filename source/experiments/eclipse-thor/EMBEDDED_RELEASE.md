# Eclipse in the combined WiiDS release

The combined APK carries the Eclipse runtime and the official Eclipse Team 1.1.0
VCDIFF patch. It carries neither the original Sunshine disc nor the patched Eclipse
disc. The existing standalone `com.wither.sms.eclipse.thor` remains separate and is
not replaced or read by this implementation.

## Installation in WiiDS

1. Open **Your games** and import your own unchanged USA revision 0 Sunshine disc.
2. On the Eclipse card, choose **Install Eclipse**. WiiDS checks the entire source
   disc, applies its bundled patch, verifies the patch and final image, and installs
   the result separately. Keep WiiDS open during installation.
3. Open Eclipse's case in the library and launch its disc.

A raw official `patches/v1.1.0.xdelta` selected through Android's file chooser is
also accepted. An already patched Eclipse 1.1.0 image can instead be imported with
**Import patched image**, including the existing ISO/RVZ conversion support. Other
Eclipse builds with the reused GMSE04 header fail the full output SHA-256 check.

The source disc is only opened read-only. Cancellation or a failed checksum leaves
any existing playable Eclipse image in place and removes only this installer's
unfinished `game.iso.patching` file. The final rename is atomic. At least 1.5 GB
free is needed for Eclipse's separate game image; reinstallation also retains the
old image until the verified replacement is ready.

## Isolation and native routing

- Java host: `dev.chasem.eclipselauncher`.
- Android activities: launcher/config in `:eclipse`, SDL game in `:eclipse_game`.
- Disc: internal `files/games/eclipse/game.iso`.
- Profile: external app files `eclipse/`, containing `game/`, `saves/card-a`,
  `controller-mappings.txt` and the generated `ThorHud/`.
- Preferences: `eclipse_settings`, `eclipse_controller-profiles`, `eclipse_transport`.
- Runtime: `libeclipse.so`, unchanged from the verified standalone Eclipse runtime.
- Bootstrap: `libeclipsebootstrap.so`; its relocated JNI exports poll the same
  read-only snapshot/sky/photo/floor functions and load `libeclipse.so` into the
  existing reserved address range.
- SDL: the combined app's existing relocated `org.libsdl.sdl2.app` / `libSDL2.so`.

Original Sunshine's Java classes, native libraries, storage paths and preferences
are not rewritten. Neither standalone nor integrated Sunshine saves are shared
with Eclipse. Existing standalone Eclipse saves are not automatically imported
across Android application sandboxes; no personal saves ship in the APK.

Claude's companion renderer is retained. Eclipse's existing software rasterization
workaround and custom-area/menu guards remain; its theme is explicitly Eclipse
even though the combined app's package name does not contain that word.

## Build and bounded verification

After the ordinary merged-runtime staging, run:

```powershell
python experiments/eclipse-thor/Stage-Embedded.py --bundle-patch
```

The helper verifies the original patch hash before staging it as
`mods/eclipse-v1.1.0.xdelta`, recompiles only the small bootstrap, and writes
`build/eclipse-thor/embedded/staging.json`. It does not stage disc images, saves
or extracted HUD assets. The existing on-device HUD builder extracts its images
from the player's imported/patched disc.

The Java default-table VCDIFF implementation uses bounded per-window allocations,
random access to source segments, source/address/size checks, and per-window Adler32.
The actual 175-window official patch produced the expected full image hash in a
host JVM with a 256 MB heap; 12 focused malformed-input/overlap checks also passed.
Those results are in `build/eclipse-thor/embedded/decoder-verification.json` and
do not establish Android UI, launch or gameplay verification for the combined build.

Original standalone Eclipse gameplay validation remains scoped to its earlier
installed APK, as described in `README.md`. The release operator records combined
Android installation and launch evidence separately before claiming it verified.
