package com.driftdroid.android

import android.app.Activity
import android.app.ActivityManager
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.widget.TextView
import android.widget.Toast
import com.driftdroid.android.rom.GameCatalog
import com.driftdroid.android.rom.WiiDsDisc
import java.io.File

/** Public entry point. Identifies a disc; launches only already imported, validated game data. */
class FrontendLaunchActivity : Activity() {
    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        if (!BuildConfig.THOR_BUILD) { fail("This build does not support WiiDS frontend launches."); return }
        SaveManagerProfiles.blocker(this)?.let { fail(it); return }
        val manager = getSystemService(ACTIVITY_SERVICE) as ActivityManager
        val processes = manager.runningAppProcesses?.map { it.processName }
        if (processes == null) { fail("Could not check running games. Try again from the WiiDS library."); return }
        if (processes.any { FrontendResume.isRuntimeProcess(packageName, it) }) {
            // A pin/deep link identifies a game without disc I/O. Only move its
            // already-existing task; a stale process must never start SDL again.
            val game = runCatching {
                require(intent.data == null || intent.data?.scheme == "wiids") {
                    "Return to the WiiDS library before opening a disc file."
                }
                resolve(intent)
            }.getOrElse { fail(it.message ?: "Invalid game shortcut."); return }
            val tasks = manager.appTasks
            val snapshots = tasks.mapNotNull { task ->
                runCatching {
                    val info = task.taskInfo
                    info.topActivity?.let { FrontendResume.Task(info.taskId, it.packageName, it.className) to task }
                }.getOrNull()
            }
            val id = FrontendResume.existingTask(game, packageName, processes, snapshots.map { it.first })
            val existing = snapshots.firstOrNull { it.first.id == id }?.second
            if (existing != null) {
                runCatching { existing.moveToFront() }
                    .onSuccess { finish() }
                    .onFailure { fail("Resume this game from Recents, or return to its WiiDS library.") }
            } else fail("A WiiDS game is already running. Continue from Recents or return to its library before changing games.")
            return
        }
        setContentView(TextView(this).apply { text = "Opening WiiDS…"; textSize = 22f })
        val request = intent
        Thread {
            val result = runCatching { resolve(request) }
            runOnUiThread {
                if (isFinishing || isDestroyed) return@runOnUiThread
                result.onSuccess { game ->
                    // Do not forward file paths, grants, arbitrary extras or component names to a runtime.
                    startActivity(Intent(this, ModePickerActivity::class.java)
                        .putExtra(ModePickerActivity.EXTRA_FRONTEND_GAME, game.id)
                        // Keep the retained kart Activity below the library. CLEAR_TOP
                        // destroys it before launchProduct can deliver onNewIntent.
                        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_REORDER_TO_FRONT or Intent.FLAG_ACTIVITY_SINGLE_TOP))
                    finish()
                }.onFailure { fail(it.message ?: "The launch request could not be read.") }
            }
        }.apply { name = "WiiDsFrontend"; isDaemon = true }.start()
    }

    private fun resolve(request: Intent): FrontendGame {
        val uri = request.data
        var explicit: FrontendGame? = null
        if (request.hasExtra("game")) {
            explicit = FrontendGame.fromId(request.getStringExtra("game"))
                ?: error("Unknown WiiDS game ID.")
        }
        if (request.action == Intent.ACTION_VIEW && uri?.scheme == "wiids") {
            require(uri.host == "play" && uri.pathSegments.size == 1 && uri.query == null && uri.fragment == null) {
                "Use wiids://play/<game-id>."
            }
            val linked = FrontendGame.fromId(uri.pathSegments.single()) ?: error("Unknown WiiDS game ID.")
            require(explicit == null || explicit == linked) { "Conflicting WiiDS game IDs." }
            return linked
        }
        require(request.action == ACTION_PLAY || request.action == Intent.ACTION_VIEW) { "Unsupported launch action." }
        if (uri == null) return explicit ?: error("Choose a WiiDS game or provide a local disc image.")
        val source = when (uri.scheme) {
            "content" -> WiiDsDisc.Source.Document(uri, "frontend disc")
            "file" -> fileSource(uri)
            else -> error("Use a local file or Android content URI.")
        }
        val info = WiiDsDisc.identify(this, source)
        val disc = GameCatalog.Disc.values().firstOrNull { it.matches(info) }
            ?: error("This disc region or revision is not supported by WiiDS.")
        val detected = when (disc) {
            GameCatalog.Disc.MARIO_KART_PAL -> FrontendGame.MARIO_KART
            GameCatalog.Disc.NSMB_USA_REV1 -> FrontendGame.NSMB
            GameCatalog.Disc.SUNSHINE_USA -> FrontendGame.SUNSHINE
            GameCatalog.Disc.ECLIPSE_110 -> FrontendGame.ECLIPSE
            GameCatalog.Disc.BRAWL_USA_REV2 -> FrontendGame.BRAWL
            GameCatalog.Disc.GALAXY_USA -> FrontendGame.GALAXY
        }
        val base = when (explicit) {
            FrontendGame.RETRO_REWIND -> FrontendGame.MARIO_KART
            FrontendGame.MSMBWII -> FrontendGame.NSMB
            FrontendGame.KAIZO -> FrontendGame.GALAXY
            else -> explicit
        }
        require(base == null || base == detected) { "The selected game does not match this disc." }
        return explicit ?: detected
    }

    private fun fileSource(uri: Uri): WiiDsDisc.Source.Files {
        require(uri.authority.isNullOrEmpty() && uri.query == null && uri.fragment == null) { "Invalid local disc URI." }
        val first = File(uri.path ?: error("Missing disc path."))
        require(first.isAbsolute && first.isFile) { "The selected disc is unavailable. Import it in WiiDS first, or grant file access." }
        val parts = mutableListOf(first)
        if (first.extension.equals("wbfs", true)) {
            for (part in 1..99) {
                val next = File(first.parentFile, first.nameWithoutExtension + ".wbf$part")
                if (!next.isFile) break
                parts += next
            }
        }
        return WiiDsDisc.Source.Files(parts)
    }

    private fun fail(message: String) {
        Toast.makeText(this, message, Toast.LENGTH_LONG).show()
        finish()
    }

    companion object { const val ACTION_PLAY = "io.github.maddex712.wiids.action.PLAY" }
}
