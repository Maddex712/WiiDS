package com.driftdroid.android

import android.app.Activity
import android.app.AlertDialog

/** Shared session menu for Sunshine and Eclipse. The host still owns game teardown. */
internal class WiiDsSessionDialog(
    private val activity: Activity,
    private val game: String,
    private val settings: Runnable,
    private val exit: Runnable,
) {
    private var dialog: AlertDialog? = null

    fun show() {
        if (dialog != null || activity.isFinishing || activity.isDestroyed) return
        val menu = ThorDialogs.builder(activity)
            .setTitle("$game · WiiDS")
            .setItems(arrayOf("Resume", "Game settings", "Return to WiiDS")) { _, row ->
                when (row) {
                    1 -> settings.run()
                    2 -> exit.run()
                }
            }.create()
        dialog = menu
        menu.setOnDismissListener { if (dialog === menu) dialog = null }
        menu.show()
    }

    fun dismiss() {
        dialog?.dismiss()
        dialog = null
    }
}
