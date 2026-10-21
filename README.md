# WiiDS

<img src="docs/images/wiids-minimal-logo.png" width="120" alt="Minimal WiiDS dual-screen app logo">

Reviewed source snapshot: [source/](source/) (runtime, Android frontend, translators and build tools).

Release preparation: [source and rebuild notes](docs/release/SOURCE-RELEASE.md),
[release checklist](docs/release/RELEASE-DRAFT.md), and
[component credits](docs/release/WIIDS-NOTICE.md). The final APK's matching source
archive and SHA-256 checksums belong beside it under GitHub Releases.

**Wii and GameCube favourites, running natively on the AYN Thor, with a second screen that works like a DS.**

WiiDS turns the AYN Thor's two displays into something that feels like a Nintendo handheld that never
existed: the game runs on the top screen, and the bottom screen gets its own purpose-built companion,
such as a live course map, a stored-item box, Star Coin slots or an episode guide. The games
aren't emulated. They run as native Android code through static recompilation and decompilation ports.

> **Status: public beta.** Download the signed Android APK, matching source and checksums from
> [GitHub Releases](https://github.com/Maddex712/WiiDS/releases). Bring your own supported game discs.
> The APK includes native engines and authorised mod patches, with no original or patched disc images.

<p align="center">
  <img src="docs/images/mkw-retro-rewind-peach-circuit.jpg" width="640"
       alt="Retro Rewind Peach Circuit racing on the AYN Thor's top screen, with a live overhead course map, standings and timer on the bottom screen">
</p>

## Games

| Game | How it runs | The second screen | Disc (see below) |
|---|---|---|---|
| **Mario Kart Wii** and **Retro Rewind** | Static recompilation (DriftDroid / WiiCompiled) | Live overhead course map with every racer, standings and race timer | Europe/PAL, `RMCP01` revision 0 |
| **New Super Mario Bros. Wii** | Static recompilation (NSMB Wii recompilation) | Course progress, Star Coins, score and an NSMB2-style stored-item box, all in the game's own fonts and art | USA, `SMNE01` revision 1 |
| **MSMBWii: Just a Little Adventure** | The same engine, with MidiGuyDP's fan level pack | The same companion, with its own case and save | NSMB Wii disc above, plus the mod |
| **Super Mario Sunshine** | Decompilation port, built for Android | A "Delfino Tours" episode tracker, the game's own live title sky, and FLUDD's tank | USA, `GMSE01` revision 0 |
| **Super Mario Eclipse 1.1.0** | Sunshine expansion in its own native runtime processes | The Sunshine companion design, with Eclipse-specific context handling | Uses your USA Sunshine disc; official patch and installer included |

## Highlights

- **Six cases with matching plastic colors:** white Mario Kart/Retro Rewind, red NSMB/MSMBWii, black Sunshine/Eclipse.
- **One APK, separate saves.** Eclipse's official 1.1.0 patch and MSMBWii's pack are bundled; Retro Rewind installs from its official servers. No companion APK is required.
- **A real game shelf.** A carousel of game cases: open one, and the disc lifts out and spins up as the
  game boots. Wii games sit together on the shelf; the GameCube case comes last, with a
  correctly sized 8 cm mini disc.
- **Bottom screens designed per game**, built from each game's own art (and, for Mario Kart and NSMB, its fonts) rather than a generic overlay.
- **NSMB2-style item reserve** for New Super Mario Bros. Wii: a spare power-up waits in the box on the
  bottom screen, and pressing R3 drops it from the top of the screen.
- **Widescreen and high frame rates**: Super Mario Sunshine in 16:9 at 1080p, 60 FPS with a 120 FPS option;
  Mario Kart Wii and New Super Mario Bros. Wii at 60 FPS with an experimental interpolated 120 FPS mode.
- **Retro Rewind** 6.13.0–6.13.1, installed and updated in-app from the official servers. Newer versions need a matching engine build first.

## Screenshots

Genuine captures from an AYN Thor, with a caption bar added. Where both screens are shown, the top screen is above and the bottom screen below.

### The launcher

<p align="center">
  <img src="docs/images/launcher-carousel.jpg" width="420" alt="WiiDS launcher: Mario Kart Wii, Retro Rewind, New Super Mario Bros. Wii and MSMBWii cases side by side, with the Super Mario Sunshine GameCube case selected and its mini disc shown on the bottom screen">
  <img src="docs/images/gamecube-case-open.jpg" width="420" alt="Super Mario Sunshine GameCube case opened, with the cover art inside the lid and the 8 cm mini disc in its tray">
</p>
<p align="center">
  <img src="docs/images/gamecube-disc-lift.jpg" width="420" alt="The Super Mario Sunshine mini disc lifting out of its case and spinning">
  <img src="docs/images/mkw-launcher-disc-spin.jpg" width="420" alt="The Mario Kart Wii disc lifted out and spinning while the game loads, with a loading screen on the bottom display">
</p>

### Mario Kart Wii and Retro Rewind

<p align="center">
  <img src="docs/images/mkw-original-luigi-circuit.jpg" width="420" alt="Original Mario Kart Wii Luigi Circuit race above, overhead course map with standings below">
  <img src="docs/images/mkw-retro-rewind-shy-guy-beach.jpg" width="420" alt="Retro Rewind GBA Shy Guy Beach race above, overhead course map with standings below">
</p>

### New Super Mario Bros. Wii

<p align="center">
  <img src="docs/images/nsmb-course.jpg" width="420" alt="World 1-1 above; the bottom screen shows World 1-1, lives, course progress with Mario's marker, Star Coins, score, coins and time">
  <img src="docs/images/nsmb-item-reserve.jpg" width="420" alt="World 1-1 above; a Propeller Mushroom waits in the stored-item box on the bottom screen">
</p>
<p align="center">
  <img src="docs/images/nsmb-item-drop.jpg" width="420" alt="A stored Super Mushroom dropping from the top of the screen toward Mario">
  <img src="docs/images/nsmb-world-map.jpg" width="420" alt="World 1 map above; the bottom screen shows the world's route graph with Mario's position and the stored item">
</p>

### MSMBWii: Just a Little Adventure

<p align="center">
  <img src="docs/images/msmbwii-level.jpg" width="420" alt="MSMBWii's custom World 1-1 above, the course companion below">
</p>

### Super Mario Sunshine

<p align="center">
  <img src="docs/images/sunshine-title.jpg" width="420" alt="Super Mario Sunshine title screen above; the game's own live title sky with sun rays and clouds mirrored on the bottom screen">
  <img src="docs/images/sunshine-file-select.jpg" width="420" alt="File select above; the Super Mario Sunshine logo over the title sky on the bottom screen">
</p>
<p align="center">
  <img src="docs/images/sunshine-episode-tracker.jpg" width="420" alt="Bottom screen in Bianco Hills: Delfino Tours postcard, Episode 1 Road to the Big Windmill, Shine slots, counters and FLUDD's water tank with the Spray nozzle">
  <img src="docs/images/sunshine-gameplay.jpg" width="420" alt="Super Mario Sunshine gameplay in 16:9 on the AYN Thor's top screen">
</p>

## What you'll need

- An **AYN Thor**. WiiDS is built around its two screens. Single-screen devices are planned later.
- **Your own game discs**, dumped yourself (for example with CleanRip on a Wii, or from Dolphin). Each game
  is built from one exact version of the disc, so the region and revision must match the table below.
  The app checks every file in your games folder and tells you if it's the wrong version.

### Exact game files for each game

| Game | Disc you need | Game ID | Region | Revision | Redump SHA-1 of a clean dump |
|---|---|---|---|---|---|
| Mario Kart Wii | Mario Kart Wii | `RMCP01` | Europe (PAL) | 0 | n/a |
| Retro Rewind | the same Mario Kart Wii disc | `RMCP01` | Europe (PAL) | 0 | n/a |
| New Super Mario Bros. Wii | New Super Mario Bros. Wii | `SMNE01` | USA | **1** | `aa7b134a7606435ddcd735f2964ebdecde38c2ef` |
| MSMBWii: Just a Little Adventure | the same NSMB Wii disc | `SMNE01` | USA | **1** | `aa7b134a7606435ddcd735f2964ebdecde38c2ef` |
| Super Mario Sunshine | Super Mario Sunshine (GameCube) | `GMSE01` | USA | 0 | `8d094f2c5c112aba9660f0478b16c7f2caf63cbf` |

**Not supported (yet):** the USA Mario Kart Wii disc (`RMCE01`), other regions, NSMB Wii revisions 0 and 2,
and PAL or Japanese Super Mario Sunshine.

**File types the first release will accept:**

| Format | Extension | Accepted |
|---|---|---|
| Plain disc image | `.iso`, `.gcm` | ✅ |
| Dolphin compressed | `.rvz` | ✅ |
| Wii backup | `.wbfs` (including split `.wbf1`) | ✅ |
| Other compressed images | `.wia`, `.gcz`, `.ciso` | ✅ |
| NKit | `.nkit.iso`, `.nkit.gcz` | ❌ Convert to RVZ or ISO in Dolphin first |

**How to check your disc:** in Dolphin, the game list shows the Game ID. Right-click the game, choose
**Properties → Info**, and look at **Revision**.

## Mods in the first release

Both mods are part of the first release:

- **Retro Rewind** (Mario Kart Wii): the Retro Rewind engine is built into WiiDS. The track pack itself
  (about 2 GB) downloads and updates in the app with one tap, straight from Retro Rewind's official servers.
- **MSMBWii: Just a Little Adventure** (NSMB Wii): its engine changes are built into WiiDS. Its level pack
  downloads in the app with one tap from the mod's official page.

Mod files are fetched from their creators' official sources rather than rehosted, out of respect for
their work. If the authors give permission, they'll be bundled directly instead.

## Credits

WiiDS stands on the work of these projects and people:

- **DriftDroid** (dorPXP) and **WiiCompiled** (patchzyy): the Mario Kart Wii static recompilation and Android runtime
- **NSMB Wii recompilation** (WitherBlitz): the New Super Mario Bros. Wii runtime
- **sms-pc-port** and **sms-launcher** (Chase Myers / chasem-dev), on the **Super Mario Sunshine decompilation** (doldecomp)
- **Retro Rewind** (ZPL and the Retro Rewind team)
- **MSMBWii: Just a Little Adventure** (MidiGuyDP and Fredo), using **Another Super Mario Bros. Wii** code (Skawo, Treeki)
- **Dolphin**, **SDL**, **aurora** and the other open-source libraries each project credits

The Mario Kart Wii work also lives in its own repository: [Maddex712/mkwii-thor](https://github.com/Maddex712/mkwii-thor).

## Legal

WiiDS is an unofficial fan project. It is not affiliated with, endorsed by or sponsored by Nintendo.
Mario Kart, Super Mario, New Super Mario Bros., Super Mario Sunshine, Wii and GameCube are trademarks of
Nintendo. No games, ROMs, disc images or extracted game files are distributed here. Please own the games
you play.
