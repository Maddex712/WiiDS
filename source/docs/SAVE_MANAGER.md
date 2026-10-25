# WiiDS 1.1 save manager (local preview)

Open **Options > Save manager**, choose a game, and use:

- **Back up now** to create a dated, verified backup inside WiiDS.
- **Export a backup** to copy a selected backup through Android's file picker.
- **Restore a local backup** to return to an earlier dated backup.
- **Restore a backup file** to select an exported WiiDS backup ZIP.
- **Import a Dolphin save** to select an in-game save through Android's file picker,
  without root. Choose the destination game first; originals and mods stay separate.

Close the game and any importer before managing saves. Mario Kart's runtime
survives in the library process, so restart WiiDS after playing Mario Kart or
Retro Rewind before opening these tools. Local backups are private app data;
export copies to keep them if WiiDS is uninstalled.

Restoring checks the ZIP format, explicit game/mod ID, file inventory, sizes and
SHA-256 hashes before changing saves. It creates a dated **before restore**
backup, stages the replacement, and journals the directory swaps. An interrupted
restore blocks game launching until Save manager recovers it. Recovery rolls back
an uncommitted transaction; after commit it finishes cleanup. Automated recovery
tests cover process-interruption layouts, not sudden power failure or hardware
storage errors.

## Save locations covered

| Game | Saved data |
| --- | --- |
| Mario Kart Wii | PAL title data under `WiiCompiled/NAND/title/00010004/524d4350/data` |
| Retro Rewind | `WiiCompiled/riivolution/save/RetroWFC/RMCP`, the `RetroWFC2/RMCP` alternate, and `NAND/shared2/Pulsar/RetroRewind6` mod settings |
| NSMB Wii | `nsmbw/NAND/title/00010000/534d4e45/data` |
| MSMBWii | Its separate `msmbwii/NAND/title/00010000/534d4e45/data` |
| Sunshine | `sms/saves/card-a` |
| Eclipse | Its separate `eclipse/saves/card-a` |
| Galaxy | Internal `galaxy/nand/title`, internal slot/quick-save NAND title folders, and external `galaxy/GalaxyData/saves` |
| Kaizo Galaxy | The equivalent folders under its separate `galaxy-kaizo` profile |

Galaxy mirrors and internal slot copies travel together. This avoids restoring
only a mirror while leaving a different authoritative NAND save behind. Neither
Galaxy's game assets nor its touch settings are included.

Achievements, RA credentials, motion/controller settings, Mii database, NSMB
reserve-item sidecars, and game images stay outside these archives. Restore does
not reset or merge them. Custom NAND locations and extra NSMB `Saves/<key>`
profiles are refused in this preview rather than selecting an uncertain save.
Original and mod backups are not interchangeable.

## Importing from Dolphin

Choose **Import a Dolphin save**, read the selected game's format guidance, and
select the file. WiiDS checks the save before displaying a confirmation. **Back up
and import** creates a dated, verified **before import** archive and uses the same
staging, atomic replacement, journal and recovery mechanism as WiiDS restore.
Canceling the confirmation changes no saves. The selected file is never moved or
modified. Export the local before-import backup if you want to keep it outside WiiDS.

| Destination | Accepted import |
| --- | --- |
| Mario Kart Wii | `rksys.dat`, or PAL RMCP Wii `data.bin` export |
| Retro Rewind | Its own `rksys.dat`, or a matching PAL RMCP Wii export; updates both RetroWFC save locations and preserves Pulsar settings |
| NSMB Wii | USA SMNE `wiimj2d.sav`, or its Wii `data.bin` export |
| MSMBWii | Its own USA SMNE `wiimj2d.sav`, or matching Wii export |
| Sunshine | USA GMSE01 seven-block game save exported as `.gci` |
| Eclipse | GMSE04 seven-block `super_mario_sunshine` game save exported as `.gci`; mod settings files are kept |
| Galaxy | Retail big-endian `GameData.bin`, native WiiDS `GameData.bin`, or Galaxy Wii `data.bin` export |
| Kaizo Galaxy | The same containers, selected explicitly for Kaizo's separate profile |

