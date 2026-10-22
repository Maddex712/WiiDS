# Super Mario Galaxy in WiiDS 1.0

Galaxy 1 runs **inside the single WiiDS APK**, in its own processes and profile.
No helper, PC converter or pre-cooked download is needed for a fresh setup.
Galaxy 2 is not supported. Galaxy needs Android 12L/API 32+, ARM64, Vulkan and
the AYN Thor's two displays. Other entries retain their existing requirements.

## Fresh setup

1. Install `WiiDS-1.0-arm64.apk` from the [v1.0.0 release](https://github.com/Maddex712/WiiDS/releases/tag/v1.0.0).
   Update WiiDS in place; do not uninstall or clear data.
2. Copy your own clean **USA RMGE01 revision 0** disc onto the device. ISO is
   the tested input. NKit and patched discs are unsupported setup inputs.
3. Open WiiDS > Options > Your games > Super Mario Galaxy, choose the disc and
   **Set up Galaxy**. Allow storage access and keep setup open until it finishes.
4. WiiDS extracts/checks the disc, cooks it, then generates the HUD, fonts,
   Gardens map and textured planets. Readiness is published after installation.
5. Return to the Galaxy case. Back opens the menu: Quick Save, Controls > Buttons
   and Return to WiiDS. Drag/pinch the lower planet to orbit/zoom; double-tap resets it.

With the disc already copied, setup needs about 7.55 GB free with movies or
2.73 GB with movies omitted. The disc and APK also consume space. The low-space
ISO/WBFS path skips movies during extraction. RVZ relies on the bundled nodtool
extractor and requires the full-space path; RVZ has not been exercised in these
desktop checks. A clean ISO is recommended. Conversion/baking can take several
minutes, depending on storage and CPU speed.

Re-import replaces only cooked `files/`, `sys/` and generated `ThorHud/` using a
recovery journal. Saves/settings/NAND stay separate. A cross-process file lock
prevents replacement while Galaxy is running. Retry interrupted setup; keep your
original disc and saves instead of clearing app data.

## Existing standalone-helper profiles

The old helper stays installed and untouched. Android isolates its private data,
so an ordinary WiiDS update cannot silently read its NAND. Existing users can
import a **copy** before importing a disc into the new WiiDS Galaxy profile:

1. Copy the old helper's external `files/` and internal `files/nand/` using your
   backup tools. Its debuggable package can be exported with `adb pull` and
   `adb exec-out run-as io.github.maddex712.wiids.galaxy tar`. Close Galaxy first;
   never uninstall or clear it to migrate.
2. From the named source ZIP, run:

```powershell
python -I experiments/galaxy-thor/Export-LegacyProfile.py --external C:/GalaxyBackup/files --nand C:/GalaxyBackup/nand --out C:/GalaxyBackup/Galaxy-profile.zip
```

3. Copy that private ZIP to shared storage and choose **Import legacy Galaxy
   backup** on WiiDS's Galaxy card. It verifies every SHA-256 and copies the
   profile transactionally. It refuses to overwrite an existing WiiDS Galaxy
   profile. Keep the helper and backup until a normal save/reload has passed.

This ZIP contains game/save data: do not publish it. Fresh users never need it
or the helper. The integrated roots are WiiDS internal `files/galaxy/nand` and
external `files/galaxy/GalaxyData`, with generated `ThorHud` inside it. No global
GalaxyData folder or another game's save directory is used.

## Validation and limits

WiiDS remains non-debuggable and owner-signed. Cached Galaxy memory and its
library slot are relocated below 4 GB; uncached mirrors use separate free ranges.
The loader acquires regions with `MAP_FIXED_NOREPLACE` and replaces pages only
inside its own verified reservation. No wrap.sh or second package is required.
Colliding layouts fail instead of overwriting Android memory.

Desktop/Android checks cover actual-source memory/loading in fresh non-debuggable
x86 processes, Python/native dependencies and lower baking, and transaction and
migration failures. The ARM64 package is compiled, signed and audited. The new
integrated build was **not tested on a physical Thor**. Earlier Thor captures
used the separate-helper development build; see the inventory for exact scope.

The planet is a simplified diorama driven by matched actor visibility/matrices.
Enemies, NPC/player models and skeletal/joint animation are omitted; the player
uses a marker. Some materials stay dark and some pieces are unmatched. Only the
Gardens has a flat map. Observatory ledger/star chart and CapsuleCage glass remain
unverified. No all-stage, sustained guest FPS, audible-audio, haptic or long-session
guarantee is made. A 60 Hz lower draw cap is not a guest-performance measurement.

## Source/rebuild

Use `WiiDS-1.0-source.zip` for current source and the exact curated presentation.
Galaxy-Android is pinned to `1abcb975927ecb4189949bd51f81e73a04d97566`, with local
changes and CC0 Petari sources included. Restore upstream dependencies, overlay
the snapshot, then use Build-Native.ps1, Build-Importer.py, Stage-Integrated.py
and Package-PublicRelease.ps1. Galaxy uses NDK 29, ARM64/API 32, CMake/Ninja and
JDK 21. Android Python wheels use pinned SHA-256 values in python-packages.json.

Credits: OpenPort-Labs Galaxy-Android (GPL-3.0), SMGCommunity Petari (CC0),
bigmak94/GalaxyQuest, CPython, NumPy, Pillow, FreeType and included dependencies.
This software is based in part on the work of the FreeType Team. No independent
clean-machine rebuild is claimed.
