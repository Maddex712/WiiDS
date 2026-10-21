package com.driftdroid.android

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.system.Os
import android.widget.EditText

/**
 * Experimental: redirects Wii game-service DNS lookups (retail *.nintendowifi.net and Retro
 * Rewind's *.rwfc.net) to a self-hosted, compatible service instead of the real WFC/RWFC
 * infrastructure - see network_deferred.cpp's PrivateWfcHostOverride()/IsWiiServiceHostname(),
 * which is the only place this env var is read. Everything else the guest resolves (unrelated
 * TLS/HTTP hosts) is untouched.
 *
 * The env var is read once, at native-library load time (see MainActivity.onCreate(), which
 * calls [configureLaunch] before System.loadLibrary), so a change here only takes effect after
 * fully closing and reopening the app.
 */
internal object PrivateServerSettings {
    private const val ENVIRONMENT_VARIABLE = "WIICOMPILED_PRIVATE_WFC_HOST"
    private const val PREFS_NAME = "private_wfc_server"
    private const val KEY_HOST = "host"

    private fun preferences(context: Context) =
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    fun validHost(host: String): Boolean =
        host.length in 1..253 &&
            host.split('.').all { label ->
                label.length in 1..63 && label.first() != '-' && label.last() != '-' &&
                    label.all { c -> c in 'a'..'z' || c in 'A'..'Z' || c in '0'..'9' || c == '-' }
            }

    /** Must run before System.loadLibrary() - see MainActivity.onCreate(). */
    fun configureLaunch(context: Context) {
        val host = preferences(context).getString(KEY_HOST, "").orEmpty()
        try {
            Os.unsetenv(ENVIRONMENT_VARIABLE)
            if (validHost(host)) {
                Os.setenv(ENVIRONMENT_VARIABLE, host, true)
            }
        } catch (_: Exception) {
            // Best-effort: worst case online play just uses the real service, same as if this
            // setting had never been touched.
        }
    }

    fun show(activity: Activity) {
        val prefs = preferences(activity)
        val field = EditText(activity)
        field.isSingleLine = true
        field.hint = "Hostname or IPv4 address"
        field.inputType =
            android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_VARIATION_URI
        field.setText(prefs.getString(KEY_HOST, ""))

        val dialog =
            ThorDialogs.builder(activity)
                .setTitle("Experimental Server Settings")
                .setMessage(
                    if (BuildConfig.THOR_BUILD) {
                        "Use a compatible private Wii server. " +
                            "Close the app from Recents and reopen after saving."
                    } else {
                        "Redirects online play to a self-hosted, compatible Wii game service instead " +
                            "of the real WFC/Retro WFC servers. Requires an already-running compatible " +
                            "service - not another player's app or a generic web server. This does not " +
                            "restore original Nintendo WFC or provide native rooms. Fully close the app " +
                            "from Recents and reopen it for a change here to take effect."
                    },
                )
                .setView(field)
                .setNegativeButton("Cancel", null)
                .setNeutralButton("Use Default") { _, _ -> prefs.edit().remove(KEY_HOST).apply() }
                .setPositiveButton("Save for Next Launch", null)
                .create()
        dialog.setOnShowListener {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                val host = field.text.toString().trim()
                if (host.isNotEmpty() && !validHost(host)) {
                    field.error = "Enter a hostname or IPv4 address, without a scheme, port or path."
                } else {
                    prefs.edit().putString(KEY_HOST, host).apply()
                    dialog.dismiss()
                }
            }
        }
        dialog.show()
    }
}
