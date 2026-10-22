# WiiDS game guide

Seven library entries share one Android app. Galaxy needs Android API 32+; see its setup section below. Each needs the supported base disc listed below; no original
or patched game image ships in the APK. All use the Thor's physical controls. The lower screen is a
companion display, with no touch gameplay-button overlay.

The main shelf groups each original game with its mod: Mario Kart Wii/Retro Rewind,
NSMB Wii/MSMBWii, and Sunshine/Eclipse. Choose a family first, then choose the game inside it.
The printed cases and lower-screen readiness panel identify which version will launch.

For split WBFS imports, keep the `.wbfs` file and its `.wbf1`/later numbered parts together in the
same folder. Grant storage access and use **Search again** in **Your games**, which collects sibling
parts. **Choose file** opens only the single selected file.

## Mario Kart Wii

The original Wii racing game, with its normal courses, characters, items and race rules. The race runs
on the upper display. Below it, WiiDS draws a textured overhead course map, moving racer portraits,
standings, held items and the race timer using Mario Kart Wii's own HUD art and glyphs.

Import your **Europe/PAL `RMCP01`, revision 0** dump through **Your games**. The USA disc is not the
supported input for this build. There is no mod-content download for Original Mario Kart Wii.

## Retro Rewind

A community expansion for Mario Kart Wii, focused on its collection of recreated and custom courses.
It has its own case and launch mode. The lower screen keeps the Mario Kart layout, with a bundled
cache of 398 rendered course maps. A cached map is used only when its course identity and archive
hash match; a changed or unknown course uses the existing fallback instead of a misleading map.

First import the same **PAL `RMCP01`, revision 0** Mario Kart Wii disc. Then open **Retro Rewind
updates** in Options, or press **Y** in the library, and install the newest compatible release.
The native runtime and map cache ship in the APK; the actual track/content pack downloads from the
Retro Rewind team's official servers. A network connection and additional storage are required.

The updater checks the mod's compiled code against the runtime in WiiDS. A newer mod release that
changes that code needs a matching WiiDS build; selecting Update does not promise that every upstream
version can run. The currently supported code family covers Retro Rewind 6.13.0–6.13.1.

## New Super Mario Bros. Wii

The original side-scrolling Wii adventure. Its companion uses an NSMB2-style arrangement built from
the game's own art. In a course, it shows the course name and lives, a progress track ending at the
Bowser flag, a large Star Coin tray, score and a tall stored-item box. Coins and time stay on the
upper display. On the world map, the lower screen shows the route and Mario's position above linked
world bubbles. Press **R3** to drop the stored power-up above Mario. The course progress marker is an
approximation of horizontal position within the current zone, not an exact checkpoint indicator.

Import your **USA `SMNE01`, revision 1** dump. Revisions 0 and 2 are not interchangeable with this
runtime. Required disc metadata must come from your own dump.

## MSMBWii: Just a Little Adventure

