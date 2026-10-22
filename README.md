# WiiDS

<img src="docs/images/wiids-minimal-logo.png" width="120" alt="Minimal WiiDS dual-screen app logo">

Play supported Wii and GameCube games on the AYN Thor, with a companion on its lower screen.

## Quick start

**You need:** an **AYN Thor with both screens**, Android **11 or newer** (**12L or newer for Galaxy**),
and your own [compatible game dump](#exact-game-files-for-each-game). The region and revision must match.
**Games are not included.** Leave room for the APK, your dump and the prepared game files.

1. **[Download WiiDS 1.0 for AYN Thor (APK, 1.85 GB)](https://github.com/Maddex712/WiiDS/releases/download/v1.0.0/WiiDS-1.0-arm64.apk)** on your Thor.
2. **Install the APK and open WiiDS.** Allow your browser or file manager to install apps if Android asks.
   Wait for WiiDS to prepare its files. Updating? Install over your existing copy; do not uninstall it.
3. **Copy your game dump to the Thor.** Open **Options → Your games** (it opens automatically on first use).
   Allow storage access, then use **Choose file** on the game's card or **Search again** to find your dump.
   Keep WiiDS open until import finishes.
4. **Galaxy only:** use a clean **USA Galaxy 1 ISO, `RMGE01`, revision 0**, then press **Set up Galaxy**.
   After installing WiiDS and copying the ISO, leave **at least 2.8 GB free**, or **7.6 GB to include movies**.
   Setup prepares the game and lower screen automatically. **No helper app or PC is needed.**
5. **Play.** Return to the library. Press **A** to choose a game (Original or its mod), **A** to open its case, then **A** to launch the disc.
   **B** goes back; **X** opens Options.

**Want the mods?**

- **MSMBWii:** installs automatically after importing the supported NSMB Wii disc. Use **Install mod** or **Retry mod installation** if needed.
- **Eclipse:** import the supported Sunshine disc, then choose **Install Eclipse** in **Your games**. The patch is included.
- **Retro Rewind:** import the supported Mario Kart Wii disc, then press **Y** in the library or open **Options → Retro Rewind updates**. This download needs internet access.

[Check supported discs and formats](#what-youll-need) · [Galaxy setup and storage details](docs/release/GALAXY-SETUP.md) · [Per-game guide](docs/GAMES.md)

<p align="center">
  <img src="docs/images/mkw-retro-rewind-peach-circuit.jpg" width="640"
       alt="Retro Rewind Peach Circuit racing on the AYN Thor's top screen, with a live overhead course map, standings and timer on the bottom screen">
</p>

## Games

| Game | How it runs | The second screen | Disc (see below) |
|---|---|---|---|
| **Mario Kart Wii** | The original Wii racer, through DriftDroid / WiiCompiled static recompilation | Independent overhead course map, racer portraits, standings, held items and timer | Europe/PAL, `RMCP01` revision 0 |
| **Retro Rewind** | The community Mario Kart Wii expansion, in its own launcher entry | The Mario Kart companion with a matching supported-course map cache | Same Mario Kart Wii disc; install the content pack in-app |
| **New Super Mario Bros. Wii** | Static recompilation (NSMB Wii recompilation) | Course progress, Star Coin tray, score, stored-item box and world route with NSMB2-style world bubbles | USA, `SMNE01` revision 1 |
| **MSMBWii: Just a Little Adventure** | The same engine, with MidiGuyDP's fan level pack | The same companion, with its own case and save | NSMB Wii disc above; mod included |
| **Super Mario Sunshine** | Decompilation port, built for Android | Enlarged area map and Mario marker, moving cloud backdrop, postcard, episode Shines and original-art counters; live title sky in menus | USA, `GMSE01` revision 0 |
| **Super Mario Eclipse 1.1.0** | Sunshine expansion in its own native runtime processes | The same live-map companion, with Eclipse-specific area and menu handling | Uses your USA Sunshine disc; official patch and installer included |
| **Super Mario Galaxy 1** | Native Galaxy-Android/Petari port inside WiiDS; own-disc conversion and lower bake are integrated | Live counters, textured GPU planet with matched actor visibility/matrices and player marker; Gardens flat map | USA `RMGE01` revision 0; Android API 32+ |

Galaxy setup automatically generates its private lower-screen art and models from your disc. Read [Galaxy setup/migration and current limits](docs/release/GALAXY-SETUP.md). **The new integration was validated on desktop/Android emulation, not a physical Thor.** Earlier Thor images retain their original development-build scope.

Read the [game guide](docs/GAMES.md) for each game's setup, companion screen and current limitations.

## First launch

Follow the [Quick start](#quick-start) above. WiiDS is built for the Thor's two screens;
single-screen phones are not supported by this release.

For split WBFS dumps, keep `.wbfs` and all `.wbf1`/later numbered parts in the same folder.
Use **Search again** in **Your games** to collect all parts; **Choose file** opens only one file.

Imports and mods need storage beyond the APK and original dump. Eclipse creates a separate game image.
Update WiiDS in place to keep existing profiles; uninstalling or clearing app data can remove them.
Galaxy 2 is not supported. See [Galaxy setup](docs/release/GALAXY-SETUP.md) for other input formats,
legacy-save migration and current limits.

## Highlights

- **The complete presentation:** selected retail/fan covers and discs, Wii Menu music and UI sounds, original HUD/fonts/settings artwork and cached lower-screen maps are bundled. Existing user overrides and saves are preserved.
- **Seven cases with matching artwork:** white Mario Kart/Retro Rewind, red NSMB/MSMBWii, black Sunshine/Eclipse.
- **Printed case spines:** Mario Kart, NSMB Wii and Sunshine use crops from retail full-cover scans;
  Retro Rewind and Eclipse use community cover artwork. MSMBWii's spine is an explicit custom
  adaptation. [Artwork sources](docs/GAMES.md#case-artwork).
- **One APK, separate saves.** Eclipse's official 1.1.0 patch and MSMBWii's pack are bundled; Retro Rewind installs from its official servers. No companion APK is required.
- **A real game shelf.** A carousel of game cases: open one, and the disc lifts out and spins up as the
  game boots. Wii games sit together on the shelf; Sunshine and Eclipse use black
  GameCube cases with correctly sized 8 cm mini discs.
- **Stable family stacks.** Each original game and its mod move together when browsing the main
  shelf. The NSMB/MSMBWii pair keeps its draw order when moving back to Mario Kart, and selecting
  an exposed case slice opens the matching family.
- **Bottom screens designed per game**, built from each game's own art (and, for Mario Kart and NSMB, its fonts) rather than a generic overlay.
- **NSMB2-style item reserve** for New Super Mario Bros. Wii: a spare power-up waits in the box on the
  bottom screen, and pressing R3 drops it from the top of the screen.
- **Video options:** 1080p rendering and 60 FPS defaults, with an optional 120 FPS setting.
  Mario Kart Wii and NSMB Wii use experimental presentation interpolation while their game logic
  stays at 60 Hz. The Sunshine/Eclipse 120 FPS option is also experimental; it is not a sustained-performance guarantee.
- **Retro Rewind** 6.13.0–6.13.1, installed and updated in-app from the official servers. Newer versions need a matching engine build first.

## New in 1.0

Galaxy uses relocated native memory inside a non-debuggable WiiDS process, with its own
NAND/settings/import profile. Its Back menu includes Quick Save and Controls > Buttons.
WiiDS Options has a controller-button chooser, and NSMB/MSMBWii have a Controller tab.
See [1.0 release/validation notes](docs/release/V1.0.md).

## Screenshots

Genuine captures from an AYN Thor, with a caption bar added. Where both screens are shown, the top screen is above and the bottom screen below.
Game titles identify Original, Retro Rewind and the other mod entries separately. Mario Kart and
Retro Rewind retain their existing gameplay captures because their presentation is unchanged.
Original Sunshine's Airstrip and title-sky images below were captured in **WiiDS 0.9.0-beta.5**.
The unchanged launcher, NSMB and Eclipse images retain their **beta.3** labels, as do Sunshine's
older file-selection and case captures. Mario Kart images retain their existing capture context.
Each image identifies the build that actually produced it.
Captures document their named scene, rather than implying every course or stage was retested.

### Launcher and Options — captured in WiiDS 0.9.0-beta.3

Captures from the installed beta.3 build, with each complete upper display above its lower display.
The shelf now keeps original/mod families together during transitions, including NSMB/MSMBWii when
moving back to Mario Kart. Retail and community scanned spines are visible on the angled cases.
These are launcher captures; each gameplay section below identifies its game and capture context.

<p align="center">
  <img src="docs/images/launcher-beta3-shelf.png" width="420" alt="WiiDS 0.9.0-beta.3: main shelf with Original Mario Kart Wii selected, Retro Rewind behind it, red NSMB and MSMBWii cases together, black Sunshine and Eclipse cases together; Original Mario Kart disc and readiness below">
  <img src="docs/images/launcher-beta3-nsmb-stack.png" width="420" alt="WiiDS 0.9.0-beta.3: New Super Mario Bros. Wii family selection, with Original selected and MSMBWii beside it, both red cases with printed spines; Original NSMB Wii disc and readiness on the lower display">
</p>
<p align="center">
  <img src="docs/images/launcher-beta3-sunshine-stack.png" width="420" alt="WiiDS 0.9.0-beta.3: Super Mario Sunshine family selection, with Original selected and Super Mario Eclipse beside it in black GameCube cases; Original Sunshine mini disc and readiness on the lower display">
  <img src="docs/images/launcher-beta3-options.png" width="420" alt="Shared WiiDS 0.9.0-beta.3 Options: Your games, Graphics, Mii Data, Retro Rewind updates, Texture Packs, Diagnostics, App Icon, Server Settings and Menu Music above; library options companion below">
</p>

### Mario Kart disc launch

This earlier Mario Kart capture shows the disc-lift animation; its game and lower-screen design are unchanged.

<p align="center">
  <img src="docs/images/mkw-launcher-disc-spin.jpg" width="420" alt="The Mario Kart Wii disc lifted out and spinning while the game loads, with a loading screen on the bottom display">
</p>

### Mario Kart Wii and Retro Rewind

<p align="center">
  <img src="docs/images/mkw-original-luigi-circuit.jpg" width="420" alt="Original Mario Kart Wii Luigi Circuit race above, overhead course map with standings below">
  <img src="docs/images/mkw-retro-rewind-shy-guy-beach.jpg" width="420" alt="Retro Rewind GBA Shy Guy Beach race above, overhead course map with standings below">
</p>

### New Super Mario Bros. Wii

Fresh WiiDS 0.9.0-beta.3 captures of **Original New Super Mario Bros. Wii**. The World 1-1 companion
shows lives, goal-flag progress, the Star Coin tray, score and reserve box; coins and time remain above.
The world-map companion shows Mario on the actual route with linked NSMB2-style world bubbles below.

<p align="center">
  <img src="docs/images/nsmb-beta3-course.png" width="420" alt="Original New Super Mario Bros. Wii in WiiDS 0.9.0-beta.3: real World 1-1 gameplay above; five lives, goal-flag progress, three empty Star Coin slots, score and an empty reserve box below">
  <img src="docs/images/nsmb-beta3-world-map.png" width="420" alt="Original New Super Mario Bros. Wii in WiiDS 0.9.0-beta.3: World 1 map above; the live route with Mario marker, linked world bubbles and five lives below">
</p>

### MSMBWii: Just a Little Adventure

Fresh WiiDS 0.9.0-beta.3 **library capture** of MSMBWii's red case, mod disc and readiness panel.
MSMBWii shares the companion design shown in the Original NSMB Wii screenshots above and keeps its
own save profile.

<p align="center">
  <img src="docs/images/msmbwii-beta3-open-case.png" width="420" alt="MSMBWii: Just a Little Adventure in WiiDS 0.9.0-beta.3, library screenshot: open red case with mod-labelled cover and disc above, mod disc and readiness below">
</p>

### Super Mario Sunshine

**Original Super Mario Sunshine in WiiDS 0.9.0-beta.5:** the larger Airstrip overview fills the
lower map panel, with Mario's marker, postcard and original-art counters over moving clouds.
The upper capture shows the complete MARIO label. The title capture shows the game's live sky
and birds on both displays; a still image illustrates the scene, not its animation rate.

<p align="center">
  <img src="docs/images/sunshine-beta5-airstrip.png" width="420" alt="Original Super Mario Sunshine in WiiDS 0.9.0-beta.5: Delfino Airstrip gameplay with the complete MARIO label above; larger overhead main-island map and Mario marker, postcard and original-art counters below">
  <img src="docs/images/sunshine-beta5-title-sky.png" width="420" alt="Original Super Mario Sunshine in WiiDS 0.9.0-beta.5: actual title sky and birds on the upper display, with the live sky companion on the lower display">
</p>

These unchanged file-selection and black GameCube case images remain labeled **WiiDS 0.9.0-beta.3**.

<p align="center">
  <img src="docs/images/sunshine-beta3-file-select.png" width="420" alt="Original Super Mario Sunshine in WiiDS 0.9.0-beta.3: file selection above; Sunshine logo, live sky and Welcome to Isle Delfino companion below">
  <img src="docs/images/sunshine-beta3-open-case.png" width="420" alt="Original Super Mario Sunshine in WiiDS 0.9.0-beta.3: its black GameCube case open above, with the cover and mini disc; selected disc readiness on the lower display">
</p>

### Super Mario Eclipse 1.1.0

Fresh WiiDS 0.9.0-beta.3 captures of **Super Mario Eclipse 1.1.0**: tutorial trainer dialogue and its
black GameCube case. The lower screen shows the current purple area map, arrival photo and original-art
counters. Eclipse's game data and saves stay separate from regular Sunshine.

<p align="center">
  <img src="docs/images/eclipse-beta3-tutorial.png" width="420" alt="Super Mario Eclipse 1.1.0 in WiiDS 0.9.0-beta.3: Mario speaking with the tutorial trainer above; the purple collision-floor area map, arrival photo and original-art counters below">
  <img src="docs/images/eclipse-beta3-open-case.png" width="420" alt="Super Mario Eclipse 1.1.0 in WiiDS 0.9.0-beta.3: its black GameCube case open above with cover and mini disc; separate game readiness on the lower display">
</p>

## What you'll need

- An **AYN Thor**. WiiDS is built around its two screens. Single-screen devices are planned later.
- **Your own game discs**, dumped yourself (for example with CleanRip on a Wii). Each game
  is built from one exact version of the disc, so the region and revision must match the table below.
  Import supported images through **Your games**; the app verifies their supported version.

### Exact game files for each game

| Game | Disc you need | Game ID | Region | Revision | Redump SHA-1 of a clean dump |
|---|---|---|---|---|---|
| Mario Kart Wii | Mario Kart Wii | `RMCP01` | Europe (PAL) | 0 | n/a |
| Retro Rewind | the same Mario Kart Wii disc | `RMCP01` | Europe (PAL) | 0 | n/a |
| New Super Mario Bros. Wii | New Super Mario Bros. Wii | `SMNE01` | USA | **1** | `aa7b134a7606435ddcd735f2964ebdecde38c2ef` |
| MSMBWii: Just a Little Adventure | the same NSMB Wii disc | `SMNE01` | USA | **1** | `aa7b134a7606435ddcd735f2964ebdecde38c2ef` |
| Super Mario Sunshine | Super Mario Sunshine (GameCube) | `GMSE01` | USA | 0 | `8d094f2c5c112aba9660f0478b16c7f2caf63cbf` |
| Super Mario Eclipse 1.1.0 | the same Sunshine source disc; apply the included official patch | `GMSE01` source → `GMSE04` installed | USA source | 0 (source) | same clean Sunshine source hash above |
| Super Mario Galaxy 1 | Super Mario Galaxy (Wii), clean ISO | `RMGE01` | USA | **0** | `a36105c9fbfff6041dc5babf4318c6a216ad470a` |

For Eclipse, first import the unchanged supported Sunshine disc, then choose **Install Eclipse**.
WiiDS checks the source disc, bundled 1.1.0 patch and resulting image, and keeps Eclipse's image,
settings and saves separate from regular Sunshine. The exact supported prepatched Eclipse image
can also be imported; other images sharing the `GMSE04` header are not accepted as the same build.
If installation is cancelled, **Retry Eclipse installation** reuses the bundled patch.

**Not supported (yet):** the USA Mario Kart Wii disc (`RMCE01`), other regions, NSMB Wii revisions 0 and 2,
and PAL or Japanese Super Mario Sunshine.

**Supported disc-image formats:** for Galaxy, use the clean ISO described in the Quick start.
The table below covers the other games; see [Galaxy setup](docs/release/GALAXY-SETUP.md) for its alternatives.

| Format | Extension | Accepted |
|---|---|---|
| Plain disc image | `.iso`, `.gcm` | ✅ |
| Dolphin compressed | `.rvz` | ✅ |
| Wii backup | `.wbfs` (including split `.wbf1`) | ✅ |
| Other compressed images | `.wia`, `.gcz`, `.ciso` | ✅ |
| NKit | `.nkit.iso`, `.nkit.gcz` | ❌ Convert to RVZ or ISO in Dolphin first |

For split WBFS images, keep the `.wbfs` file and its `.wbf1`/later numbered parts in the same folder.
Grant storage access and use **Search again** in **Your games** so WiiDS finds all the sibling parts.
**Choose file** grants access to one selected file and does not collect the other parts.

**How to check your disc:** in Dolphin, the game list shows the Game ID. Right-click the game, choose
**Properties → Info**, and look at **Revision**.

## Included mods and presentation

All three mod integrations are included in the one APK:

- **Retro Rewind** (Mario Kart Wii): its native runtime and 398 rendered lower-screen maps are bundled.
  The actual track/content pack installs and updates in the app from Retro Rewind's official servers.
  Compatibility checks keep the pack aligned with the included runtime; cached maps still require
  the loaded course archive's matching identity and SHA-256.
- **MSMBWii: Just a Little Adventure** (NSMB Wii): its native runtime and level-pack archive are bundled.
  Mod installation starts automatically after importing the supported NSMB Wii disc, with its own
  save profile. Its card offers **Install mod** or **Retry mod installation** if it did not finish.
- **Super Mario Eclipse 1.1.0** (Sunshine): its native runtime, unchanged official VCDIFF patch and
  on-device installer are bundled. Apply the patch to your own supported Sunshine disc through
  **Your games → Install Eclipse**. No separate Eclipse app or downloaded game image is required.
  **Retry Eclipse installation** uses the bundled patch again after cancellation.

The complete selected presentation is also bundled: retail/fan covers and discs, printed case spines, Wii Menu music,
UI sounds, companion HUDs/fonts, menu/settings art, retail maps and the Retro Rewind map cache.
Its release manifest records each file's size and SHA-256. This presentation
does not replace the required game discs or Retro Rewind content installation. Existing valid user
overrides remain separate and preserved. See the [component notices](docs/release/WIIDS-NOTICE.md)
for mod and presentation credits and terms.

## Source and build information

The [1.0 release](https://github.com/Maddex712/WiiDS/releases/tag/v1.0.0) contains the APK, checksums and
matching `WiiDS-1.0-source.zip`. That named source ZIP includes the large presentation files omitted
from GitHub's automatic Source code download. The reviewed code is also in [source/](source/).
See the [build notes](docs/release/SOURCE-RELEASE.md), [release notes](docs/release/V1.0.md)
and [component credits](docs/release/WIIDS-NOTICE.md).

## Credits

WiiDS stands on the work of these projects and people:

- **DriftDroid** (dorPXP) and **WiiCompiled** (patchzyy): the Mario Kart Wii static recompilation and Android runtime
- **NSMB Wii recompilation** (WitherBlitz): the New Super Mario Bros. Wii runtime
- **sms-pc-port** and **sms-launcher** (Chase Myers / chasem-dev), on the **Super Mario Sunshine decompilation** (doldecomp)
- **Retro Rewind** (ZPL and the Retro Rewind team)
- **MSMBWii: Just a Little Adventure** (MidiGuyDP and Fredo), using **Another Super Mario Bros. Wii** code (Skawo, Treeki)
- **Super Mario Eclipse** (Eclipse Team / JoshuaMKW), **BetterSunshineEngine**, **BetterSunshineMoveset** and **SunshineHeaderInterface**
- **Some-Crappy-Edits**: Eclipse fan disc design; the selected Eclipse cover was supplied by the owner and its creator is unverified
- **GameTDB and its cover contributors**: the retail and community full-cover images used for printed
  case spines. The [game guide lists the source pages](docs/GAMES.md#case-artwork); MSMBWii is a custom adaptation.
- **Nintendo**: original game artwork, fonts/UI and the supplied Wii Menu recording; approved generated disc/app-logo artwork is identified in the component notices
- **Dolphin**, **SDL**, **aurora** and the other open-source libraries each project credits

The Mario Kart Wii work also lives in its own repository: [Maddex712/mkwii-thor](https://github.com/Maddex712/mkwii-thor).

## Legal

WiiDS is an unofficial fan project. It is not affiliated with, endorsed by or sponsored by Nintendo.
Mario Kart, Super Mario, New Super Mario Bros., Super Mario Sunshine, Wii and GameCube are trademarks of
Nintendo. No original or patched ROM/disc images, personal saves or private keys are included.
The release does include compiled native engines, the specified mod components and the selected
presentation assets described above. Code licences do not relicense third-party artwork or music.
Please own the games you play.

## Development assistance

Development assistance: OpenAI Codex. The owner-approved minimal app icon was generated with OpenAI's built-in image-generation tool. Game and mod creators are credited in the component notices.

### Galaxy and controller updates — October 22 development captures

These are genuine Thor captures from the separate-helper development build before
the one-APK integration. They illustrate the existing presentation, not validation of
the final ARM64 1.0 APK. Opening/Gateway checks do not establish full-game compatibility.

<p><img src="docs/images/galaxy-dev-gateway-upper.png" width="560" alt="October 22 pre-integration Thor development build: Luigi in Gateway Galaxy">
<img src="docs/images/galaxy-dev-gateway-lower.png" width="360" alt="Same development build: live lower-screen textured planet and Luigi marker"></p>
<p><img src="docs/images/wiids-dev-seven-covers.png" width="420" alt="October 22 development build: WiiDS Options companion with seven covers">
<img src="docs/images/nsmb-dev-controller.png" width="560" alt="October 22 development build: NSMB Controller tab with Thor button labels"></p>
