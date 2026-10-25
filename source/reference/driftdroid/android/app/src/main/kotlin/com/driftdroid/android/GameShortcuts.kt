package com.driftdroid.android

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.pm.ShortcutInfo
import android.content.pm.ShortcutManager
import android.graphics.Bitmap
import android.graphics.drawable.Icon
import android.widget.Toast
import java.io.File

/** User-requested pinned shortcuts; all launches use the same gate as a frontend request. */
internal object GameShortcuts {
    fun show(activity: Activity) {
        val games = FrontendGame.values().filter {
            it != FrontendGame.RETRO_REWIND || BuildConfig.THOR_RETRO_REWIND
        }
        ThorDialogs.builder(activity)
            .setTitle("Game shortcuts")
            .setItems(games.map { it.label }.toTypedArray()) { _, which ->
                val game = games[which]
                ThorDialogs.builder(activity)
                    .setTitle(game.label)
                    .setMessage("Add a shortcut that opens this game directly. Import and set up the game in WiiDS first. " +
                        "For Cocoon, set it as your default launcher, then add the shortcut there.")
                    .setPositiveButton("Add shortcut") { _, _ -> request(activity, game) }
                    .setNegativeButton("Back") { _, _ -> show(activity) }
                    .show()
            }
            .setNegativeButton("Back", null)
            .show()
    }

    /** No ROM URI or transient permission is needed after the game has been imported. */
    internal fun create(context: Context, game: FrontendGame, icon: Icon): ShortcutInfo =
        ShortcutInfo.Builder(context, "wiids.play.${game.id}")
            .setShortLabel(game.label)
            .setLongLabel("Play ${game.label} in WiiDS")
            .setIcon(icon)
            .setIntent(Intent(context, FrontendLaunchActivity::class.java)
                .setAction(FrontendLaunchActivity.ACTION_PLAY)
                .putExtra("game", game.id))
            .build()

    internal fun request(activity: Activity, game: FrontendGame) {
        val manager = activity.getSystemService(ShortcutManager::class.java)
        if (manager == null || !manager.isRequestPinShortcutSupported) {
            message(activity, "Your default launcher does not support adding shortcuts. Choose Cocoon or another supported launcher as the default, then try again.")
            return
        }
        // Existing artwork is validated and may need disk reads; keep that off the UI thread.
        Thread {
            val shortcut = runCatching {
                val folder = ThorLauncherFolders.cases(activity, File(activity.filesDir, "WiiCompiled/ThorMap"))
                val art = ThorLauncherArtwork.bitmap(folder, "${game.artwork}-disc")
                val icon = if (art != null) Icon.createWithBitmap(Bitmap.createScaledBitmap(art, 192, 192, true))
                    else Icon.createWithResource(activity, R.mipmap.ic_launcher)
                create(activity, game, icon)
            }
            activity.runOnUiThread {
                if (activity.isFinishing || activity.isDestroyed) return@runOnUiThread
                shortcut.onSuccess { info ->
                    runCatching { manager.requestPinShortcut(info, null) }
                        .onSuccess { accepted ->
                            message(activity, if (accepted) "Finish adding ${game.label} in your launcher."
                                else "Your launcher did not accept the shortcut. Try again from WiiDS.")
                        }
                        .onFailure { message(activity, "The shortcut could not be requested. Try again while WiiDS is open.") }
                }.onFailure { message(activity, "The shortcut could not be created. Try again from WiiDS.") }
            }
        }.apply { name = "WiiDsShortcut"; isDaemon = true }.start()
    }

    private fun message(activity: Activity, text: String) = Toast.makeText(activity, text, Toast.LENGTH_LONG).show()
}
