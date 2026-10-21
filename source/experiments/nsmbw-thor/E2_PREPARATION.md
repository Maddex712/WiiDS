# Owner USA revision 2 preparation

`prepare_e2.py` pins WitherBlitz commit `20f0158dd1d96bd94f821adf80ee361e4fbd414e`,
the owner's E2 DOL and four REL hashes, and the downloaded address correspondence
map. It refuses missing map ranges, mismatched inputs and unreviewed source changes.
The original E1 manifest, importer and executable hash remain unchanged.

Run from the private workbench root, with Python 3.12:

```powershell
python experiments/nsmbw-thor/prepare_e2.py `
  --repo reference/nsmbw-android `
  --disc 'build/nsmbw-thor/extracted/New Super Mario Bros. Wii (USA) (En,Fr,Es) (Rev 2)/DATA' `
  --address-map build/nsmbw-thor/address-map.txt `
  --output build/nsmbw-thor/e2-prepared --apply-reviewed-runtime
python experiments/nsmbw-thor/test_prepare_e2.py
./experiments/nsmbw-thor/Build-E2Translation.ps1
```

The script decompresses the owner's RELs and runs the SHA-pinned upstream linker
with explicit E2 module bases. Actual module sizes and heap chaining must agree.
It remaps all 39,582 upstream E1 function seeds, then supplements them from the
owner's E2 calls, function pointers and executable-section starts. All game data
and generated code stay under ignored `build/` or `reference/`.

The source migration is an enumerated 19 address sites in the REL-layout verifier,
menu-state IDs and profiler interval, plus the low-memory disc-revision seed.
`NSMBW_DISC_REVISION_E2=1` selects E2; an ordinary upstream build keeps E1 constants.
All scanned native binding entries map to unchanged addresses. The broader audit
also records 19 exclusions, including arithmetic division constant `0x80808081`
and dead MKW-specific constants. These must never be blindly shifted.

Repeating preparation recognizes already-migrated address expressions and preserves
additional source edits such as `ThorState::Capture()`. Before migration it refuses
unrelated changes in the four affected files. `runtime-e2-opt-in.patch` is a pure
revision patch, constructed against the pinned source; it excludes other agents'
launcher, Android, telemetry and build changes.

Receipts: `preparation.json`, `runtime-e2-audit.json`, `native-bindings-e2.json`,
`runtime-e2-applied.json`, and `translation.json` in the private preparation folder.
The PowerShell translation wrapper writes step logs and a receipt. The initial
translation was also run directly with the same four CLI commands and passed:
41,420 functions, 72 shards, 7,841,120 embedded owner bytes.

Two speculative map entries (`0x80006618`, `0x8000669C`) did not translate because
of unsupported word `0x18080000` at `0x800066D8`. They were not reached by the
translator's direct call walk; that does not prove they cannot be reached indirectly.
A boot path warned about branch `0x801AB010 -> 0x00000060` outside the loaded image.
Keep these warnings with the first-boot evidence; do not erase them by enabling
unsupported-instruction fallback.

Preparation and translation are completed checks. They establish neither a working
native build nor successful gameplay. On-device boot, input, pause/save, audio and
performance require separate evidence.
