# Kaizo Mario Galaxy Rebalanced 2.1 — local integration

2026-10-22: implementation authorized. Route: pinned official Riivolution overlay
applied to a private extracted RMGE01 rev0 copy, converted through the existing
cooker, with every active NTSC executable patch translated into native behavior.
Native code is shared; profile, NAND, saves, settings, setup journal and generated
lower presentation are separate. Existing companion design is the baseline.

Official patch is imported by the user, not bundled while redistribution terms
remain unspecified. Physical Thor validation was subsequently authorized when the
owner enabled wireless debugging. Public release, upload and push remain outside
this task.
Patch SHA256: b0b43ee39e3b2c2b7a3dcc4bc33d77718cb97da4b95f490146ec8a80ceddd548.

Research: `experiments/galaxy-mod-research/MODLOG.md`.
Private diagnostics and test output: `build/galaxy-kaizo`.

The local engine slice is complete. Final owner-signed/nondebuggable code906 APK
SHA256: `13ad3816d77b2b27ade61066241713d112bf49df175e328cd8b0e3111ded02b9`.
Physical own-disc setup, normal new Kaizo save/reload, corrected profile mirrors,
and original migrated-file/Gateway coexistence passed bounded checks. All25native
patch sites are mapped; this is not an all-boss/all-stage validation. Kaizo's own
physical gameplay sample is Gardens with its flat map; its 3D-planet stage and
case artwork remain outside the verified scope. See [Claude handoff](CLAUDE_HANDOFF.md)
and `build/galaxy-kaizo/physical-final-verification.json` for exact evidence/limits.

## Presentation polish (2026-10-22, Claude)

- **Case art.** No public Kaizo box art exists. `Compose-KaizoArt.py` builds the cover,
  spine and disc from Galaxy's retail art, with the mod's own title logo in place of
  Galaxy's. The logo is by TheDes7royer, from the creator ZIP's `LayoutData/TitleLogo.arc`.
  Outputs and provenance are in `build/galaxy-kaizo/art`.
- **Staging.** `Stage-KaizoArt.py` adds only `kaizo-cover`, `kaizo-spine` and `kaizo-disc`
  to the device's custom launcher folder and swaps the manifest atomically. All 21 existing
  image hashes were preserved (`build/galaxy-kaizo/device-art-verification.json`).
- **Frontend.** `Polish-Frontend.py` lets the carousel, collage and companion use the
  Kaizo pair. On the shared disc, the setup text now names each card's own action
  ("Set up Rebalanced" on Kaizo's card).
- **APK.** `8cecb45860b5e0a2…` differs from `13ad3816…` only in `classes.dex` and the
  baseline profile (`build/galaxy-kaizo/compare_polish_apk.py`). Installed in place:
  code 906, same signer. The previous APK is kept as `WiiDS-kaizo-dev-arm64-before-polish.apk`.
- **Seen on the Thor.** The Kaizo cover in the collage; the cover and spine in the fanned
  Galaxy stack; the cover and disc in the open case.
- **Not seen on the Thor.** The setup wording only shows before Kaizo is set up, so it was
  checked in code only.
- **Lower screen.** The Castle Gardens labels come from the mod's loaded names. A replay of
  file 1 showed the live marker and star-bit counter updating. There was nothing to change.
- **Planet framing: still unverified.** Driving the prologue by injected input reached the
  invasion phase of the Gardens (crystal-bound Toads, airships). The fixed cameras near the
  castle bridge stopped steering before the first galaxy. Nothing was saved. A Kaizo 3D-planet
  capture still needs someone to play through the prologue by hand.
