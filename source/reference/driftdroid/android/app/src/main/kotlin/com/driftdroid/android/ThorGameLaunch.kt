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
    const val GALAXY = "galaxy"
    const val KAIZO = "kaizo"
    /** Super Smash Bros. Brawl uses its own runtime process and save profile inside WiiDS. */
    const val BRAWL = "brawl"
    private const val BRAWL_ENTRY = "dev.wiids.brawl.ThorLaunchActivity"
    private const val GALAXY_ENTRY = "dev.thor.galaxy.ThorLaunchActivity"
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

    /** The isolated development runtime reports only its own verified data readiness. */
    fun galaxyInstalled(context: Context): Boolean = android.os.Build.VERSION.SDK_INT >= 32 && dev.thor.galaxy.GalaxyStorage.ready(context)

    fun launchGalaxy(activity: Activity): Result = launch(activity, GALAXY_ENTRY, GALAXY,
        galaxyInstalled(activity))

    fun kaizoInstalled(context: Context): Boolean = android.os.Build.VERSION.SDK_INT >= 32 && dev.thor.galaxy.GalaxyStorage.kaizoReady(context)

    fun launchKaizo(activity: Activity): Result = launch(activity, "dev.thor.galaxy.KaizoActivities\$Launch", KAIZO, kaizoInstalled(activity))

    /** Brawl has its own imported disc and art inside WiiDS. */
    fun brawlInstalled(context: Context): Boolean = dev.wiids.brawl.BrawlStorage.ready(context) && dev.wiids.brawl.BrawlArt.ready(context)
    fun launchBrawl(activity: Activity): Result = launch(activity,BRAWL_ENTRY,BRAWL,brawlInstalled(activity))

    fun launchNsmb(activity: Activity): Result = launch(activity, NSMB_ENTRY, NSMB_WII, nsmbInstalled(activity))

    fun launchSunshine(activity: Activity): Result = launch(activity, SUNSHINE_ENTRY, SUNSHINE, sunshineInstalled(activity))

    fun launchMsmbwii(activity: Activity): Result = launch(activity, MSMBWII_ENTRY, MSMBWII, msmbwiiInstalled(activity))

    fun launchEclipse(activity: Activity): Result = launch(activity, ECLIPSE_ENTRY, ECLIPSE,
        eclipseInstalled(activity))

    private fun launch(activity: Activity, entry: String, game: String, ready: Boolean,
                       targetPackage: String = activity.packageName): Result {
        SaveManagerProfiles.blocker(activity)?.let {
            android.widget.Toast.makeText(activity, it, android.widget.Toast.LENGTH_LONG).show()
            return Result.UNAVAILABLE
        }
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
