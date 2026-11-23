package com.driftdroid.android

import android.content.Context

/** Applies the 2.0 menu defaults to existing installations once. */
internal object LibraryMenuUpdate {
    private const val UPDATE_VERSION_CODE = 2000
    private const val NOTICE_PENDING = "menu_2000_notice_pending"

    private fun preferences(context: Context) =
        context.getSharedPreferences("library_updates", Context.MODE_PRIVATE)

    fun prepare(context: Context): Boolean {
        if (!BuildConfig.THOR_BUILD || BuildConfig.VERSION_CODE != UPDATE_VERSION_CODE) return false
        val state = preferences(context)
        if (state.contains(NOTICE_PENDING)) return state.getBoolean(NOTICE_PENDING, false)

        @Suppress("DEPRECATION")
        val info = runCatching { context.packageManager.getPackageInfo(context.packageName, 0) }
            .getOrNull() ?: return false
        val updated = info.firstInstallTime > 0 && info.lastUpdateTime > info.firstInstallTime
        if (updated) {
            ThorMenuTheme.set(context, ThorMenuTheme.Style.WII_CHANNELS)
            MenuMusicLibrary.resetSelection(context)
        }
        state.edit().putBoolean(NOTICE_PENDING, updated).apply()
        return updated
    }

    fun markNoticeShown(context: Context) {
        preferences(context).edit().putBoolean(NOTICE_PENDING, false).apply()
    }
}
