package com.driftdroid.android.rom

import android.content.Context
import java.io.File

/**
 * Every WiiDS game, the exact disc it needs and where its imported data lives. A game is
 * installed once its data is complete: main.dol (written last) for Wii, the renamed ISO for
 * GameCube. The required discs match the pins in the game runtimes, which refuse anything else.
 */
object GameCatalog {
    enum class Disc(
        val gameId: String,
        val revision: Int,
        val platform: WiiDsDisc.Platform,
        val gameName: String,
        val regionName: String,
    ) {
        MARIO_KART_PAL("RMCP01", 0, WiiDsDisc.Platform.WII, "Mario Kart Wii", "Europe (PAL)"),
        NSMB_USA_REV1("SMNE01", 1, WiiDsDisc.Platform.WII, "New Super Mario Bros. Wii", "USA"),
        SUNSHINE_USA("GMSE01", 0, WiiDsDisc.Platform.GAMECUBE, "Super Mario Sunshine", "USA"),
        ECLIPSE_110("GMSE04", 0, WiiDsDisc.Platform.GAMECUBE, "Super Mario Eclipse 1.1.0", "Patched USA");

        val requirement: String
            get() = "$gameName, $regionName ${if (platform == WiiDsDisc.Platform.GAMECUBE) "GameCube " else ""}disc, " +
                "game ID $gameId, revision $revision"

        /** Same game, any region or revision (the first three letters of the game ID). */
        fun sameGame(info: WiiDsDisc.Info) = info.gameId.take(3) == gameId.take(3)

        fun matches(info: WiiDsDisc.Info) = !info.isNkit && info.gameId == gameId && info.revision == revision &&
            info.disc == 0 && info.platform == platform

        /** Why a disc of the same game can't be used, in plain words; null when it matches. */
        fun mismatch(info: WiiDsDisc.Info): String? = when {
            matches(info) -> null
            info.isNkit -> "It's an NKit image, which isn't a complete copy of the disc. Convert it to RVZ or ISO " +
                "in Dolphin (right-click the game, Convert File) and search again."
            info.gameId != gameId -> "It's the ${info.region} release (${info.gameId}). WiiDS needs the $regionName " +
                "release, $gameId."
            info.revision != revision -> "It's revision ${info.revision}. WiiDS needs revision $revision of $gameId."
            else -> "It's disc ${info.disc + 1} of a set. WiiDS needs disc 1."
        }

        fun installDir(context: Context): File = when (this) {
            MARIO_KART_PAL -> File(context.filesDir, "WiiCompiled/DiscData")
            NSMB_USA_REV1 -> File(context.filesDir, "games/nsmbw")
            SUNSHINE_USA -> File(context.filesDir, "games/sms")
            ECLIPSE_110 -> File(context.filesDir, "games/eclipse")
        }

        /** The finished-install marker. */
        fun installedFile(context: Context): File = when (platform) {
            WiiDsDisc.Platform.WII -> File(installDir(context), "sys/main.dol")
            WiiDsDisc.Platform.GAMECUBE -> File(installDir(context), "game.iso")
        }

        fun isInstalled(context: Context) = installedFile(context).isFile
    }

    enum class Game(val title: String, val disc: Disc, val note: String? = null) {
        MARIO_KART("Mario Kart Wii", Disc.MARIO_KART_PAL),
        NSMB("New Super Mario Bros. Wii", Disc.NSMB_USA_REV1),
        MSMBWII("MSMBWii: Just a Little Adventure", Disc.NSMB_USA_REV1,
            "Plays MidiGuyDP's free level mod on your New Super Mario Bros. Wii disc."),
        SUNSHINE("Super Mario Sunshine", Disc.SUNSHINE_USA),
        ECLIPSE("Super Mario Eclipse", Disc.ECLIPSE_110,
            "Import your USA Sunshine revision 0 disc first, then install Eclipse Team's included 1.1.0 patch here. An already patched GMSE04 image also works. Original Sunshine and Eclipse keep separate saves."),
    }

    /**
     * MSMBWii's files (riivolution/ and msmbwiijala/), beside the NSMB disc data: the runtime
     * reads them from <disc root>/../mod.
     */
    fun msmbwiiModDir(context: Context): File = File(context.filesDir, "games/mod")

    fun msmbwiiModInstalled(context: Context) =
        File(msmbwiiModDir(context), "msmbwiijala/AnotherRes/SystemUS_1.bin").isFile &&
            File(msmbwiiModDir(context), "riivolution").isDirectory

    fun isPlayable(context: Context, game: Game) =
        game.disc.isInstalled(context) && (game != Game.MSMBWII || msmbwiiModInstalled(context))

    const val ACCEPTED_FORMATS = "ISO, WBFS (one file or split), RVZ, WIA, GCZ or CISO. NKit images aren't accepted."
}
