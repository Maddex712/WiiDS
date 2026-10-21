# MSMBWii: Just a Little Adventure (Thor mod case)

MidiGuyDP's level pack (2013), `MSMBWii.zip` from riivolution.nanolx.org (3,007,052 bytes,
MD5 89d35c1a4cc06ddc28a18ab30ed53581), downloaded with the owner's permission on 2026-10-19.
RVLution Wii (also downloaded, 270 MB) uses the Newer engine (Loader.bin, DLCode) and is parked
with Newer SMBW.

## What the mod is
- Files: 43 replaced levels (worlds 1-4), 12 tilesets, a US title layout. Riivolution XML
  folder patches; the runtime's own overlay support applies them (`overlay_roots`).
- Code: the Another SMBW loader's `AnotherRes/SystemUS_1.bin` (USA rev 1; every site matches
  this disc) writes ~20 words: castle-level (0x17) special cases off in the camera/scroll
  code, a few branches removed, and a 6-word cave that lets only profile 0x193 stop the
  flag-object search. Applied before translation by `prepare_msmbwii.py` to copies of the
  E1 DOL/REL/flat image; the cave is rewritten in place (fcmpo+bgt/blt for the four bound
  checks, same result for non-NaN positions). Loader hooks are not applied.

## Build
- `reference/nsmbw-android-msmbwii`: copy of the E1 checkout (runtime incl. the NSMB2 item box),
  app id `com.wither.nsmbw.thor.msmbwii`, name "MSMBWii", launch game id `msmbwii`,
  Config.toml `overlay_roots = [files/mod]`. Native check uses the patched DOL hash; the disc
  check stays the clean revision 1.
- `build/nsmbw-thor/msmbwii/prepared` (patched inputs, receipts), translation 27 s, native
  975 steps, `MSMBWii-Thor.apk` (137 MB). `stage_native.py --dol-sha256` for mod builds.
- Device data: `files/game` copied from the NSMB app, `files/ThorHud`, `files/mod/{riivolution,
  msmbwiijala}`, `riivolution/config/SMNE.xml` enables the option. Saves: mod redirect
  `files/mod/msmbwiijala/save/SMNE` (another.sav), separate from NSMB.

## Verified on the Thor (2026-10-19)
Runtime log: 1 active patch, 4 mappings (Music skipped: empty), 43 disc files replaced, 59
overlays. New save created; World 1-1 is the custom level; lower screen works. Launcher case
five (art composed from the owner's NSMB cover/disc with an MSMBWii band) launches it.
Not verified: castles, full playthrough.
