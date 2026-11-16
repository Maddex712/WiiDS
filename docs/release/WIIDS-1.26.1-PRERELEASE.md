# WiiDS 1.26.1 Prerelease — easier Project+ imports

Project+ setup now uses Android's file picker. Choose your Project+ Dolphin ZIP, or select the extracted `sd.raw` and `RSBE01.gct` files. WiiDS copies them into its separate Project+ profile and enables its case; you no longer need to create folders or manually copy files into `Android/data`.

## Install and set up Project+

1. Download **WiiDS-1.26.1-arm64.apk** and install it over your existing multi-game WiiDS app. Do not uninstall or clear WiiDS. Close any running game normally before updating.
2. Import your own supported **USA Brawl `RSBE01` revision 2** disc through **Options > Your games > Brawl** first.
3. Open **Options > Your games > Project+ > Import Project+**.
4. Choose **Project+ ZIP**, or select **sd.raw** and **RSBE01.gct** separately, then press **Import selected files**. Use both files from the same Project+ download.
5. Wait for setup to finish, return to the library, and open the Project+ case in the Brawl stack.

Extract `.7z` or `.rar` downloads before choosing the two files. ZIP setup needs room for a local ZIP copy and the extracted files; choosing the extracted files uses less temporary space. Project+ files and the Brawl disc are not bundled.

## What changed

- ZIP setup locates the two required files inside nested folders and checks their sizes, CRC checksums and file structure. Ambiguous ZIPs ask you to extract and select the correct files.
- Copying and extraction show progress and support cancellation. Setup continues through screen recreation, and conflicting game/save/import/storage/update operations are blocked while it runs.
- Replacing an existing setup requires confirmation and retains the old writable SD image and codes under the Project+ profile's `ImportBackups` folder. Brawl's disc, both games' NAND save folders and settings remain outside the replacement targets.
- Interrupted replacements can roll back, incomplete staging is cleaned on startup, and unexpected post-interruption edits are preserved for review. Existing WiiDS Project+ setups remain supported.

## Preview status

This includes the integrated Pokemon XD build from the preceding **1.26 prerelease**. Game runtime binaries are unchanged: all **40 native libraries and 663 content assets** match that build. This release does not contain a fix for the newly reported XD endless-menu-scroll or frame-hitching issues; those remain under investigation. Standalone XD saves are not automatically migrated.

Release assembly/vital lint, **413 Java importer/recovery assertions**, **16 Android framework host tests** and independent source review passed. These tests use synthetic files; importing a real downloaded Project+ pack and gameplay after import still need Thor acceptance. File structure checks do not guarantee every Project+ version is compatible. Audio and the known XD P-star-DA mapping item retain the previous preview's limits.

Android version: **1.26.1**, build **1208**, package `io.github.maddex712.wiids`, owner release signature, ARM64, Android 11+. This is a **prerelease**; stable WiiDS **1.2.5** and its normal updater channel remain unchanged.

The matching source ZIP contains the committed sources, presentation inputs and synthetic importer tests. Runtime binaries, game discs, user saves and signing keys are excluded. A clean-machine native rebuild is not claimed.
