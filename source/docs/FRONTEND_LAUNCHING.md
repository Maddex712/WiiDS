# Direct frontend launching (local 1.1 development)

This interface is implemented in the local development source. It is absent
from published WiiDS 1.0. An October 24 Thor pass verified a pinned Retro Rewind
shortcut in Cocoon 3.07: it survived an in-place WiiDS update, loaded
`libRetroRewind.so` and reached the mod's existing license selection.

Import and prepare each game in WiiDS first. A frontend request opens the
existing game/profile; it does not import or patch a disc automatically.
Return from an active game to its WiiDS library before starting another game.
The public entry point rejects changing games while another embedded native game
process is alive, preserving its in-progress save and runtime. A pin or deep link
for the same game can bring its existing game task forward. It never creates a
second SDL Activity inside a stale process; if that task is unavailable, use
Recents or return to the library. Disc-file requests remain refused during a game.

## Add a game shortcut to Cocoon or your home screen

In WiiDS, open **Options > Game shortcuts**, choose **Retro Rewind** (or another
game), then choose **Add shortcut**. The fallback library also has a Game shortcuts
button. Import and finish setting up that game or mod in WiiDS first.

Cocoon supports Android's Add Shortcut requests when it is the default launcher.
Accept the request in Cocoon and choose its placement there. Other Android
launchers can also accept these shortcuts. If the default launcher does not
support the feature, WiiDS explains how to choose a supported launcher; it does
not change the default launcher for you.

Each shortcut has a stable per-game ID, the game's name and its existing disc art
when available (otherwise the WiiDS icon). It opens the existing frontend entry
point with an explicit game ID. A Retro Rewind shortcut selects Retro Rewind,
not the original Mario Kart Wii profile. No original disc URI, extra APK, copied
ROM, or custom Cocoon player configuration is needed for a pinned shortcut.
Setup, runtime, update and profile checks still apply each time it is opened.
Android accepting a pin request does not prove that the user placed the shortcut.

The shortcut source and local Android compilation are verified separately from
physical Cocoon acceptance. The Retro Rewind path above is verified; other
launchers, all eight shortcuts and full gameplay are not covered by that check. Leaving a game
currently returns to the WiiDS library; automatic return straight to Cocoon is
not implemented by this shortcut change.

The NSMB pin also survived an update and cold-launched the correct native engine.
Dev14's task-only resume returned to the same NSMB PID without the earlier
unnecessary running-game warning. The selector has 93 actual-policy checks,
including different mods, foreign/stale/missing tasks and multiple live engines.

References: [Cocoon's app shortcut support](https://cocoon-shell.com/news/cocoon-2-1-beta/),
[Android pinned shortcuts](https://developer.android.com/develop/ui/views/launch/shortcuts/creating-shortcuts).

## Choose a specific game

Send an explicit intent to
`io.github.maddex712.wiids/com.driftdroid.android.FrontendLaunchActivity`:

```text
action: io.github.maddex712.wiids.action.PLAY
string extra game: nsmbw
```

IDs: `mkw`, `retro-rewind`, `nsmbw`, `msmbwii`, `sunshine`, `eclipse`,
`galaxy`, `kaizo`. Indices from the carousel are not part of the protocol.

Alternatively, send `android.intent.action.VIEW` with `wiids://play/nsmbw`.
Unknown or conflicting IDs are rejected. The individual native activities
remain private.

## Choose from a disc library

Use the same explicit component, action `android.intent.action.VIEW`, and the
disc's `file://` or `content://` URI as intent data. For a content URI, grant
read permission. WiiDS uses its disc reader to check actual game identity,
region and revision; renaming a file never changes its target. ISO/GCM, WBFS,
RVZ, WIA, GCZ and CISO use the existing reader. Split WBFS files must remain
together and must be filesystem-accessible; a single content URI cannot grant
access to its sibling parts.

Original discs select original games. To select Retro Rewind or MSMBWii, also
send the corresponding `game` string extra. The supplied disc must still match
the base game. Kaizo similarly requires an explicit selection and its own
completed setup. Original and mod saves stay in their existing separate profiles.

## Cocoon

Cocoon's public Wii/GameCube definitions use `playerList` entries with
`amStartArguments` and `{file.uri}`. Candidate WiiDS entries are in
[`wiids-players.json`](../experiments/frontend/wiids-players.json).
Merge the relevant entry into a custom platform/player configuration; the file
contains player entries, not a complete platform. It is not yet included in
Cocoon's official player list. No Cocoon configuration on the owner's device
has been replaced.

Example command after importing NSMB Wii:

```powershell
adb shell am start -n io.github.maddex712.wiids/com.driftdroid.android.FrontendLaunchActivity -a io.github.maddex712.wiids.action.PLAY --es game nsmbw
```

The current native runtimes return to the WiiDS library; Android Back from the
library can return to the previous frontend. Exact focus/display behavior and
repeated launches must be verified on the Thor before calling Cocoon supported.

Sources: [Cocoon Wii player schema](https://raw.githubusercontent.com/inssekt/CocoonFE/main/platforms/NintendoWii.json),
[GameCube player schema](https://raw.githubusercontent.com/inssekt/CocoonFE/main/platforms/GameCube.json).
