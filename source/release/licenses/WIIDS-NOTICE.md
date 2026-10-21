# WiiDS

WiiDS is an unofficial, non-commercial fan project that runs Wii and GameCube games natively on the
AYN Thor. It is not affiliated with, endorsed by or sponsored by Nintendo. Mario Kart, Super Mario,
New Super Mario Bros., Super Mario Sunshine, Wii and GameCube are trademarks of Nintendo.

**You must supply your own supported game discs.** WiiDS does not provide disc
images. Its native engines include compiled runtime and translated game logic;
required original disc sections and game-derived presentation assets are read
or reconstructed from your own imported disc and checked against supported
fingerprints. Authorised mod components are identified below. Please only play
games you own.

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

## Licences in this folder

- `LICENSE-GPL-3.0.txt`: GNU General Public License v3.0 (WiiDS, DriftDroid/WiiCompiled, the NSMB
  Wii runtime). A binary release must provide matching corresponding source and
  build/generation scripts; the project address alone is not a source offer.
- `THIRD-PARTY-NOTICES-*.md`: every bundled component, its licence and copyright.
- Further licence texts as named.
