package com.driftdroid.android

import android.app.Activity
import android.content.Intent
import android.os.Bundle

/** Public app icon. Internal game requests enter through FrontendLaunchActivity. */
class ModePickerActivity : Activity() {
    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        startActivity(Intent(this, LibraryActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_REORDER_TO_FRONT or Intent.FLAG_ACTIVITY_SINGLE_TOP))
        finish()
    }
}