MidiGuyDP and Fredo's level pack for New Super Mario Bros. Wii, with its own red case, custom courses
and separate save profile. It uses the same NSMB companion and stored-item system; starting this mod
does not replace your regular NSMB Wii save.
The [current Original NSMB Wii screenshots](../README.md#new-super-mario-bros-wii) show this shared
companion design; MSMBWii's current gallery image shows its separate case and readiness panel.

Import the **USA revision 1 NSMB Wii disc** above. Installation of the bundled MSMBWii mod starts
automatically after that import. If it was cancelled or did not finish, open MSMBWii's card in
**Your games** and choose **Install mod** or **Retry mod installation**. The authorised level-pack
archive and native runtime are bundled in WiiDS, so a separate companion APK or game-image download
is not needed.

## Super Mario Sunshine

Mario's GameCube adventure on Isle Delfino, running through the native Sunshine port. The lower
screen pairs a postcard with a live overhead area map built from the game's collision floor, with
a moving Mario marker. It also shows the area and episode, original-art Shine/blue-coin/coin counters
and episode Shine slots in the supported retail areas. The menu companion uses the game's live title
sky.

The beta.5 Original Sunshine presentation update gives the lower screen display-aligned motion,
moving clouds during gameplay and elapsed-time fades. Its map fit excludes ceilings, submerged
terrain and sparse remote components so the main Airstrip island fills the panel, while Mario's
marker uses the same world transform. This is an overview adaptation, not a complete survey of
every collision surface: the small remote Airstrip island is omitted. The upper MARIO label fits
the screen, and the stock life-meter animation keeps a fixed widescreen anchor. The complete
damage-meter animation has not been verified live. Eclipse is unchanged by this pass.

Import your **USA `GMSE01`, revision 0** dump. PAL and Japanese Sunshine images are not supported by
this build. Sunshine keeps its own data and save profile, separate from Eclipse.

## Super Mario Eclipse 1.1.0

Eclipse Team's Sunshine expansion, with added areas and its own adventure progression. Its black
GameCube case launches a separate native runtime and profile. Its companion uses the same live
collision-floor map, Mario marker, postcard/photo and original-art counters as Sunshine, with
Eclipse-specific handling for menus and custom-area context. Custom stages use their actual area
name or scene name and do not borrow unrelated retail episode Shine slots. The companion is not an
exhaustive guide to every Eclipse area.

First import the unchanged **USA `GMSE01`, revision 0** Sunshine disc, then choose **Install Eclipse**
in **Your games**. The unmodified official 1.1.0 patch and its installer are bundled. WiiDS verifies
the source disc, patch and resulting image before committing Eclipse's separate installation.
The exact supported prepatched image can also be imported; a matching game ID alone is insufficient.
If installation is cancelled, **Retry Eclipse installation** tries the bundled patch again.

Regular Sunshine's image, settings and saves stay separate. Existing standalone Sunshine or Eclipse
apps are not replaced, and their saves are not automatically copied into WiiDS.

## Display and testing notes

Original Sunshine's current Airstrip and title-sky images were captured in WiiDS 0.9.0-beta.5.
The unchanged launcher, NSMB and Eclipse images retain their beta.3 labels, along with Sunshine's
older file-selection and case captures. Mario Kart retains its existing capture context.
Every image keeps its actual capture-version label; selected scene captures do not establish
animation cadence or a complete playthrough.

1080p and a 60 FPS target are the normal starting settings. The optional 120 FPS modes are experimental.
For the recompiled Wii games, 120 FPS interpolates presentation while game logic remains at 60 Hz.
The 120 setting is not a promise of sustained performance on every scene or game.

Release checks cover selected launches, menu transitions and representative companion states. They
do not constitute a complete playthrough or a course-by-course test. Audible audio, haptics, physical
play feel and long-session performance still benefit from player feedback.

## Case artwork

Case plastic follows the shelf's hardware style: white for Mario Kart Wii/Retro Rewind, red for
NSMB Wii/MSMBWii, black for Sunshine/Eclipse. Printed spines use the following full-cover sources:

| Case | Spine source |
|---|---|
| Mario Kart Wii | [GameTDB retail cover scan](https://www.gametdb.com/Wii/RMCE01) |
| New Super Mario Bros. Wii | [GameTDB retail cover scan](https://www.gametdb.com/Wii/SMNE01) |
| Super Mario Sunshine | [GameTDB retail cover scan](https://www.gametdb.com/Wii/GMSE01) |
| Retro Rewind | [GameTDB community cover](https://www.gametdb.com/Wii/RMCETO) |
| Super Mario Eclipse | [GameTDB community cover](https://www.gametdb.com/Wii/GMSE04) |
| MSMBWii | Custom adaptation of the NSMB Wii scanned spine and the selected mod title artwork |

These are image crops applied to the projected case spine, rather than text labels standing in for
the printed packaging. Community/custom covers are not Nintendo retail releases. The Mario Kart
spine scan is USA artwork; that presentation choice does not change the PAL game-input requirement.
Nintendo and the respective fan-art creators retain their artwork rights. See the
[component notices](release/WIIDS-NOTICE.md) for the other presentation and runtime credits.

## Super Mario Galaxy 1

The Galaxy-Android/Petari native runtime is integrated into WiiDS, with a separate
package-internal game process, NAND, settings and import profile. Bring your USA
RMGE01 revision 0 disc. The setup card extracts/converts it and generates its lower
HUD/fonts, Gardens map and textured planets. No helper APK or PC converter is needed.
See [full setup, migration and limits](release/GALAXY-SETUP.md).

The live lower screen uses matched actor visibility/base matrices and a Mario/Luigi
marker. It omits animated enemies/NPC/player models and has material/coverage gaps.
The current integration has desktop/Android emulator validation, with no physical
Thor run of the final ARM64 APK. Earlier Thor development captures are labelled.
No all-stage/performance/audio/haptics guarantee is made. Galaxy 2 is unsupported.

## Controller remapping in 1.0

WiiDS Options > Controller buttons explains each game's remapping page. Galaxy:
Back > Controls > Buttons. NSMB/MSMBWii: Settings > Controller. Sunshine/Eclipse:
their existing controller config, now reachable from WiiDS. Mario Kart/Retro Rewind:
Back > Controller; their prebuilt page still uses Xbox position labels.
NSMB rebinding was checked on the earlier Thor build; MSMBWii shares its code but
was not independently checked on Thor.
