package com.driftdroid.android

import android.content.Context
import android.net.Uri
import com.driftdroid.android.rom.GameInstaller
import dev.wiids.brawl.BrawlStorage
import dev.wiids.brawl.ProjectPlusImport
import java.io.IOException

/** One application-owned worker; Activity recreation never drops the global import gate. */
internal object ProjectPlusInstaller {
    data class State(val running: Boolean = false, val phase: String = "", val bytes: Long = 0,
                     val cancellable: Boolean = true, val finished: Boolean = false, val error: String? = null)
    @Volatile var state = State()
        private set
    @Volatile private var cancelled = false

    fun cancel() { if (state.running && state.cancellable) cancelled = true }

    fun start(context: Context, zip: Uri? = null, card: Uri? = null, codes: Uri? = null) {
        require(zip != null && card == null && codes == null || zip == null && card != null && codes != null) {
            "Choose a Project+ ZIP or both extracted files."
        }
        for (uri in listOfNotNull(zip, card, codes)) require(uri.scheme == "content") { "Choose files with Android's file picker." }
        val app = context.applicationContext
        synchronized(GameInstaller) {
            SaveManagerProfiles.launchBlocker(app)?.let { throw IOException(it) }
            GameStorage.requireIdle(app)
            check(ThorGameLaunch.brawlInstalled(app)) { "Import your USA Brawl revision 2 disc first." }
            check(GameStorage.preparingProjectPlus.compareAndSet(false, true)) { "Project+ setup is already running." }
            cancelled = false
            state = State(running = true, phase = "Preparing Project+ import")
        }
        Thread({
            val result = runCatching {
                val root = BrawlStorage.projectPlusDirectory(app)
                val progress = ProjectPlusImport.Progress { phase, bytes ->
                    state = state.copy(phase = phase, bytes = bytes.coerceAtLeast(0), cancellable = bytes >= 0)
                }
                fun open(uri: Uri) = app.contentResolver.openInputStream(uri) ?: throw IOException("The chosen file could not be opened. Choose it again.")
                if (zip != null) open(zip).use { ProjectPlusImport.importZip(root, it, { cancelled }, progress) }
                else open(checkNotNull(card)).use { raw ->
                    open(checkNotNull(codes)).use { gct -> ProjectPlusImport.importFiles(root, raw, gct, { cancelled }, progress) }
                }
                GameStorage.enable(app, FrontendGame.PROJECT_PLUS)
            }
            // Publish the result only once all file handles/transaction recovery have finished.
            synchronized(GameInstaller) {
                state = state.copy(running = false, finished = true, error = result.exceptionOrNull()?.message,
                    phase = if (result.isSuccess) "Project+ is ready. Open its case in the Brawl stack." else "Import did not finish.")
                GameStorage.preparingProjectPlus.set(false)
            }
        }, "WiiDsProjectPlusImport").apply { isDaemon = true }.start()
    }
}
