# NSMB Wii Thor implementation

Requested 2026-10-18: native execution, Wii gameplay unchanged, physical controls,
top-screen gameplay and DS/3DS-style use of the lower display. Use the existing
Thor Mario Kart launcher. Claude handles visual design.

## Runtime route

- Requested source: https://github.com/ral1009/NSMBWiicompiled at
  `04b3c953893fef07fd86076a86f4b76565940166`, GPL-3.0. Its build targets PC x86-64
  and PAL revision 1; it cannot directly consume this owner's USA revision 2.
- Android candidate: https://github.com/WitherBlitz/NSMB-Wii-Multiplayer-Recompilation
  at `20f0158dd1d96bd94f821adf80ee361e4fbd414e`, GPL-3.0, native ARM64/Dawn/SDL.
  This is an explicit runtime substitution, not an Android build of the linked repo.
  Upstream targets USA revision 1. Thor must generate its code locally from E2.
- Owner dump: SMNE01 revision 2, NKit ISO. SHA-256
  `8a3575742cc1f133bc306b614333f5ce38bace2062a61cb55ce519a44c82d033`.
- Extracted DOL SHA-256:
  `0373b4dbffd7c9da9d27eb7257a7677877a3e33ceb81818bab1daa93560b9907`.
- Official NKit 3.2.0 extracted 1195 files into ignored `build/nsmbw-thor/extracted`.
  NKit reports extraction unverified, partial read; this is not a full-disc hash-tree
  validation. No update-partition recovery files were fetched.
- Address correspondence: https://github.com/mkwcat/nsmbw-project/blob/master/address-map.txt
  (downloaded and SHA-256 pinned by `prepare_e2.py`). E2 has changed save-function
  instruction ranges and REL allocations. Never simply accept E2 in an E1 runtime.

## Isolation and launcher

The shared launcher keeps `com.driftdroid.android.thor`. NSMB uses
`com.wither.nsmbw.thor`, with its existing separate `:game` process. The two games
have independent data directories, NAND saves, settings and native libraries.
`LAUNCHER_INTEGRATION.md` documents the transport for Claude's library entry.
Visual integration and lower-screen art remain Claude-owned.

This pass targets offline single-player. Network features inherited from the
candidate runtime are outside the validation scope.

## First playable check

1. Verify E2 inputs, REL layout, native address migration, regenerated translation.
2. Build native ARM64 with the E2 revision flag, then package with a native receipt.
3. Stage the owner's files into the isolated NSMB profile, without changing MKW.
4. Check boot, physical selection, 1-1 movement/jump/run/spin, pause and return.
5. Expose read-only game state for the lower display; verify fields in gameplay
   before calling them supported. Leave the original upper HUD available meanwhile.

No boot, performance, audio, complete-level, save or dual-screen claim follows
from extraction, translation, compilation or the upstream README alone.
