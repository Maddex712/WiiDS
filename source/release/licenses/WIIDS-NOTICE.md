# WiiDS

WiiDS is an unofficial, non-commercial fan project that runs Wii and GameCube games natively on the
AYN Thor. It is not affiliated with, endorsed by or sponsored by Nintendo. Mario Kart, Super Mario,
New Super Mario Bros., Super Mario Sunshine, Wii and GameCube are trademarks of Nintendo.

**You must supply your own supported game discs.** WiiDS does not provide disc
images. Its native engines include compiled runtime and translated game logic;
required original disc sections and game-derived presentation assets are read
or reconstructed from your own imported disc and checked against supported
fingerprints. This release also carries the owner's explicitly selected current
presentation assets, as listed in its hashed presentation manifest. Authorised
mod components are identified below. Please only play games you own.

Source and project page: https://github.com/Maddex712/WiiDS

## Built on

- **DriftDroid** (dorPXP) and **WiiCompiled** (patchzyy): Mario Kart Wii static recompilation and
  Android runtime. GPL-3.0. https://github.com/dorPXP/driftdroid
- **NSMB Wii recompilation** (WitherBlitz): New Super Mario Bros. Wii runtime. GPL-3.0.
- **sms-pc-port** (SoftwareAura LLC / Chase Myers / chasem-dev) and **sms-launcher**:
  Super Mario Sunshine port and Android shell. The Eclipse port snapshot and
  Android shell include their MIT texts. The older regular-Sunshine port snapshot
  predates that licence file; the owner reports Chase Myers's redistribution
  permission, recorded in the release permissions document.
- **Super Mario Sunshine decompilation** (doldecomp and contributors): CC0 1.0.
- **Retro Rewind** (ZPL and the Retro Rewind team): a recompiled build of its Code.pul is included
  with the team's permission; the pack itself is downloaded from its official servers.
- **MSMBWii: Just a Little Adventure** (MidiGuyDP and Fredo), with **Another Super Mario Bros. Wii**
  code by Skawo and Treeki: included with the authors' permission (official page:
  riivolution.nanolx.org).
- **Super Mario Eclipse** (Eclipse Team / JoshuaMKW): mod source under GPL-3.0.
  Its unmodified official 1.1.0 patch is included under CC BY-NC-ND 4.0, with
  attribution and full terms in `THIRD-PARTY-NOTICES-Eclipse.md` and
  `LICENSE-CC-BY-NC-ND-4.0.txt`. Original Sunshine and Eclipse use separate game data
  and save profiles. The patch archive's shell-patcher license/README are retained
  separately; their MIT terms do not replace the mod payload's license.
- **VCDIFF decoder**: the Android Eclipse patch decoder is adapted from
  `sms-pc-port/tools/mods/vcdiff.py`, MIT, copyright (c) 2026 SoftwareAura LLC.
- **Dolphin Emulator** data and code, **SDL**, **aurora** and the other libraries listed in each
  component's third-party notices in this folder.

Portions of this software are copyright © 2026 The FreeType Project (www.freetype.org).
All rights reserved.

## Included presentation

The owner requested that this release preserve the current covers, discs, menu
music and lower-screen/settings artwork. The bundled presentation manifest lists
the exact files, sizes and SHA-256 hashes. These assets are separate from ROMs,
personal saves and credentials, none of which are included.

- Nintendo: original game cover/disc artwork, game-derived UI/HUD/fonts and the
  supplied Wii Menu recording. Nintendo names and artwork remain their respective
  owners' material; inclusion does not imply Nintendo approval or endorsement.
- Some-Crappy-Edits: Eclipse fan disc design, cropped/normalised from the creator's
  supplied mockup. The selected Eclipse cover was supplied by the owner; its
  independent creator has not been verified.
- The approved Retro Rewind disc design and minimal WiiDS app logo were generated
  with the built-in image tool and selected by the owner.

The owner's authorisation to package this configured presentation is not a claim
that the owner created every asset. The project's code licence does not relicense
third-party presentation material. Component licence texts and the Eclipse mod
payload terms remain separately applicable.

## Licences in this folder

- `LICENSE-GPL-3.0.txt`: GNU General Public License v3.0 (WiiDS, DriftDroid/WiiCompiled, the NSMB
  Wii runtime). A binary release must provide matching corresponding source and
  build/generation scripts; the project address alone is not a source offer.
- `THIRD-PARTY-NOTICES-*.md`: every bundled component, its licence and copyright.
- Further licence texts as named.
