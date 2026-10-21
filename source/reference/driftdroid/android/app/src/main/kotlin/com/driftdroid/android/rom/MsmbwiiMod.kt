package com.driftdroid.android.rom

import android.content.Context
import android.net.Uri
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest
import java.util.zip.ZipInputStream

/**
 * MSMBWii: Just a Little Adventure (MidiGuyDP and Fredo; Another SMBW code by Skawo and Treeki).
 * Included with WiiDS with its authors' permission (assets/mods/MSMBWii.zip); if a build lacks
 * it, the official page's download or a copy the player picks is used instead. Only the exact
 * known release is accepted, unpacked beside the NSMB disc as games/mod/{riivolution,msmbwiijala}.
 */
object MsmbwiiMod {
    const val SOURCE_URL = "http://riivolution.nanolx.org/MSMBWii.zip"
    const val SOURCE_NAME = "riivolution.nanolx.org"
    const val SIZE = 3_007_052L
    private const val SHA256 = "be135baea3a6caeb6510574e5bf0ed55a6e3fdfbe51d191195134d865faa30cf"
    /** Turns the mod's Riivolution option on (the runtime reads riivolution/config/SMNE.xml). */
    private const val OPTION_CONFIG = "<riivolution version=\"2\">\n" +
        "<option id=\"MidiGuyDP's SMBWiiMSMBWii: Just A Little Adventure\" default=\"1\" />\n" +
        "</riivolution>\n"

    data class State(val running: Boolean = false, val done: Long = 0, val error: String? = null, val finished: Boolean = false)

    @Volatile var state = State()
        private set

    @Volatile private var cancelled = false

    private const val BUNDLED = "mods/MSMBWii.zip"

    /** True when this build carries the mod, so no download is needed. */
    fun bundled(context: Context): Boolean = runCatching { context.assets.open(BUNDLED).use { true } }.getOrDefault(false)

    fun cancel() { cancelled = true }

    private fun checkCancel() {
        if (cancelled) throw IOException("Mod installation cancelled. Your existing mod and saves were kept.")
    }

    fun acknowledge() { if (!state.running) state = State() }

    /** Downloads from the official page ([picked] == null) or installs the player's own copy. */
    fun start(context: Context, picked: Uri? = null): Boolean {
        synchronized(this) {
            if (state.running) return false
            state = State(running = true)
            cancelled = false
        }
        val app = context.applicationContext
        Thread {
            val error = runCatching { install(app, picked) }.getOrElse { it.message ?: "The mod couldn't be installed." }
            state = State(running = false, done = state.done, error = error, finished = true)
        }.apply { name = "WiiDsMod"; isDaemon = true }.start()
        return true
    }

    private fun install(context: Context, picked: Uri?): String? {
        val modDir = GameCatalog.msmbwiiModDir(context)
        val download = File(modDir.parentFile, "MSMBWii.zip.part")
        val temp = File(modDir.parentFile, modDir.name + ".importing")
        var ownsStaging = false
        modDir.parentFile?.mkdirs()
        try {
            MsmbwiiInstallCommit.recover(modDir)?.let { return it }
            if (MsmbwiiInstallCommit.orphanedStagedSave(temp, modDir))
                return "An interrupted mod installation holds a saved game in its staging folder. Both copies were kept for recovery."
            checkCancel()
            if (picked == null && bundled(context)) {
                context.assets.open(BUNDLED).use { copyLimited(it, download) }
            } else if (picked == null) {
                val connection = URL(SOURCE_URL).openConnection() as HttpURLConnection
                connection.connectTimeout = 15_000
                connection.readTimeout = 30_000
                try {
                    if (connection.responseCode != 200) return "$SOURCE_NAME answered ${connection.responseCode}. Try again later."
                    connection.inputStream.use { copyLimited(it, download) }
                } finally {
                    connection.disconnect()
                }
            } else {
                val input = context.contentResolver.openInputStream(picked!!) ?: return "The chosen file couldn't be opened."
                input.use { copyLimited(it, download) }
            }
            if (download.length() != SIZE || sha256(download) != SHA256)
                return "That isn't the MSMBWii.zip release WiiDS knows (it should be ${SIZE} bytes). Nothing was changed."
            checkCancel()
            if (temp.exists() && !temp.deleteRecursively()) throw IOException("The previous staging folder couldn't be cleared. Your mod and saves were kept.")
            ownsStaging = true
            unpack(download, temp)
            checkCancel()
            File(temp, "riivolution/config").mkdirs()
            File(temp, "riivolution/config/SMNE.xml").writeText(OPTION_CONFIG)
            if (!File(temp, "msmbwiijala/AnotherRes/SystemUS_1.bin").isFile) {
                temp.deleteRecursively()
                return "The mod archive is incomplete."
            }
            MsmbwiiInstallCommit.preserveSave(modDir, temp, ::checkCancel)
            checkCancel()
            return MsmbwiiInstallCommit.publish(temp, modDir, ::checkCancel)
        } finally {
            download.delete()
            if (ownsStaging) temp.deleteRecursively()
        }
    }

    private fun copyLimited(input: InputStream, target: File) {
        FileOutputStream(target).use { out ->
            val buffer = ByteArray(64 * 1024)
            var total = 0L
            while (true) {
                checkCancel()
                val n = input.read(buffer)
                if (n < 0) break
                total += n
                if (total > SIZE) throw IllegalStateException("That isn't the MSMBWii.zip release WiiDS knows.")
                out.write(buffer, 0, n)
                state = state.copy(done = total)
            }
        }
    }

    /** Only the mod's two folders and its credits file; no path may leave the target. */
    private fun unpack(zip: File, target: File) {
        val root = target.canonicalFile
        ZipInputStream(zip.inputStream().buffered()).use { stream ->
            while (true) {
                checkCancel()
                val entry = stream.nextEntry ?: break
                val name = entry.name.replace('\\', '/')
                val wanted = name.startsWith("msmbwiijala/") || name.startsWith("riivolution/") || name == "Readme (Credits).txt"
                if (!wanted) continue
                val out = File(root, name).canonicalFile
                if (!out.path.startsWith(root.path + File.separator)) throw IllegalStateException("Unsafe path in the mod archive.")
                if (entry.isDirectory) { out.mkdirs(); continue }
                out.parentFile?.mkdirs()
                FileOutputStream(out).use { output ->
                    val buffer = ByteArray(64 * 1024)
                    while (true) {
                        checkCancel()
                        val count = stream.read(buffer)
                        if (count < 0) break
                        output.write(buffer, 0, count)
                    }
                }
            }
        }
        checkCancel()
    }

    private fun sha256(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().use { input ->
            val buffer = ByteArray(64 * 1024)
            while (true) {
                checkCancel()
                val n = input.read(buffer)
                if (n < 0) break
                digest.update(buffer, 0, n)
            }
        }
        checkCancel()
        return digest.digest().joinToString("") { "%02x".format(it) }
    }
}
