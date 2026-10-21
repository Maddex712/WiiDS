package com.driftdroid.android.rom

import android.content.Context
import android.util.Log
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * Builds each game's art from the player's imported disc: companion-screen pictures and fonts,
 * Mario Kart's Wii-menu chrome, launcher sounds and race maps, and the launcher's game cases.
 * WiiDS ships none of it. Runs after an import, and again whenever something is missing.
 */
object WiiDsArt {
    private const val TAG = "WiiDsArt"

    /**
     * One art set. [ownFolder] sets are built aside and swapped in whole; shared folders (the
     * Mario Kart ThorMap root, the launcher cases) are written in place. [marker] identifies
     * the art version; the manifest's referenced files must also be present.
     */
    private data class Job(val kind: String, val source: File, val target: File, val marker: String, val ownFolder: Boolean)

    private fun external(context: Context): File = context.getExternalFilesDir(null) ?: File(context.filesDir, "data")

    /** Mario Kart's art folder (menu, HUD, maps, sounds) and the launcher's cases inside it. */
    fun thorMap(context: Context): File = File(context.filesDir, "WiiCompiled/ThorMap")
    private fun cases(context: Context): File = File(thorMap(context), "launcher")

    private fun jobs(context: Context, disc: GameCatalog.Disc): List<Job> {
        val root = disc.installDir(context)
        return when (disc) {
            GameCatalog.Disc.NSMB_USA_REV1 -> listOf(
                // course9.png: the NSMB2-style world bubbles (2026-10-20); older sets are rebuilt.
                Job("nsmb-hud", root, File(external(context), "nsmbw/ThorHud"), "course9.png", true),
                Job("nsmb-hud", root, File(external(context), "msmbwii/ThorHud"), "course9.png", true),
                Job("nsmb-cases", root, cases(context), "msmbwii-disc.png", false))
            GameCatalog.Disc.SUNSHINE_USA -> listOf(
                Job("sms-hud", disc.installedFile(context), File(external(context), "sms/ThorHud"), "manifest.json", true),
                Job("sms-cases", disc.installedFile(context), cases(context), "sunshine-disc.png", false))
            GameCatalog.Disc.ECLIPSE_110 -> listOf(
                Job("sms-hud", disc.installedFile(context), File(external(context), "eclipse/ThorHud"), "manifest.json", true))
            GameCatalog.Disc.MARIO_KART_PAL -> listOf(
                Job("mkw-art", root, thorMap(context), "menu.json", false),
                Job("mkw-audio", root, File(thorMap(context), "launcher-audio"), "audio.json", true),
                Job("mkw-cases", root, cases(context), "original-disc.png", false),
                Job("mkw-maps", root, thorMap(context), "course_31.json", false))
        }
    }

    /** The disc whose art is being built right now (for the Games screen), or null. */
    @Volatile var building: GameCatalog.Disc? = null
        private set

    fun missing(context: Context, disc: GameCatalog.Disc): Boolean =
        disc.isInstalled(context) && jobs(context, disc).any { !complete(it) }

    /** Blocking. Returns null on success or the first reason; never touches the imported disc. */
    @Synchronized
    fun build(context: Context, disc: GameCatalog.Disc, onlyMissing: Boolean = false): String? {
        building = disc
        try { return buildJobs(context, disc, onlyMissing) } finally { building = null }
    }

    private fun buildJobs(context: Context, disc: GameCatalog.Disc, onlyMissing: Boolean): String? {
        var firstError: String? = null
        for (job in jobs(context, disc)) {
            if (onlyMissing && complete(job)) continue
            val error = run(job)
            if (error != null) {
                Log.w(TAG, "${disc.gameName} ${job.kind}: $error")
                if (firstError == null) firstError = error
            }
        }
        return firstError
    }

    /** A manifest alone can survive an interrupted write or a deleted picture. Check the
     * generated files too, so repair actually restores the complete companion experience. */
    private fun complete(job: Job): Boolean {
        if (!present(File(job.target, job.marker))) return false
        return when (job.kind) {
            "mkw-art" -> listOf("menu.json", "hud.json").all { manifestComplete(job.target, it) } &&
                listOf("settings.tsv", "font.rgba", "background.rgba", "panel.rgba", "header.rgba",
                    "footer.rgba", "button.rgba", "button_focus.rgba").all { present(File(job.target, "thor_settings/$it")) }
            "mkw-maps" -> (0 until 32).all { id ->
                val stem = "course_" + id.toString().padStart(2, '0')
                present(File(job.target, "$stem.png")) && present(File(job.target, "$stem.json"))
            }
            "mkw-audio" -> manifestComplete(job.target, "audio.json")
            "nsmb-hud", "sms-hud" -> manifestComplete(job.target, "manifest.json")
            "mkw-cases", "nsmb-cases", "sms-cases" -> manifestComplete(job.target, "artwork.json")
            else -> true
        }
    }

