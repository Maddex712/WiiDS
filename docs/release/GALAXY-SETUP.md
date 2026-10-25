# Super Mario Galaxy and Kaizo in WiiDS 1.1

Galaxy 1 runs **inside the single WiiDS APK**, in its own processes and profile.
No helper, PC converter or pre-cooked download is needed for a fresh setup.
Galaxy 2 is not supported. Galaxy needs Android 12L/API 32+, ARM64, Vulkan and
the AYN Thor's two displays. Other entries retain their existing requirements.

## Fresh setup

1. Install `WiiDS-1.1-arm64.apk` from the [v1.1.0 release](https://github.com/Maddex712/WiiDS/releases/tag/v1.1.0).
   Update WiiDS in place; do not uninstall or clear data.
2. Copy your own clean **USA RMGE01 revision 0** disc onto the device. ISO is
   the tested input. NKit and patched discs are unsupported setup inputs.
3. Open WiiDS > Options > Your games > Super Mario Galaxy, choose the disc and
   **Set up Galaxy**. Allow storage access and keep setup open until it finishes.
4. WiiDS extracts/checks the disc, cooks it, then generates the HUD, fonts,
   Gardens map and textured planets. Readiness is published after installation.
5. Return to the Galaxy case. Back opens the menu: Quick Save, Controls > Buttons
   and Return to WiiDS. **Controls > Lower screen** selects Trackpad or Planet.
   In Trackpad mode, slide to move the Star Pointer; lift/reposition without jumping
   the cursor. Touch and right-stick pointing share one cursor; A/B remain separate.
   Planet mode keeps drag/pinch orbit/zoom and double-tap reset.

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

## Kaizo Mario Galaxy Rebalanced 2.1

1. Use the same clean **USA RMGE01 revision 0** Galaxy disc as above.
2. Select **Kaizo Mario Galaxy Rebalanced 2.1** in **Options > Your games**, then
   choose **Set up Rebalanced**.
3. Select the creator's original **Rebalanced 2.1 ZIP** using **Choose ZIP**.
   The dialog includes an **Official download** link if you need the patch.
   WiiDS accepts the exact supported archive; other versions and repacked ZIPs
   are refused. Its SHA-256 is
   b0b43ee39e3b2c2b7a3dcc4bc33d77718cb97da4b95f490146ec8a80ceddd548.
4. Keep setup open while WiiDS applies the patch privately, cooks the game and
   generates its own lower-screen art/models. Wait until the card reports ready.

The creator's patch ZIP is not bundled. Kaizo has its own NAND, saves, settings,
import state and lower-screen data. Original Galaxy progress is not copied into
the mod, and the author's optional Luigi-unlocked save is not installed. Keep
additional room for this separate game profile and its prepared files.

## Controls and Picture settings in 1.1

The lower-screen mode hint appears for five seconds after playable control begins;
it stays hidden after Home/resume. Change modes later through **Controls > Lower
screen**. Right-stick Camera mode uses the game's permitted horizontal turns;
camera elevation stays under game control.

**Display > Picture** offers Original, neutral Smooth and optional Smooth + glow.
Smooth no longer forces bloom or a color boost. The opening-attack palette and
town color transition are corrected; the scene's intended warm rim lighting remains.

For save backups, restore and supported Dolphin imports, use the shared
[Save manager](../SAVE_MANAGER.md). Choose Galaxy or Kaizo explicitly.

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
migration failures. The ARM64 package is compiled, signed and audited. The 1.1 development builds
also passed physical Thor checks of original/Kaizo save loading, relative pointing,
Star Bit collection in Kaizo Gardens, native camera limits, sleep/wake and normal
exit. Clean dev28 verified corrected attack-scene palettes and original Gateway
rendering. The release retains its native libraries and game/menu assets. These
checks cover selected scenes and transitions, not a complete playthrough.

The planet is a simplified diorama driven by matched actor visibility/matrices.
Enemies, NPC/player models and skeletal/joint animation are omitted; the player
uses a marker. Some materials stay dark and some pieces are unmatched. Only the
Gardens has a flat map. Observatory ledger/star chart and CapsuleCage glass remain
unverified. No all-stage, sustained guest FPS, audible-audio, haptic or long-session
guarantee is made. One earlier development build had a selected-file-menu allocation
panic whose cause remains unestablished; it did not recur in later bounded checks.
A 60 Hz lower draw cap is not a guest-performance measurement.

## Source/rebuild

Use `WiiDS-1.1-source.zip` for current source and the exact curated presentation.
Galaxy-Android is pinned to `1abcb975927ecb4189949bd51f81e73a04d97566`, with local
changes and CC0 Petari sources included. Restore upstream dependencies, overlay
the snapshot, then use Build-Native.ps1, Build-Importer.py, Stage-Integrated.py
and Package-PublicRelease.ps1. Galaxy uses NDK 29, ARM64/API 32, CMake/Ninja and
JDK 21. Android Python wheels use pinned SHA-256 values in python-packages.json.

Credits: OpenPort-Labs Galaxy-Android (GPL-3.0), SMGCommunity Petari (CC0),
bigmak94/GalaxyQuest, CPython, NumPy, Pillow, FreeType and included dependencies.
This software is based in part on the work of the FreeType Team. No independent
clean-machine rebuild is claimed.