For Wii games, use Dolphin's save export or copy the raw file from its save
folder to a document location such as Downloads. For GameCube games, use Dolphin's
Memory Card Manager to export the individual game save as GCI. A full memory-card
image, complete NAND, general ZIP archive, GCS/SAV wrapper, emulator save state or
unsupported game/region is refused. Input is limited to 8 MiB.

Import replaces **all in-game slots/licenses contained in the selected save**.
It merges only the recognized game payload into the current profile; unrelated
save files, card entries, WiiDS backup archives and settings remain. Galaxy's
authoritative NAND copies and external main mirror receive the same converted
bytes. Its separately saved slot/quick snapshots remain available with their
previous progress; loading one later intentionally restores that older snapshot.
GameCube payload bytes retain their retail byte order while GCI metadata is
translated to the native card format and its index is updated without moving
other entries. Card capacity is checked before replacement.

Raw Wii saves and their standard title IDs cannot prove whether progress came
from an original or its mod. The confirmation explicitly asks the user to choose
the matching destination; WiiDS does not claim to authenticate mod compatibility.
GCI game/maker identity is checked, so Sunshine and Eclipse are not relabeled.
Supported Wii exports are decrypted and checked for header MD5, title, sizes,
duplicate/unexpected files and bounds. Game checksums are validated (MKW CRC,
NSMB header and six record CRCs, Galaxy checksum and chunk layout; GameCube checks
the option sector and at least one valid copy per slot). Console ECC certificate
authenticity and every embedded ghost/Mii's semantic validity are not asserted.

Protocol references: Dolphin's [Wii save implementation](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/HW/WiiSave.cpp),
[Wii save structures](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/HW/WiiSaveStructs.h),
and [GameCube card structures](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/HW/GCMemcard/GCMemcard.h).
Galaxy conversion follows the port's existing `DolphinSaveConverter` and game
serializers. No game data or owner saves are bundled with the importer.

## Verification

`experiments/release/test_save_archive.py` executes the production Java ZIP,
hashing, backup, restore and recovery code against disposable filesystem data.
It tests successful restore/undo, empty and absent roots, original/mod mismatch,
corruption, missing/extra files, invalid paths, size limits, failed pre-restore
backup, and recovery after each of several directory swaps.

`experiments/release/test_save_profiles.py` checks actual adapter destinations,
profile isolation, running-game/importer refusals, missing external storage,
custom NAND refusal and launch blockers with controlled Android services.

The October 24 Thor pass verified the actual NSMB backup, Android document-provider
export and same-save local restore. Both exported files match the fresh device
baseline; all 48 readable save/settings files remained byte-identical after
restore. Actual Galaxy and Mario Kart backups also passed. Each restore retains
a verified before-restore archive. File-picker import and broader gameplay
acceptance remain separate checks.

The October 25 dev17 pass additionally completed the actual Android document-picker
NSMB raw-save import, confirmation, dated backup and transaction using a copy of
the exact current save. All 48 accessible external save/settings files remained
byte-identical. `test_dolphin_saves.py` adds 124 production adapter/merge/refusal
checks across all eight destinations; `test_dolphin_android.py` repeats them in
a separate package using the production Android profile paths. Fixtures combine
retained owner NSMB/Galaxy/Sunshine data with independently packed Wii exports,
synthetic MKW and Eclipse-identity containers. This is not a claim of loading
someone else's save in every live game. `test_dolphin_native.py` independently
checks that the Java Galaxy conversion matches the existing C++ implementation
byte-for-byte in both directions on actual ARM64 Android.

`experiments/release/test_save_android.py` runs production storage code in an
isolated Android test package. Its 12 checks cover real internal/external
backup/restore, retained previous saves, wrong-profile refusal and symlink
refusal. Android's trusted Context storage bases are canonicalized before use;
links below those bases remain refused. The original preview failed on the
device's `/data/user/0` alias; the corrected production app passed.
