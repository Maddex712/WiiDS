# WiiDS achievements (local preview)

## Local Casual client work (not installed)

See [the Casual client status](RA_CASUAL_CLIENT.md). A newer RA moderator
clarification permits existing console sets in Casual mode without approval.
The previous blanket exclusion below is historical and too broad. The NSMB client
prototype builds and passes mock submission tests, but live earning is blocked
by the current revision 1 runtime versus RA's supported revision 2 discs.
The Thor still runs the read-only 1.1-dev.7 preview. No real unlock was submitted.

## 1.1-dev.6: RetroAchievements sets for every game (current)

WiiDS's achievements are now each game's own **RetroAchievements sets**. Connect
your RA account once, and everything you have already earned (in Dolphin or on
real hardware) shows as completed. This preview stays local; the public release
is unchanged.

- **Installed preview.** Version 1.1-dev.7 reads account progress and does not
  submit unlocks. Console rules need the matching disc revision and memory layout;
  the new Casual client work is documented separately above.
- **Which sets.** WiiDS finds them by RA's own titles through the official
  `API_GetGameList` (Wii and GameCube lists, cached for a week):
  - **Base games:** the base set plus every "[Subset - …]" set, for Mario Kart Wii,
    New Super Mario Bros. Wii, Super Mario Galaxy (not Galaxy 2) and Super Mario
    Sunshine.
  - **Mods:** only a "~Hack~" set named for that mod (Retro Rewind, MSMBWii, Super
    Mario Eclipse, Kaizo Mario Galaxy). A mod never shows its base game's achievements.
  - **No set:** a game RA has no set for shows "No RetroAchievements set" and is
    checked again on every refresh.
- **Progress.** One `API_GetGameInfoAndUserProgress` request per set, 350 ms apart.
  Shown for each achievement: earned date, Hardcore mark, points and author.
  - **When it refreshes:** at most once a day when you open WiiDS, or with
    **Refresh** (Y in the browser; at most once a minute).
  - **Failed refresh:** keeps the previous copy.
  - **Changing accounts or disconnecting:** removes the old account's saved progress
    and badge pictures.
- **Connection.** RA username (or ULID) plus the Web API key from your RA control
  panel. The key is encrypted with Android Keystore; the form blocks screenshots.
- **Badges.** RA's own pictures from `https://media.retroachievements.org/Badge/<BadgeName>.png`:
  - **Rules:** fetched after a refresh, no credentials, no redirects, PNG only,
    at most 64 KB and 16 to 256 px, up to 3000 kept.
  - **Speed (1.1-dev.7):** six downloads at once over reused HTTPS connections. Pictures
    appear on screen in batches of 60 as they arrive. Measured on the Thor: 300 badges in 16 s
    (18.5/s, about 4× the first version), so a full first load of ~1,100 badges takes about a minute.
  - **Look:** gold frame when earned; greyed with a padlock when locked.
  - **Not fetched yet:** a WiiDS trophy is shown instead.
- **Where they appear.**
  - **Lower library screen:** for the selected case, the earned count across its
    sets, recent unlocks and what's still to earn. Touch it or press Y to open the
    browser.
  - **The browser:** reached from Options > Achievements (the last selected game),
    or from Back > Achievements inside NSMB/MSMBWii.
    - Achievements are grouped by set.
    - Filters: L/R. Switch game: ZL/ZR or the arrows.
    - Y connects or refreshes.
    - The lower screen shows the focused achievement in full.
- **Removed.** The four WiiDS-made NSMB achievements and their in-game unlock
  popup are no longer recorded or shown. Their old ledger files are left untouched
  on the device. The popup presenter stays in the source for any future
  WiiDS-detected achievements.

---

Earlier previews (1.1-dev.4/5), kept for reference:

## Where achievements appear (1.1-dev.5)

- **Lower screen while browsing cases.** The selected game's disc sits on the left
  with its readiness. On the right are its save file and earned count, up to two
  recent unlocks (newest first), the next locked challenges in the set's order, and
  **View all**. Touch a card or View all, or press **Y** (every case except Mario
  Kart Wii and Retro Rewind, where Y stays Updates). Games without a WiiDS set
  (everything except original NSMB Wii, including MSMBWii) show their disc and a
  "No WiiDS achievements yet" panel. The preview follows the exact selected case or
  expanded mod; a late read for a case already scrolled past is discarded.
- **Default save file.** The preview shows the save file with the most recent
  unlock and names it ("Save file 2"; the NAND profile is added only when there are
  several). Before any unlock it says progress is kept per save file.
- **Achievement browser** (Options > Achievements, View all, Y, or NSMB's Back
  menu). It uses the launcher's own chrome, font and gold selection. The upper
  screen holds the badge grid, All/Earned/Locked filters (L/R), WiiDS or
  RetroAchievements history (X) and the save file (Y). The lower screen shows the
  focused achievement in full: picture, state, date, the whole description and
  source. RA entries add author, RA points and Hardcore provenance, always labelled
  "Earned on RetroAchievements". Without a lower screen, A shows the same details
  over the grid. B returns exactly where you were (case, expanded mod or Options row).
