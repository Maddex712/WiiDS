package com.driftdroid.android.rom

import android.content.Context
import android.net.Uri
import com.driftdroid.android.FrontendGame
import com.driftdroid.android.GameStorage
import com.driftdroid.android.SaveManagerProfiles
import dev.wiids.xd.XdDisc
import dev.wiids.xd.XdStorage
import dev.wiids.xd.XgUps
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.security.DigestInputStream
import java.security.DigestOutputStream
import java.security.MessageDigest

/** Installs the creator's unmodified 1.2.1-3 UPS patch using the imported USA disc. */
internal object XgMod {
    private const val PATCH_BYTES = 284_076_072L
    private const val PATCH_SHA256 = "2814433290709013a99905037add6921c911538b3fad71a1bc55dd87c26ef6a8"
    private const val RESULT_SHA1 = "7af7d11916f69d3af1041c728f9231ba9b38f29e"
    private const val BLOCK = 1024 * 1024

    data class State(val running: Boolean = false, val phase: String = "", val done: Long = 0,
                     val total: Long = 1, val error: String? = null)

    @Volatile var state = State()
        private set
    @Volatile private var cancelled = false

    fun cancel() { cancelled = true }

    fun start(context: Context, picked: Uri): Boolean {
        val app = context.applicationContext
        synchronized(GameInstaller) {
            if (SaveManagerProfiles.blocker(app) != null) return false
            if (runCatching { SaveManagerProfiles.requireStopped(app) }.isFailure) return false
            cancelled = false
            state = State(running = true, phase = "Checking your Pokémon XD disc")
        }
        Thread({
            val error = runCatching {
                synchronized(WiiDsArt) { install(app, picked) }
                GameStorage.enable(app, FrontendGame.POKEMON_XG)
                null
            }.getOrElse { it.message ?: "Pokémon XG couldn't be installed." }
            state = State(error = error)
        }, "WiiDsXgInstall").apply { isDaemon = true }.start()
        return true
    }

    private fun install(context: Context, picked: Uri) {
        val source = XdStorage.iso(context)
        XdDisc.validateImage(source) { cancelled }?.let { throw IOException(it) }
        val destination = XdStorage.game(context, true)
        val root = destination.parentFile ?: throw IOException("Pokémon XG storage is unavailable.")
        if (!root.isDirectory && !root.mkdirs()) throw IOException("Pokémon XG storage is unavailable.")
        DiscInstallCommit.recover(destination)?.let { throw IOException(it) }
        val staged = File(root, "game.importing")
        if (staged.exists() && !staged.deleteRecursively()) throw IOException("The interrupted XG setup couldn't be cleared.")
        if (root.usableSpace < XdDisc.BYTES + 64L * BLOCK) throw IOException("Pokémon XG needs at least 1.53 GB free.")
        if (!staged.mkdir()) throw IOException("The Pokémon XG setup folder couldn't be created.")
        try {
            val output = File(staged, "game.iso")
            val input = context.contentResolver.openInputStream(picked) ?: throw IOException("The XG patch couldn't be opened.")
            val patchHash = MessageDigest.getInstance("SHA-256")
            val outputHash = MessageDigest.getInstance("SHA-1")
            state = State(running = true, phase = "Applying Pokémon XG 1.2.1", total = PATCH_BYTES)
            DigestInputStream(input.buffered(BLOCK), patchHash).use { patch ->
                source.inputStream().buffered(BLOCK).use { original ->
                    FileOutputStream(output).use { file ->
                        val result = DigestOutputStream(file, outputHash).buffered(BLOCK)
                        XgUps.apply(original, patch, result, XdDisc.BYTES, PATCH_BYTES) { done ->
                            checkCancel()
                            if (done - state.done >= BLOCK || done == PATCH_BYTES) state = state.copy(done = done)
                        }
                        result.flush()
                        file.fd.sync()
                    }
                }
            }
            if (hex(patchHash) != PATCH_SHA256) throw IOException("Choose the creator's Pokémon XG v1.2.1-3.ups file.")
            if (output.length() != XdDisc.BYTES || hex(outputHash) != RESULT_SHA1)
                throw IOException("The patched Pokémon XG disc didn't verify.")
            checkCancel()
            File(staged, XdStorage.XG_VERIFIED_MARKER).writeText(RESULT_SHA1)
            DiscInstallCommit.publish(staged, destination)?.let { throw IOException(it) }
        } finally {
            staged.deleteRecursively()
        }
    }

    private fun checkCancel() {
        if (cancelled) throw IOException("Pokémon XG setup cancelled. Your games and saves were kept.")
    }

    private fun hex(hash: MessageDigest) = hash.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
}
