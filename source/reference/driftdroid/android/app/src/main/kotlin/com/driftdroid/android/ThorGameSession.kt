package com.driftdroid.android

/** A native runtime survives Activity destruction; only process restart releases it. */
internal object ThorGameSession {
    private var retained = false
    private var updating = false

    @Synchronized fun hasRetainedRuntime(): Boolean = retained
    @Synchronized fun isUpdating(): Boolean = updating

    @Synchronized fun tryBeginRuntime(): Boolean {
        if (updating) return false
        retained = true
        return true
    }

    @Synchronized fun tryBeginUpdate(): Boolean {
        if (retained || updating) return false
        updating = true
        return true
    }

    @Synchronized fun endUpdate() { updating = false }
}
