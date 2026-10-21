package com.driftdroid.android

/**
 * Shared, in-process status for whatever Retro Rewind install [RetroRewindInstallService] is
 * currently running. The copy/extract itself no longer lives inside any Activity instance (that
 * was the whole point of moving it into a foreground service - see the service's own doc comment
 * for why), so ModePickerActivity can't just hold an IntArray/poller closure over its own thread
 * anymore. Instead the service writes progress here as it works, and any ModePickerActivity
 * instance - including a freshly recreated one, e.g. after the original one was reclaimed by the
 * OS while backgrounded - can poll this directly (same process, so no IPC needed) to resume
 * showing live progress instead of losing track of an install that's still actually running.
 */
internal object RetroRewindInstallStatus {
    @Volatile var running: Boolean = false
    @Volatile var label: String = ""
    @Volatile var filesDone: Int = 0
    @Volatile var downloadedBytes: Long = 0
    @Volatile var totalBytes: Long = 0
    @Volatile var error: String? = null
    @Volatile var finished: Boolean = false

    /** Set by flows whose success isn't "N files copied" - an update that applied deltas, or one
     * that found nothing to do. Null means the generic installed-N-files wording. */
    @Volatile var successMessage: String? = null

    /** True while the install directory is being rebuilt from scratch and is not yet whole, i.e.
     * the one situation in which a failure leaves something that should be cleaned up. A failure
     * at any other time (a refused version, a failed update check, no network) has not touched
     * the player's install and must leave it where it is. Not reset by [start], which runs once
     * per progress label - the service clears it when a run begins. */
    @Volatile var installIncomplete: Boolean = false

    fun start(label: String) {
        running = true
        this.label = label
        filesDone = 0
        downloadedBytes = 0
        totalBytes = 0
        error = null
        finished = false
        successMessage = null
    }

    fun complete(error: String?) {
        this.error = error
        finished = true
        running = false
    }
}
