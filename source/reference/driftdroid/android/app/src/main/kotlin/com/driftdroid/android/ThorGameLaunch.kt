package com.driftdroid.android

import android.app.Activity
import android.content.ActivityNotFoundException
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import com.driftdroid.android.rom.GameCatalog

/**
 * Starts the other WiiDS games. They ship inside this app, each in its own processes (see the
 * manifest); "installed" means the player has imported the game's disc on the Games screen.
 */
internal object ThorGameLaunch {
    const val NSMB_WII = "nsmbw"
    const val SUNSHINE = "sunshine"
    const val ECLIPSE = "eclipse"
    /** MSMBWii ("Just a Little Adventure"): New Super Mario Bros. Wii with MidiGuyDP's level mod. */
    const val MSMBWII = "msmbwii"
    private const val ACTION_PLAY = "dev.thor.launcher.action.PLAY_GAME"
    private const val NSMB_ENTRY = "com.wither.nsmbw.ThorLaunchActivity"
    private const val MSMBWII_ENTRY = "com.wither.nsmbw.MsmbwiiActivities\$Launch"
    private const val SUNSHINE_ENTRY = "com.wither.sms.ThorLaunchActivity"
    private const val ECLIPSE_ENTRY = "dev.chasem.eclipselauncher.ThorLaunchActivity"

    enum class Result { OPENED, NOT_INSTALLED, UNAVAILABLE }

    fun nsmbInstalled(context: Context): Boolean = GameCatalog.isPlayable(context, GameCatalog.Game.NSMB)

    fun sunshineInstalled(context: Context): Boolean = GameCatalog.isPlayable(context, GameCatalog.Game.SUNSHINE)

    fun msmbwiiInstalled(context: Context): Boolean = GameCatalog.isPlayable(context, GameCatalog.Game.MSMBWII)

    /** The embedded Eclipse runtime has a separately imported disc and save profile. */
    fun eclipseInstalled(context: Context): Boolean = GameCatalog.isPlayable(context, GameCatalog.Game.ECLIPSE)

    fun launchNsmb(activity: Activity): Result = launch(activity, NSMB_ENTRY, NSMB_WII, nsmbInstalled(activity))

    fun launchSunshine(activity: Activity): Result = launch(activity, SUNSHINE_ENTRY, SUNSHINE, sunshineInstalled(activity))

    fun launchMsmbwii(activity: Activity): Result = launch(activity, MSMBWII_ENTRY, MSMBWII, msmbwiiInstalled(activity))

    fun launchEclipse(activity: Activity): Result = launch(activity, ECLIPSE_ENTRY, ECLIPSE,
        eclipseInstalled(activity))

    private fun launch(activity: Activity, entry: String, game: String, ready: Boolean,
                       targetPackage: String = activity.packageName): Result {
        if (!ready) return Result.NOT_INSTALLED
        return try {
            activity.startActivity(Intent(ACTION_PLAY).setComponent(ComponentName(targetPackage, entry))
                .putExtra("thor_protocol", 1)
                .putExtra("thor_game", game)
                .putExtra("thor_launcher_package", activity.packageName)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP))
            Result.OPENED
        } catch (_: ActivityNotFoundException) {
            Result.UNAVAILABLE
        } catch (_: SecurityException) {
            Result.UNAVAILABLE
        }
    }
}