- **In-game popup.** A compact "Achievement unlocked" card with the badge and name
  appears top-centre between NSMB's coin counter and timer for about 4 seconds. It
  shows only after the ledger records a new unlock, one at a time in order, never
  repeats an ID, and never takes focus or touches. Pause freezes it, and leaving the
  game clears anything still queued.

**Badges.** WiiDS medallions frame pictures already extracted on this device from
your own NSMB disc (nsmbw/ThorHud, hash-checked against its manifest):
- Star Coin Found: the Star Coin.
- Three of a Kind: three Star Coins.
- First Steps: World 1-1's course picture.
- A Fresh Exit: the results-screen Mario over the World 1 panorama.

Nothing from the disc is in the APK. If that art is missing, drawn WiiDS glyphs
are used instead. Earned badges have gold rims and full colour; locked badges are
greyed, dimmed and padlocked. Imported RA history uses RA's own badge pictures.
These are fetched only during a manual import, from
`https://media.retroachievements.org/Badge/<BadgeName>.png`, with no credentials,
no redirects, PNG only, at most 64 KB and 16 to 256 px, and at most 600 kept.
Badges not yet fetched (offline) show a WiiDS trophy instead. Disconnect removes
the pictures with the history. RA snapshots written before badge support remain
valid.

## First WiiDS set

| Achievement | Condition |
|---|---|
| A Fresh Exit | Record a previously uncleared course exit in solo play. |
| First Steps | Record a previously uncleared World 1-1 exit in solo play. |
| Star Coin Found | Pick up a Star Coin during solo play. |
| Three of a Kind | Pick up a missing Star Coin so all three are held in the current course. |

The observer requires normal solo play, an initialized stage/player and a valid
selected save. LAN, multiplayer, title/demo and Super Guide samples cannot earn
these achievements. First observations establish a baseline; an old completed
save or restored checkpoint cannot itself trigger an unlock. The first two rules
track changes in recorded exit flags, so repeating an already recorded exit does
not qualify. Star Coin pickups can qualify on replayed courses. Current checkpoint
coins may contribute to the three held coins, but a new pickup must occur.

Unlocks have stable versioned IDs and original earned dates. Repeated events do
not duplicate an unlock or its notification. Progress is separate for each Wii
save slot and resolved NAND profile. The first set applies to original NSMB only;
MSMBWii and the other games have no local set yet. Achievement files are separate
from the original game saves, which this system does not write.

## Import RetroAchievements history

1. Choose **Show RetroAchievements history > Connect RetroAchievements**.
2. Enter the RA username (or stable account ULID) and that account's **Web API
   key** from its RA account control panel. This is not an emulator login token;
   WiiDS does not request the account password.
3. Choose **Connect and import**. Later imports are manual, with a minute between
   attempts. There is no background polling.

The connection key is encrypted with Android Keystore and kept in app-private,
non-backup storage. The connection dialog blocks screenshots. The NSMB base set
is fixed to [RA game 95](https://retroachievements.org/game/95); it is not chosen
from a filename or a user-supplied endpoint. Requests use HTTPS to the official
Web API, reject redirects and oversized/invalid replies, and never log the key.

The history screen retains achievement titles, descriptions, authors, RA points,
earned dates and Hardcore provenance. Imported completions say **Earned on
RetroAchievements**. They do not grant WiiDS unlocks, submit new RA unlocks or
change RA points/rankings. No mappings between individual RA and WiiDS rules have
been reviewed in this preview. A successful refresh replaces that account's RA
snapshot, including RA progress resets. A failed refresh retains the prior cache.
Switching accounts removes the prior account's cache. Disconnect removes the
current connection/history and preserves local WiiDS achievements.

Official RA port support remains subject to its
[standalone policy](https://docs.retroachievements.org/general/standalone-support.html).
The history import uses its documented
[game/user progress endpoint](https://api-docs.retroachievements.org/v1/get-game-info-and-user-progress.html).

## Verification and remaining checks

- 54 production Java ledger/rule/parser/cache checks, including concurrent writes,
  restarts, account/slot isolation, resets and preserving damaged data.
- 90 ARM64 checks with the actual snapshot/event source and read-only mock guest
  memory, including the earlier 73 snapshot checks. Pickup and new-exit events,
  world-map transitions, initial baselines and excluded play modes are covered.
- 15 Android Keystore/storage/session checks in a separate synthetic test package;
  history UI and safe import failure inspected. Test progress never enters WiiDS.

Synthetic checks do not prove a real in-game unlock or a live account import.
Those require actual play and an account/key entered privately on the device.
The native observer captures on the game thread; Android reads an immutable event
mailbox and does not read guest RAM. Its bounded 256-event queue exposes sequence gaps; it
cannot reconstruct missed events after a terminated game process.

Run source checks with `experiments/achievements/test_android_sources.py --serial
YOUR_EXPLICIT_ADB_SERIAL`. The Android test application under `android-test/` has
its own package/storage and no Internet permission. It is never part of the
production APK. Source patches and private verification receipts live under
`build/achievements-nsmb/`.
