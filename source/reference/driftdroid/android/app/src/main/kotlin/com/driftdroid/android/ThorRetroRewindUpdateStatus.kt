package com.driftdroid.android

/** Immutable process-wide progress; Activities may poll while visible without owning the worker. */
internal object ThorRetroRewindUpdateStatus {
    enum class State {
        IDLE, CHECKING, READY, UP_TO_DATE, BLOCKED, ERROR, DOWNLOADING, VERIFYING,
        STAGING, COMMITTING, COMPLETE, CANCELLED,
    }

    data class Snapshot(
        val state: State = State.IDLE,
        val currentVersion: String? = null,
        val latestVersion: String? = null,
        val compatibleVersion: String? = null,
        val message: String = "Check for Retro Rewind updates.",
        val downloadedBytes: Long = 0,
        val totalBytes: Long = -1,
        val filesDone: Int = 0,
        val canUpdate: Boolean = false,
        val requiresWiiDsUpdate: Boolean = false,
        val running: Boolean = false,
    ) {
        val offerWiiDsUpdate: Boolean
            get() = requiresWiiDsUpdate && !running &&
                (state == State.BLOCKED || state == State.READY || state == State.COMPLETE)
    }

    @Volatile var snapshot = Snapshot()
        internal set
}
