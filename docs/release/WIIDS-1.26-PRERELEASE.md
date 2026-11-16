# WiiDS 1.26 Prerelease - Pokemon XD in the main app

Pokemon XD: Gale of Darkness is now part of the main WiiDS app, alongside the existing games.

## Install and import

1. Download **WiiDS-1.26-arm64.apk** below and install it over your existing multi-game WiiDS app. Do not uninstall or clear WiiDS.
2. Open **Options > Your games > Pokemon XD: Gale of Darkness > Choose file**.
3. Select your own supported **USA GXXE01 revision 0** disc image. Keep WiiDS open until import and checksum validation finish.
4. Return to the library and open the Pokemon XD case.

No game disc/data is included. Original disc files are read only. Existing saves in the earlier separate XD app are **not automatically migrated**; keep that app and its data until you have arranged a separate save transfer.

## What's included

- Pokemon XD case in the main WiiDS library, with normal disc scanning and the Android file picker.
- Separate XD saves, plus Save manager backup/export/restore and disc-only offloading through Manage games.
- Party, Bag, field and battle menus on the lower screen, retaining the current XD runtime's steel presentation and menu touch support.
- Matching title sky and area backgrounds, dialog/cutscene fades and battle transitions from the current XD runtime.
- The current battle input and slow-area repairs, and **WiiDS menu > Display > Resolution > 1080p (1920x1080)**.
- The existing WiiDS 1.2.5 games and player-profile tools. All 36 original native libraries and existing presentation images are preserved.

## Preview status

The integrated APK was installed on the owner's Thor with its complete installed-file hash verified. Build/vital lint, 218 production policy/disc checks, seven Android-framework host tests and independent source review passed. Integrated gameplay, Home/resume and return-to-library still need device acceptance; installation alone is not a gameplay test. Audio has not been verified by ear, and the known P-star-DA character mapping item remains.

This stays a **prerelease**; stable WiiDS 1.2.5 and its updater are unchanged. The APK's Android version is `1.2.6-xd-preview.1` (build `1206`). This existing release page retains its original `v1.4.2` tag/link for continuity, while its display title stays **1.26 Prerelease**. The old separate-app APK has been replaced by the integrated build.

The named source ZIP contains the matching reviewed Android sources, presentation inputs and native source overlays. Runtime binaries, discs, saves and signing keys are excluded; a clean-machine native rebuild is not claimed.
