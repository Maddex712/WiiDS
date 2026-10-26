package com.driftdroid.android.rom

import android.content.Context
import android.net.Uri
import dev.chasem.eclipselauncher.EclipseVcdiff
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.security.DigestInputStream
import java.security.MessageDigest

/** Eclipse Team's 1.1.0 patch, applied locally to the player's imported Sunshine disc. */
object EclipseMod {
    const val PATCH_BYTES = 1_191_516_980L
    const val DISC_BYTES = 1_459_978_240L
    const val BUNDLED = "mods/eclipse-v1.1.0.xdelta"
    private const val SOURCE_SHA = "67cec1634e641227a4cd51e6a0b277730cb9a1adaa867530c9e66de45373e51d"
    private const val PATCH_SHA = "25c6b08333802bca17c1e6571c7289a6382a32a85af1aa5bd224acfbac6a8bf0"
    const val RESULT_SHA = "d47de756d57511c427c1d500858f7eee5bbcb1a8e5e40c97758d18cb2f2bed6f"

    data class State(val running: Boolean = false, val phase: String = "", val done: Long = 0,
                     val total: Long = 1, val error: String? = null, val finished: Boolean = false)
    @Volatile var state = State()
        private set
    @Volatile private var cancelled = false
    fun bundled(context: Context) = runCatching { context.assets.open(BUNDLED).use { true } }.getOrDefault(false)
    fun cancel() { cancelled = true }
    fun acknowledge() { if (!state.running) state = State() }

    /** A raw official xdelta may be selected when a source-only build lacks the bundled patch. */
    fun start(context: Context, picked: Uri? = null): Boolean {
        synchronized(GameInstaller) {
            if (state.running || GameInstaller.running || com.driftdroid.android.WiiDsUpdater.installing.get()) return false
            state = State(running = true, phase = "Checking your Sunshine disc", total = DISC_BYTES)
            cancelled = false
        }
        val app = context.applicationContext
        Thread {
            val error = runCatching { install(app, picked); null }.getOrElse { it.message ?: "Eclipse couldn't be installed." }
            state = state.copy(running = false, finished = true, error = error)
            if (error == null) GameInstaller.repairArt(app)
        }.apply { name = "WiiDsEclipseInstall"; isDaemon = true }.start()
        return true
    }

    private fun checkCancel() { if (cancelled) throw IOException("Eclipse installation cancelled. Your games and saves were kept.") }

    private fun hash(file: File, phase: String): String {
        state = state.copy(phase = phase, done = 0, total = file.length())
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().buffered().use { input ->
            val buffer = ByteArray(1024 * 1024)
            var done = 0L
            while (true) {
                checkCancel()
                val count = input.read(buffer)
                if (count < 0) break
                digest.update(buffer, 0, count); done += count
                state = state.copy(done = done)
            }
        }
        return hex(digest.digest())
    }

    private fun install(context: Context, picked: Uri?) {
        val source = GameCatalog.Disc.SUNSHINE_USA.installedFile(context)
        if (!source.isFile || source.length() != DISC_BYTES || hash(source, "Checking your Sunshine disc") != SOURCE_SHA)
            throw IOException("Import your unchanged USA Sunshine revision 0 disc first. Eclipse 1.1.0 needs that exact source.")
        val directory = GameCatalog.Disc.ECLIPSE_110.installDir(context)
        directory.mkdirs()
        val temp = File(directory, "game.iso.patching")
        // This exact path is owned only by this installer; an interrupted attempt never becomes playable.
        if (temp.exists() && !temp.delete()) throw IOException("An interrupted Eclipse installation couldn't be cleared.")
        if (directory.usableSpace < DISC_BYTES + 64 * 1024 * 1024)
            throw IOException("Eclipse needs at least 1.5 GB free for its separate game image. Sunshine and all saves are kept.")
        try {
            val digest = MessageDigest.getInstance("SHA-256")
            val input: InputStream = if (picked == null) context.assets.open(BUNDLED)
                else context.contentResolver.openInputStream(picked) ?: throw IOException("The selected Eclipse patch couldn't be opened.")
            state = state.copy(phase = "Installing Eclipse 1.1.0", done = 0, total = PATCH_BYTES)
            var consumed = 0L
            DigestInputStream(input, digest).use { patch ->
                EclipseVcdiff.apply(source, patch, temp) { done, _ ->
                    checkCancel()
                    if (done > PATCH_BYTES) throw IOException("The Eclipse patch is larger than the supported release.")
                    consumed = done; state = state.copy(done = done)
                }
            }
            if (consumed != PATCH_BYTES || hex(digest.digest()) != PATCH_SHA)
                throw IOException("This isn't the official Eclipse 1.1.0 patch. Nothing was installed.")
            if (temp.length() != DISC_BYTES || hash(temp, "Verifying Eclipse") != RESULT_SHA)
                throw IOException("The patched Eclipse image didn't verify. Nothing was installed.")
            checkCancel()
            Files.move(temp.toPath(), File(directory, "game.iso").toPath(),
                StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
        } finally {
            temp.delete()
        }
    }

    private fun hex(bytes: ByteArray) = bytes.joinToString("") { "%02x".format(it.toInt() and 255) }
}
