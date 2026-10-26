package com.driftdroid.android.rom

import android.content.Context
import java.io.File

/**
 * Copies one disc into the app (one at a time, on its own thread). The data is written into a
 * temporary folder and swapped in only when complete, so a cancelled or failed import never
 * leaves a half-installed game. The player's own file is only read.
 */
object GameInstaller {
    data class State(
        val disc: GameCatalog.Disc? = null,
        val running: Boolean = false,
        val done: Long = 0,
        val total: Long = 0,
        /** Set once finished: null = success. */
        val error: String? = null,
        val finished: Boolean = false,
    )

    @Volatile var state = State()
        private set

    val running get() = state.running
    @Volatile private var importCancelled = false

    fun start(context: Context, disc: GameCatalog.Disc, source: WiiDsDisc.Source): Boolean {
        synchronized(this) {
            if (state.running || EclipseMod.state.running || com.driftdroid.android.WiiDsUpdater.installing.get()) return false
            // Before Cancel is shown: a tap from here on must reach the extraction.
            WiiDsDisc.resetCancel()
            importCancelled = false
            state = State(disc, running = true)
        }
        val app = context.applicationContext
        Thread {
            val error = runCatching { install(app, disc, source) }.getOrElse { it.message ?: "The import failed." }
            state = state.copy(running = false, finished = true, error = error)
            // The game's art (menus, sounds, maps, cases) comes from the disc just imported, in
            // the background; a failure leaves the game playable and is retried later.
            if (error == null) repairArt(app)
        }.apply { name = "WiiDsInstall"; isDaemon = true }.start()
        return true
    }

    fun cancel() { if (state.running) { importCancelled = true; WiiDsDisc.cancel() } }

    /** Rebuilds missing art for imported games (for example after an update), off the UI thread. */
    fun repairArt(context: Context) {
        val app = context.applicationContext
        Thread {
            for (disc in GameCatalog.Disc.values()) {
                if (state.running) return@Thread
                if (WiiDsArt.missing(app, disc)) WiiDsArt.build(app, disc, onlyMissing = true)
            }
            // The included MSMBWii mod installs itself once the NSMB disc is there.
            if (GameCatalog.Disc.NSMB_USA_REV1.isInstalled(app) && !GameCatalog.msmbwiiModInstalled(app) &&
                MsmbwiiMod.bundled(app) && !MsmbwiiMod.state.running) MsmbwiiMod.start(app)
            WiiDsArt.repairRetroRewind(app)
        }.apply { name = "WiiDsArt"; isDaemon = true }.start()
    }

    /** Progress for the UI; [State.done]/[State.total] refresh while running. */
    fun poll(): State {
        val current = state
        if (!current.running) return current
        val p = WiiDsDisc.progress()
        return current.copy(done = p[0], total = p[1])
    }

    /** Clears a finished result once the UI has shown it. */
    fun acknowledge() { if (!state.running) state = State() }

    private fun install(context: Context, disc: GameCatalog.Disc, source: WiiDsDisc.Source): String? {
        val info = WiiDsDisc.identify(context, source)
        disc.mismatch(info)?.let { return it }
        val dest = disc.installDir(context)
        DiscInstallCommit.recover(dest)?.let { return it }
        val temp = File(dest.parentFile, dest.name + ".importing")
        temp.deleteRecursively()
        dest.parentFile?.mkdirs()
        val error = when (disc.platform) {
            WiiDsDisc.Platform.WII -> WiiDsDisc.extractWii(context, source, temp)
            WiiDsDisc.Platform.GAMECUBE -> {
                temp.mkdirs()
                WiiDsDisc.extractGameCube(context, source, File(temp, "game.iso"))
            }
        }
        if (error != null) {
            temp.deleteRecursively()
            return if (error == "Cancelled.") "Import cancelled." else error
        }
        // Eclipse's GMSE04 header is reused by other patched versions. Accept only the
        // exact 1.1.0 output this native runtime was built for, before replacing anything.
        if (disc == GameCatalog.Disc.ECLIPSE_110) {
            val iso = File(temp, "game.iso")
            val digest = java.security.MessageDigest.getInstance("SHA-256")
            iso.inputStream().buffered().use { input ->
                val buffer = ByteArray(1024 * 1024)
                while (true) {
                    if (importCancelled) {
                        temp.deleteRecursively()
                        return "Import cancelled."
                    }
                    val n = input.read(buffer)
                    if (n < 0) break
                    digest.update(buffer, 0, n)
                }
            }
            val actual = digest.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
            if (iso.length() != 1459978240L || actual != "d47de756d57511c427c1d500858f7eee5bbcb1a8e5e40c97758d18cb2f2bed6f") {
                temp.deleteRecursively()
                return "This image isn't the supported Eclipse 1.1.0 build. Apply the official 1.1.0 patch to an unchanged USA Sunshine revision 0 disc. Your existing game and saves were kept."
            }
        }
        // The install folder holds only disc data; saves and settings live elsewhere.
        if (importCancelled) {
            temp.deleteRecursively()
            return "Import cancelled."
        }
        val publicationError = DiscInstallCommit.publish(temp, dest)
        if (publicationError != null) {
            temp.deleteRecursively()
            return publicationError
        }
        return null
    }
}
