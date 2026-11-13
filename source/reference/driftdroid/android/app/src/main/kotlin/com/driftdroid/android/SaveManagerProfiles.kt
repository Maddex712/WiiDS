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
    private val cacheChecked = AtomicBoolean(false)
    // Android supplies /data/user/0 as an alias in the app's mount namespace.
    // Resolve only trusted Context bases; SaveArchive still rejects links beneath them.
    fun work(context: Context) = File(context.filesDir.canonicalFile, "save-manager")
    /** Artwork only reads imported data and writes presentation files. */
    fun launchBlocker(context: Context): String? = GameStorage.blocker(context) ?: when {
        com.driftdroid.android.rom.GameInstaller.recoveryError != null -> com.driftdroid.android.rom.GameInstaller.recoveryError
        WiiDsUpdater.installing.get() -> "Finish or cancel the WiiDS update installation first."
        busy.get() -> "Wait for the save operation to finish."
        com.driftdroid.android.rom.GameInstaller.running || com.driftdroid.android.rom.EclipseMod.state.running ||
            com.driftdroid.android.rom.MsmbwiiMod.state.running -> "Finish or cancel game setup first."
        SaveArchive.pending(work(context)) -> "Open Options > Save manager to recover the interrupted restore first."
        else -> null
    }
    fun blocker(context: Context): String? = launchBlocker(context)
        ?: if (com.driftdroid.android.rom.WiiDsArt.building != null) "Artwork is being prepared in the background." else null

    fun requireStopped(context: Context) {
        val manager = context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
        val active = manager.runningAppProcesses ?: throw IOException("Could not check running games. Restart WiiDS and try again.")
        if (ThorGameSession.hasRetainedRuntime() || ThorGameSession.isUpdating() || active.any {
                it.processName.startsWith(context.packageName + ":") &&
                    (it.processName.endsWith("_game") || it.processName.endsWith("_import"))
            }) throw IOException("Close the running game or importer before managing saves. If Mario Kart was opened, restart WiiDS first.")
        if (ThorRetroRewindUpdateStatus.snapshot.running) throw IOException("Finish the Retro Rewind update before managing saves.")
    }
    fun profile(context: Context, game: FrontendGame): SaveArchive.Profile =
        profile(context, game, true)

    /** Run only inside the global save-operation/file lock, before starting any new work. */
    fun cleanup(context: Context) {
        val work = work(context)
        if (SaveArchive.pending(work)) return
        for (game in FrontendGame.values()) SaveArchive.cleanup(profile(context, game, false), work, File(work, "backups"))
        if (!cacheChecked.get()) {
            val pattern = Regex("wiids-save-[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\.zip")
            for (file in context.cacheDir.listFiles() ?: throw IOException("Cannot inspect temporary backup files.")) {
                if (pattern.matches(file.name)) { SaveArchive.safe(file); java.nio.file.Files.delete(file.toPath()) }
            }
            cacheChecked.set(true)
        }
    }

    private fun configuredNand(config: File): String {
        if (!config.isFile) return ""
        try {
            require(config.length() < 1L.shl(20)) { "Config is too large to check safely." }
            return com.driftdroid.android.rom.WiiDsDisc.configuredNand(config.readBytes())
        } catch (error: Exception) {
            throw IOException("The NAND path in ${config.path} could not be checked. Keep its files in place.", error)
        }
    }

    /** Offload protection includes configured NANDs even when backup does not support their layout. */
    fun protectionRoots(context: Context): List<File> {
        val roots = ArrayList<File>()
        val internal = context.filesDir.canonicalFile
        val external = context.getExternalFilesDir(null)?.canonicalFile ?: File(internal, "data")
        for (game in FrontendGame.values()) roots.addAll(profile(context, game, false).roots.values)
        val bases = listOf(File(internal, "WiiCompiled"), File(external, "nsmbw"),
            File(external, "msmbwii"), File(external, "brawl"), File(external, "project-plus"))
        for (base in bases) {
            roots.add(File(base, "Saves"))
            val config = File(base, "Config.toml")
            val path = configuredNand(config)
            if (path.isNotEmpty()) {
                val folder = File(path)
                roots.add(if (folder.isAbsolute) folder else File(base, path))
            }
        }
        return roots
    }

    private fun profile(context: Context, game: FrontendGame, backup: Boolean): SaveArchive.Profile {
        val internal = context.filesDir.canonicalFile
        val external = context.getExternalFilesDir(null)?.canonicalFile
        val roots = linkedMapOf<String, File>()
        fun root(name: String, base: File, path: String) { roots[name] = File(base, path) }
        fun defaultNand(config: File) {
            if (backup) {
                if (configuredNand(config).isNotEmpty())
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
                if (backup && File(base, "Saves").isDirectory && File(base, "Saves").listFiles()?.any { it.isDirectory } != false)
                    throw IOException("This game has extra NAND profiles. Keep those saves in place; this preview supports the standard profile only.")
                root("main", base, "NAND/title/00010000/534d4e45/data")
            }
            FrontendGame.SUNSHINE, FrontendGame.ECLIPSE -> {
                if (external == null) throw IOException("Game save storage is unavailable.")
                root("main", external, (if (game == FrontendGame.SUNSHINE) "sms" else "eclipse") + "/saves/card-a")
            }
            FrontendGame.MKDD, FrontendGame.MKDD_PLUS -> {
                if (external == null) throw IOException("Double Dash save storage is unavailable.")
                root("main", external, if (game == FrontendGame.MKDD_PLUS) "mkdd/plus/saves" else "mkdd/saves")
            }
            FrontendGame.BRAWL, FrontendGame.PROJECT_PLUS -> {
                if (external == null) throw IOException("Brawl save storage is unavailable.")
                val profile = if (game == FrontendGame.PROJECT_PLUS) "project-plus" else "brawl"
                defaultNand(File(external, "$profile/Config.toml"))
                root("main", external, "$profile/NAND/title/00010000/52534245/data")
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