    private fun present(file: File) = file.isFile && file.length() > 0

    /** Only generated bitmap/audio references are paths; source archive names in the same
     * manifest describe the imported disc and must never be treated as output filenames. */
    private fun manifestComplete(folder: File, name: String): Boolean = runCatching {
        val file = File(folder, name)
        if (!present(file) || file.length() > 4 * 1024 * 1024) return@runCatching false
        val root = folder.canonicalFile
        fun reference(value: String): Boolean {
            if (!value.endsWith(".png") && !value.endsWith(".wav") && !value.endsWith(".jpg")) return true
            val child = File(folder, value)
            return child.canonicalFile.parentFile == root && present(child)
        }
        fun visit(value: Any?): Boolean = when (value) {
            is JSONObject -> value.keys().asSequence().all { key ->
                val child = value.opt(key)
                // Sunshine HUD manifests use the output filename as the key.
                reference(key) && ((key != "texture" && key != "file" && key != "png") ||
                    child !is String || reference(child)) && visit(child)
            }
            is JSONArray -> (0 until value.length()).all { visit(value.opt(it)) }
            else -> true
        }
        visit(JSONObject(file.readText()))
    }.getOrDefault(false)

    private fun run(job: Job): String? {
        val dest = if (job.ownFolder) File(job.target.parentFile, job.target.name + ".building") else job.target
        if (job.ownFolder) dest.deleteRecursively()
        dest.mkdirs()
        val error = runCatching {
            WiiDsDisc.ensureLoaded()
            if (job.kind == "mkw-maps") nativeBuildMaps(job.source.absolutePath, dest.absolutePath)
            else nativeBuild(job.kind, job.source.absolutePath, dest.absolutePath)
        }.getOrElse { it.message ?: "The art couldn't be built." }
        if (!job.ownFolder) return error
        if (error != null) {
            dest.deleteRecursively()
            return error
        }
        job.target.deleteRecursively()
        if (!dest.renameTo(job.target)) {
            dest.deleteRecursively()
            return "The art couldn't be moved into place."
        }
        return null
    }

    /** Retro Rewind's pack (installed from its official servers) and its maps folder. */
    private fun retroPack(context: Context) = File(context.filesDir, "WiiCompiled/RetroRewind6")
    private fun retroMaps(context: Context) = File(thorMap(context), "retro_rewind")

    /**
     * Bakes maps for an installed or updated Retro Rewind pack, in the background. Only tracks
     * whose archive changed are rebaked; nothing runs when the pack is unchanged since last time.
     */
    fun repairRetroRewind(context: Context) {
        val pack = retroPack(context)
        val code = File(pack, "Binaries/Code.pul")
        if (!code.isFile || !GameCatalog.Disc.MARIO_KART_PAL.isInstalled(context)) return
        val stampFile = File(retroMaps(context), ".pack-stamp")
        val version = File(pack, "version.txt").takeIf { it.isFile }?.readText()?.trim() ?: ""
        val stamp = "${code.length()}:${code.lastModified()}:$version"
        if (stampFile.isFile && stampFile.readText() == stamp && manifestComplete(retroMaps(context), "maps.json")) return
        val app = context.applicationContext
        Thread {
            synchronized(this) {
                building = GameCatalog.Disc.MARIO_KART_PAL
                try {
                    WiiDsDisc.ensureLoaded()
                    retroMaps(app).mkdirs()
                    val error = nativeBuildRetroMaps(pack.absolutePath, retroMaps(app).absolutePath)
                    if (error == null) stampFile.writeText(stamp) else Log.w(TAG, "Retro Rewind maps: $error")
                } catch (e: Throwable) {
                    Log.w(TAG, "Retro Rewind maps", e)
                } finally {
                    building = null
                }
            }
        }.apply { name = "WiiDsRetroMaps"; isDaemon = true }.start()
    }

    @JvmStatic private external fun nativeBuildRetroMaps(pack: String, out: String): String?
    @JvmStatic private external fun nativeBuild(kind: String, source: String, out: String): String?
    @JvmStatic private external fun nativeBuildMaps(discRoot: String, out: String): String?
}
