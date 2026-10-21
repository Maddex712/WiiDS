# platform/disc — reading the game straight from a disc image

`gcdisc.h` / `gcdisc.cpp` read a GameCube disc image and expose its header, its system files and its file system table (FST).
The DVD layer can serve the game from the user's `.iso` this way, with no extracted folder.

| Image type | Support |
|---|---|
| `.iso` / `.gcm` (1:1 dump) | yes |
| `.ciso` (Dolphin's sparse block format) | yes; unstored blocks read as zero |
| `.rvz`, `.wia`, `.gcz` | no: recognised and refused with a message to convert to `.iso` in Dolphin. RVZ would need zstd/LZMA and its own partition format. |

The image is recognised by content (magic `0xC2339F3D` at `0x1C`, or `CISO`), not by extension.

`gcdisc_open_embedded()` opens an image bundled into the running executable by `tools/bundle_disc.py`: the file ends with a 32-byte trailer `{"SMSDISC1", u64 LE image offset, u64 LE image size, 8 zero bytes}`, and the image (a trimmed but valid disc: system files, FST, then the files packed 32-byte aligned) sits at that offset.
On macOS it falls back to `Contents/Resources/disc.gcm` of the app bundle the executable runs in (`SMS.app`, built by `tools/make_mac_app.sh`), since appending to a signed executable would break its signature.
`platform/dvd` uses it when no disc argument, `SMS_DISC_IMAGE` or `SMS_DISC_ROOT` names another source.
Offsets are 64-bit (`pread64`, `O_LARGEFILE`), so a 32-bit build reads bundles and images past 2 GiB.
All on-disc fields are big-endian; the API returns host values.
Reads use `pread` and may run on any thread.
The image is only ever opened read-only.

## API

```c
GCDisc* d = gcdisc_open(path, /*verbose*/ 1);        // NULL if not a usable image
gcdisc_game_id(d);                                     // "GMSE01"
gcdisc_header(d);                                      // 0x440-byte boot.bin; the first 0x20 bytes are a DVDDiskID
int32_t e = gcdisc_lookup(d, "/data/common.szs");      // FST entry index, -1 if missing (case-insensitive; "..", "//")
GCDiscEntry info; gcdisc_entry(d, e, &info);           // name, is_dir, parent/next (dirs), offset/size (files)
gcdisc_read_file(d, e, file_offset, buf, n);           // bytes read, clamped to the file
gcdisc_read(d, disc_offset, buf, n);                   // raw disc read (what DVDReadAbsAsync* would need)
gcdisc_system_file(d, GCDISC_DOL, &off, &size);        // also BOOT, BI2, APPLOADER, FST
gcdisc_fst(d, &size);                                  // raw big-endian fst.bin, as on disc
gcdisc_entry_count(d); gcdisc_path(d, e, buf, len);    // iterate / name entries
gcdisc_probe(path); gcdisc_close(d);
```

FST entry numbers are the disc's own, so `DVDConvertPathToEntrynum` and `DVDFastOpen` get the same numbers as on hardware.

## Switching platform/dvd to it (for the bring-up lead)

Today `platform/dvd/dvd.cpp` builds `g_fst` from `<root>/../sys/fst.bin` and `pread`s host files under `SMS_DISC_ROOT`.
`platform/disc/*.cpp` is already compiled into `sms` by the `platform/**/*.cpp` glob.
A minimal switch:

1. **Choosing the source** (`port_runtime.cpp` argument parsing).
   Use `SMS_DISC_IMAGE=/path/game.iso` if set.
   Otherwise, if a positional argument (or `SMS_DISC_ROOT`) names a regular file and `gcdisc_probe()` accepts it, use it as the image.
   Otherwise keep today's extracted-folder behaviour.
2. **`port_dvd_init`**, when an image is chosen: `g_disc = gcdisc_open(path, 1)`, then fill `g_fst` from `gcdisc_entry()`.
   The fields map one to one: `dir = is_dir`, `name`, `parent`, `next`, `length = size`.
   Keep the file's disc offset from `info.offset` in a new `Entry::disc_offset` field, and set no `host` path.
   Or call `load_fst_bin` on the bytes from `gcdisc_fst()`, since it is the same table.
   Take the disk ID from the header: `memcpy(&g_disk_id, gcdisc_header(g_disc), sizeof g_disk_id)`, which replaces reading `sys/boot.bin`.
3. **`do_read`**: when `g_disc` is set, `done = gcdisc_read_file(g_disc, entry, offset, addr, length)` replaces the `pread` loop.
   The `mem` override (`port_dvd_override`) keeps priority as it does now.
4. **Absolute reads** (`DVDReadAbsAsync*`, used by the apploader/boot path if the port ever needs it): `gcdisc_read(g_disc, offset, ...)`.
   `gcdisc_system_file()` gives `main.dol`, `bi2.bin` and `apploader.img` in place.
5. Log the source at start, e.g. `[dvd] disc image GMSE01 (…iso): 181 entries`, so the choice is visible.

## Test

```sh
make -C platform/disc/tests run            # ISO=..., FILES=.../files; FULL=1 compares every file
```

The test reads the user's image in place and never copies it.
Against the retail North American image (GMSE01) and its extracted `files/`:

- **Header and FST:** `GMSE01` "Super Mario Sunshine", 1,459,978,240 bytes.
  All 181 FST entries resolve: 174 files (1117.3 MiB) and 6 directories.
  `gcdisc_lookup(gcdisc_path(i)) == i` for every entry, every file lies inside the image, and every size matches the extracted file.
- **System files:** `boot.bin`, `bi2.bin`, `apploader.img`, `main.dol` and `fst.bin` read from the image are byte-identical to `orig/GMSE01/sys/*`.
- **File contents:** `FULL=1` compares all 174 files with the extracted copies; all are byte-identical.
  The default run compares eight: `nintendo.szs`, `mario.szs`, `scene/dolpic0.szs`, `openingA.thp`, `PerformLists.bin`, `stageArc.bin`, `mSound.asn`, `opening.bnr`.
- **CISO:** a synthetic disc is written as `.iso` and as `.ciso` to a temporary directory (`build-disc-test/`) and deleted afterwards.
  Both read back identically, and an unstored block reads as zeros.

A packed image from `tools/bundle_disc.py --image-only` passes the same test except `boot.bin` and `fst.bin`, which it rewrites with the new DOL, FST and file offsets; every file, `apploader.img` and `main.dol` stay byte-identical.
