package com.driftdroid.android.rom

import android.content.Context
import android.net.Uri
import com.driftdroid.android.FrontendGame
import com.driftdroid.android.GameStorage
import com.driftdroid.android.SaveManagerProfiles
import com.driftdroid.android.WiiDsUpdater
import dev.thor.mkdd.MkddArt
import dev.thor.mkdd.MkddStorage
import dev.thor.mkdd.MkddVcdiff
import java.io.File
import java.io.IOException
import java.security.DigestInputStream
import java.security.MessageDigest

/** Applies the creator's pinned Plus 3.3 patch locally. Never writes the original disc or either card. */
object MkddPlusMod {
    const val PATCH_BYTES = 789_136_548L
    const val RESULT_BYTES = 1_074_593_792L
    const val PATCH_SHA = "e78cfc35f54b5b277ac0c7358435c9c2c82a2a0a8ecbdca6d0bdb47fbd8ba158"
    const val RESULT_SHA = "0bc58c2ceff549e1a7eb7d6efac37fbabc4fc3649a804282efc703e67e6b505d"
    data class State(val running: Boolean = false, val phase: String = "", val done: Long = 0,
                     val total: Long = 1, val error: String? = null, val finished: Boolean = false)
    @Volatile var state = State()
        private set
    @Volatile private var cancelled = false
    fun cancel() { cancelled = true }
    fun acknowledge() { if (!state.running) state = State() }

    fun start(context: Context, picked: Uri): Boolean {
        val app = context.applicationContext
        synchronized(GameInstaller) {
            if (GameStorage.blocker(app) != null || GameInstaller.running || EclipseMod.state.running ||
                MsmbwiiMod.state.running || SaveManagerProfiles.busy.get() || WiiDsUpdater.installing.get()) return false
            if (runCatching { SaveManagerProfiles.requireStopped(app) }.isFailure) return false
            if (com.driftdroid.android.saves.SaveArchive.pending(SaveManagerProfiles.work(app))) return false
            cancelled = false
            state = State(running = true, phase = "Checking your Double Dash disc")
        }
        Thread {
            val error = runCatching {
                // Serializes with all art repair and offload transactions.
                synchronized(WiiDsArt) { install(app, picked) }
                GameStorage.enable(app, FrontendGame.MKDD_PLUS)
                null
            }.getOrElse { it.message ?: "Double Dash Plus couldn't be installed." }
            state = state.copy(running = false, finished = true, error = error)
        }.apply { name = "WiiDsPlusInstall"; isDaemon = true }.start()
        return true
    }
    private fun checkCancel() {
        if (cancelled) throw IOException("Plus installation cancelled. Your games and saves were kept.")
    }
    private fun hex(digest: ByteArray) = digest.joinToString("") { "%02x".format(it.toInt() and 255) }
    private fun hash(file: File, algorithm: String, phase: String): String {
        state = state.copy(phase = phase, done = 0, total = file.length())
        val digest = MessageDigest.getInstance(algorithm)
        file.inputStream().buffered().use { input ->
            val buffer = ByteArray(1024 * 1024)
            var done = 0L
            while (true) {
                checkCancel()
                val n = input.read(buffer)
                if (n < 0) break
                digest.update(buffer, 0, n); done += n
                state = state.copy(done = done)
            }
        }
        return hex(digest.digest())
    }
    private fun install(context: Context, picked: Uri) {
        val source = MkddStorage.iso(context)
        if (source.length() != 1_459_978_240L || hash(source, "SHA-1", "Checking your Double Dash disc") !=
            "34bdef709a1ae9799fdabe8e54cff1c7d3665f2b")
            throw IOException("Import your unchanged USA Double Dash revision 0 disc first (GM4E01).")
        val root = File(MkddStorage.root(context), "plus")
        if (!root.isDirectory && !root.mkdirs()) throw IOException("Plus storage isn't available.")
        val destination = File(root, "game")
        DiscInstallCommit.recover(destination)?.let { throw IOException(it) }
        val staged = File(root, "game.importing")
        if (staged.exists() && !staged.deleteRecursively()) throw IOException("The interrupted Plus setup couldn't be cleared.")
        if (root.usableSpace < RESULT_BYTES + 256L * 1024 * 1024)
            throw IOException("Plus needs at least 1.3 GB free for its separate disc and detailed maps. Your original game and saves are kept.")
        if (!staged.mkdirs()) throw IOException("The Plus setup folder couldn't be created.")
        try {
            val output = File(staged, "game.iso")
            val digest = MessageDigest.getInstance("SHA-256")
            val input = context.contentResolver.openInputStream(picked) ?: throw IOException("The Plus patch couldn't be opened.")
            var consumed = 0L
            state = state.copy(phase = "Installing Double Dash Plus 3.3", done = 0, total = PATCH_BYTES)
            DigestInputStream(input, digest).use { patch ->
                MkddVcdiff.apply(source, patch, output) { done, _ ->
                    checkCancel()
                    if (done > PATCH_BYTES) throw IOException("Choose the original MKDD Plus 3.3 NTSC xdelta patch.")
                    consumed = done
                    state = state.copy(done = done)
                }
            }
            if (consumed != PATCH_BYTES || hex(digest.digest()) != PATCH_SHA)
                throw IOException("This isn't the supported MKDD Plus 3.3 NTSC patch. Your installed games were kept.")
            if (output.length() != RESULT_BYTES || hash(output, "SHA-256", "Verifying Double Dash Plus") != RESULT_SHA)
                throw IOException("The patched Plus disc didn't verify. Your installed games were kept.")
            checkCancel()
            // Build in the disc's staging directory before publishing; failure never replaces playable data.
            MkddArt.build(output, File(staged, "lower-art"), true) { done, total, _ ->
                checkCancel()
                state = state.copy(phase = "Building Plus course maps", done = done.toLong(), total = total.toLong())
            }?.let { throw IOException(it) }
            checkCancel()
            DiscInstallCommit.publish(staged, destination)?.let { throw IOException(it) }
            // Art is part of the committed disc directory. The renderer and ready checks prefer it.
        } finally { staged.deleteRecursively() }
    }
}
