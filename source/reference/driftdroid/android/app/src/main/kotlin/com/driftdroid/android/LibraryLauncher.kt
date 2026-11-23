package com.driftdroid.android

/** What the library needs from its upper-screen launcher: the case shelf or the Wii channel grid. */
internal interface LibraryLauncher {
    fun refreshSelection()
    fun onHostResumed()
    fun onHostPaused()
    val launchCommitted: Boolean
    fun launchFailed(message: String)
    fun redraw()
}
