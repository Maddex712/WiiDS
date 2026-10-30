package com.driftdroid.android

import android.app.ActivityManager
import android.content.Context
import com.driftdroid.android.saves.SaveArchive
import java.io.File
import java.io.IOException
import java.util.concurrent.atomic.AtomicBoolean

/** Exact engine storage paths; never accept a destination path from a backup or launch intent. */
internal object SaveManagerProfiles {
    val busy = AtomicBoolean(false)
    // Android supplies /data/user/0 as an alias in the app's mount namespace.
    // Resolve only trusted Context bases; SaveArchive still rejects links beneath them.
    fun work(context: Context) = File(context.filesDir.canonicalFile, "save-manager")
    fun blocker(context: Context): String? = when {
        GameStorage.blocker(context) != null -> GameStorage.blocker(context)
        WiiDsUpdater.installing.get() -> "Finish or cancel the WiiDS update installation first."
        busy.get() -> "Wait for the save operation to finish."
        SaveArchive.pending(work(context)) -> "Open Options > Save manager to recover the interrupted restore first."
        else -> null
    }
    fun requireStopped(context: Context) {
        val manager = context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
        val active = manager.runningAppProcesses ?: throw IOException("Could not check running games. Restart WiiDS and try again.")
        if (ThorGameSession.hasRetainedRuntime() || ThorGameSession.isUpdating() || active.any {
                it.processName.startsWith(context.packageName + ":") &&
                    (it.processName.endsWith("_game") || it.processName.endsWith("_import"))
            }) throw IOException("Close the running game or importer before managing saves. If Mario Kart was opened, restart WiiDS first.")
        if (ThorRetroRewindUpdateStatus.snapshot.running) throw IOException("Finish the Retro Rewind update before managing saves.")
    }
    fun profile(context: Context, game: FrontendGame): SaveArchive.Profile {
        val internal = context.filesDir.canonicalFile
        val external = context.getExternalFilesDir(null)?.canonicalFile
        val roots = linkedMapOf<String, File>()
        fun root(name: String, base: File, path: String) { roots[name] = File(base, path) }
        fun defaultNand(config: File) {
            if (config.isFile) {
                require(config.length() < 1L.shl(20)) { "Config is too large to check safely." }
                val configured = Regex("(?m)^\\s*nand_root\\s*=\\s*([^\\r\\n#]+)").find(config.readText())?.groupValues?.get(1)?.trim()
                if (configured != null && configured != "\"\"" && configured != "''")
                    throw IOException("This game uses a custom NAND folder. Save manager currently supports the default WiiDS save location.")
            }
        }
        when (game) {
            FrontendGame.MARIO_KART, FrontendGame.RETRO_REWIND -> {
                val base = File(internal, "WiiCompiled")
                defaultNand(File(base, "Config.toml"))
                if (game == FrontendGame.MARIO_KART) root("main", base, "NAND/title/00010004/524d4350/data")
                else {
                    root("main", base, "riivolution/save/RetroWFC/RMCP")
                    root("alternate", base, "riivolution/save/RetroWFC2/RMCP")
                    root("mod-settings", base, "NAND/shared2/Pulsar/RetroRewind6")
                }
            }
            FrontendGame.NSMB, FrontendGame.MSMBWII -> {
                val base = File(external ?: File(internal, "data"), game.id)
                defaultNand(File(base, "Config.toml"))
                if (File(base, "Saves").isDirectory && File(base, "Saves").listFiles()?.any { it.isDirectory } != false)
                    throw IOException("This game has extra NAND profiles. Keep those saves in place; this preview supports the standard profile only.")
                root("main", base, "NAND/title/00010000/534d4e45/data")
            }
            FrontendGame.SUNSHINE, FrontendGame.ECLIPSE -> {
                if (external == null) throw IOException("Game save storage is unavailable.")
                root("main", external, (if (game == FrontendGame.SUNSHINE) "sms" else "eclipse") + "/saves/card-a")
            }
            FrontendGame.BRAWL -> {
                if(external==null)throw IOException("Brawl save storage is unavailable.")
                defaultNand(File(external,"brawl/Config.toml"))
                root("main",external,"brawl/NAND/title/00010000/52534245/data")
            }
            FrontendGame.GALAXY, FrontendGame.KAIZO -> {
                if (external == null) throw IOException("Galaxy save storage is unavailable.")
                val namespace = if (game == FrontendGame.KAIZO) "galaxy-kaizo" else "galaxy"
                root("main", internal, "$namespace/nand/title")
                root("mirror", external, "$namespace/GalaxyData/saves")
                for (slot in listOf("slot1", "slot2", "slot3", "quick"))
                    root(slot, internal, "$namespace/nand_$slot/title")
            }
        }
        return SaveArchive.Profile(game.id, roots)
    }
}
