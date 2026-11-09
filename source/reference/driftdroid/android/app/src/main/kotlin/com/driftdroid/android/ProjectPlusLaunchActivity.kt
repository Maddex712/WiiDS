package com.driftdroid.android

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.widget.TextView
import android.widget.Toast
import com.driftdroid.android.rom.GameInstaller
import dev.wiids.brawl.BrawlStorage
import dev.wiids.brawl.ProjectPlusGameActivity

/** Prepares the installed mod on a worker before starting its isolated SDL process. */
class ProjectPlusLaunchActivity : Activity() {
    private var preparing = false

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        if (intent.action != "dev.thor.launcher.action.PLAY_GAME" ||
            intent.getIntExtra("thor_protocol", 0) != 1 ||
            intent.getStringExtra("thor_game") != FrontendGame.PROJECT_PLUS.id) {
            finish()
            return
        }
        val allowed = runCatching {
            synchronized(GameInstaller) {
                SaveManagerProfiles.blocker(this)?.let { error(it) }
                SaveManagerProfiles.requireStopped(this)
                check(ThorGameLaunch.projectPlusAvailable(this)) { "Set up Project+ in Manage games first." }
                check(GameStorage.preparingProjectPlus.compareAndSet(false, true)) { "Project+ setup is already running." }
            }
        }
        if (allowed.isFailure) {
            fail(allowed.exceptionOrNull()?.message)
            return
        }
        preparing = true
        setContentView(TextView(this).apply {
            text = if (BrawlStorage.projectPlusInstalled(this@ProjectPlusLaunchActivity)) "Opening Project+…"
                   else "Preparing your existing Project+ files. This may take a minute."
            textSize = 22f
            setPadding(32, 32, 32, 32)
        })
        Thread({
            val result = runCatching { BrawlStorage.prepareProjectPlus(applicationContext) }
            runOnUiThread {
                GameStorage.preparingProjectPlus.set(false)
                preparing = false
                if (isFinishing || isDestroyed) return@runOnUiThread
                result.onSuccess {
                    startActivity(Intent(this, ProjectPlusGameActivity::class.java)
                        .putExtra("thor_launcher_session", true))
                    finish()
                }.onFailure { fail(it.message) }
            }
        }, "WiiDsProjectPlusSetup").start()
    }

    @Deprecated("Keep the setup activity visible while it publishes the copied files.")
    override fun onBackPressed() {
        if (!preparing) super.onBackPressed()
    }

    private fun fail(message: String?) {
        Toast.makeText(this, message ?: "Project+ could not be opened.", Toast.LENGTH_LONG).show()
        finish()
    }
}
