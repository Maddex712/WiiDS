# Game shortcuts and frontend launching in WiiDS 1.1

Import and finish setting up each game in WiiDS first. A shortcut opens its existing
game/profile; it does not import or patch the game automatically.

## Add a shortcut to Cocoon or your home screen

1. In WiiDS, open **Options > Game shortcuts**.
2. Choose the game or mod, then **Add shortcut**.
3. Accept the pin request in your launcher and choose its placement.

Make Cocoon your default Android launcher before adding a Cocoon shortcut. Other
launchers can accept Android pinned shortcuts too. If yours does not support
them, WiiDS explains the requirement without changing your default launcher.

Each shortcut uses the game's name and disc art where available. Retro Rewind,
MSMBWii, Eclipse and Kaizo shortcuts explicitly select their mod profiles.
All eight standard game/mod profiles are available. No extra APK or copied ROM
is needed for a pin.

## Resuming and switching games

Opening the same game's shortcut brings its retained task forward when available.
WiiDS refuses to switch into a different game while another embedded native game
is running. Return normally to the WiiDS library before starting another game.
If the retained task is unavailable, use Recents or return to the library.

Exiting a game returns to the WiiDS library. Automatic return directly to Cocoon
is not implemented. Android Back from the library can return to the previous
frontend.

Pinned Retro Rewind and NSMB launches were checked in Cocoon 3.07 on the Thor.
The existing native Mario Kart/Retro Rewind activities were also checked for
retention through Home/shortcut return. This does not cover every launcher or
every game stage.

## Explicit launch interface

Frontend authors can target component
<code>io.github.maddex712.wiids/com.driftdroid.android.FrontendLaunchActivity</code>.

Use action <code>io.github.maddex712.wiids.action.PLAY</code> with a string extra
named <code>game</code>. Supported values are <code>mkw</code>,
<code>retro-rewind</code>, <code>nsmbw</code>, <code>msmbwii</code>,
<code>sunshine</code>, <code>eclipse</code>, <code>galaxy</code> and <code>kaizo</code>.

Alternatively, send <code>android.intent.action.VIEW</code> with a URI such as
<code>wiids://play/nsmbw</code>. Unknown or conflicting game IDs are refused.
Setup and running-game checks apply to both entry points; native activities remain private.

References: [Cocoon shortcut support](https://cocoon-shell.com/news/cocoon-2-1-beta/)
and [Android pinned shortcuts](https://developer.android.com/develop/ui/views/launch/shortcuts/creating-shortcuts).
