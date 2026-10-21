# Step 2: disc reader, Games screen, BCA

## wiids_disc (PORTING/wiids/disc)

One C++17 library reads every common GameCube/Wii image format and extracts what each game needs.

| Format | Support |
|---|---|
| ISO / GCM | yes |
| WBFS | single file and split (`.wbfs` + `.wbf1`...) |
| RVZ, WIA | yes (none, purge, bzip2, LZMA, LZMA2, Zstandard; RVZ padding regenerated) |
| GCZ, CISO | yes |
| NKit | recognised and refused with a reason (not a 1:1 copy) |

- Wii: the game partition is decrypted (title key via the common key, AES-128-CBC; ARMv8 AES
  instructions when the chip has them, checked at run time) and written as `sys/` + `files/`.
  `fst.bin` and `main.dol` are renamed into place last, so `sys/main.dol` means "complete".
- GameCube: written as a plain 1:1 ISO (`.part`, then renamed).
- Decoders are vendored in `third_party/` (liblzma from Dolphin, bzip2 1.0.8, zstd decompression).
  zlib comes from the platform. Notices: `THIRD-PARTY-NOTICES.md`.

### Host verification (experiments/release/disc-test/build.bat → build/disc-test)

| Image | Result |
|---|---|
| Super Mario Sunshine (USA).rvz → ISO | SHA-256 `67cec163…` identical to the known-good ISO |
| NSMB Wii (USA) Rev 1 .rvz | every file identical to the Dolphin extraction; `main.dol` `166eb6b7…`; game data check OK |
| MarioKart.iso (RMCP01) | `main.dol` `80d18895…`; game data check OK; 28 s for 2.6 GB on the PC |
| Mario Kart Wii (USA).wbfs | identified RMCE01, extracts fully; the game correctly refuses it (needs PAL) |
| NSMB Wii Rev 2 .nkit.iso | identified as NKit and refused with the conversion hint |

## Games screen (DriftDroid app, `GamesActivity`)

- `rom/WiiDsDisc.kt` (JNI to `libwiidsdisc.so`), `rom/GameCatalog.kt` (exact disc per game and
  install folders), `rom/GameScanner.kt` (searches device storage and SD cards, headers only),
  `rom/GameInstaller.kt` (imports into a temporary folder, swaps in when complete).
- Each game card shows the exact disc needed, what was found, why a found file can't be used
  (region, revision, NKit), and Import / Choose file. Accepted formats are listed on screen.
- Opens by itself on first run (nothing imported), from the launcher's "Your games" entry, and when
  a game that isn't imported is chosen.
- Install folders (host app): Mario Kart `files/WiiCompiled/DiscData`, NSMB `files/games/nsmbw`,
  MSMBWii mod `files/games/mod`, Sunshine `files/games/sms/game.iso`. The other games read these
  once the apps are merged (step 4).
- Not yet seen on a device (the Thor is away): first look happens in step 6.

## NSMB BCA (owner-approved)

When `disc/bca.bin` is absent, both NSMB runtimes answer the BCA request exactly as Dolphin's
`Core/HW/DVD/DVDInterface.cpp` (`ReadBCA`) does: 64 zero bytes with byte 0x33 set to 1. A real
dump still takes priority. Takes effect at the next NSMB native build.
