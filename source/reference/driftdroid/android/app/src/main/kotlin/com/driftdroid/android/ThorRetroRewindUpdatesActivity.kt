package com.driftdroid.android

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.TextView
import java.io.File

/** Checks may run over a paused game; installing requires a fresh, runtime-free process. */
class ThorRetroRewindUpdatesActivity : Activity() {
    private val handler = Handler(Looper.getMainLooper())
    private var page: ThorRetroRewindUpdatesView? = null
    private var companion: ThorLowerCompanion? = null
    private var lastSnapshot: ThorRetroRewindUpdateStatus.Snapshot? = null
    private var foreground = false
    private val refresh = object : Runnable {
        override fun run() {
            if (!foreground) return
            val snapshot = ThorRetroRewindUpdateStatus.snapshot
            if (snapshot != lastSnapshot) {
                lastSnapshot = snapshot
                page?.update(snapshot)
                companion?.setStatus("Retro Rewind updates", ThorRetroRewindUpdatesView.nativeMessage(
                    snapshot.message.ifBlank { "Continue on the upper screen." }))
            }
            handler.postDelayed(this, 250)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (!BuildConfig.THOR_BUILD || !BuildConfig.THOR_RETRO_REWIND) {
            finish()
            return
        }
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        val folder = File(filesDir, "WiiCompiled/ThorMap")
        page = runCatching {
            ThorRetroRewindUpdatesView(this, folder, ::checkForUpdates, ::updateOrCancel, ::leave)
        }.getOrNull()
        if (page == null) {
            setContentView(TextView(this).apply {
                text = "Import your original game and Thor menu artwork before opening Retro Rewind updates."
                setPadding(48, 48, 48, 48)
            })
            return
        }
        setContentView(page)
        companion = ThorLowerCompanion(this, folder)
        if (!ThorRetroRewindUpdateStatus.snapshot.running) {
            when {
                ThorUpdateRestart.requested(this) && !ThorGameSession.hasRetainedRuntime() -> {
                    installOrRecover()
                }
                ThorRetroRewindUpdateService.needsRecovery(this) && !ThorGameSession.hasRetainedRuntime() ->
                    ThorRetroRewindUpdateService.startRecovery(this)
                else -> checkForUpdates()
            }
        }
    }

    override fun onResume() {
        super.onResume()
        window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_FULLSCREEN or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
            View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
        if (android.os.Build.VERSION.SDK_INT >= 30) window.insetsController?.apply {
            systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            hide(WindowInsets.Type.systemBars())
        }
        foreground = true
        lastSnapshot = null
        companion?.start()
        handler.post(refresh)
    }

    override fun onPause() {
        foreground = false
        handler.removeCallbacks(refresh)
        companion?.stop()
        super.onPause()
    }

    override fun onDestroy() {
        handler.removeCallbacks(refresh)
        companion?.stop()
        super.onDestroy()
    }

    private fun checkForUpdates() {
        if (!ThorRetroRewindUpdateStatus.snapshot.running) ThorRetroRewindUpdateService.startCheck(this)
    }

    private fun updateOrCancel() {
        val snapshot = ThorRetroRewindUpdateStatus.snapshot
        if (snapshot.running) {
            if (snapshot.state.toString() != "COMMITTING") ThorRetroRewindUpdateService.cancel(this)
            return
        }
        if (!snapshot.canUpdate && !ThorRetroRewindUpdateService.needsRecovery(this)) return
        if (ThorGameSession.hasRetainedRuntime()) {
            val install = snapshot.currentVersion == null
            ThorDialogs.builder(this)
                .setTitle(if (install) "Close game and install?" else "Close game and update?")
                .setMessage("Your current race will end. The app will restart to " +
                    (if (install) "install" else "update") + " Retro Rewind safely. Saved progress stays in place.")
                .setNegativeButton("Back", null)
                .setPositiveButton(if (install) "Close game and install" else "Close game and update") { _, _ -> restartForUpdate() }
                .show()
        } else {
            installOrRecover()
        }
    }

    private fun installOrRecover() {
        if (ThorRetroRewindUpdateService.needsRecovery(this)) ThorRetroRewindUpdateService.startRecovery(this)
        else ThorRetroRewindUpdateService.startUpdate(this)
        if (ThorUpdateRestart.requested(this)) ThorUpdateRestart.clear(this)
    }

    private fun restartForUpdate() {
        // The foreground handoff outlives the old game process. A persistent user request
        // also lets an ordinary launcher opening recover an interrupted handoff.
        ThorUpdateRestart.restart(this)
    }

    @Suppress("DEPRECATION")
    override fun onBackPressed() = leave()

    private fun leave() {
        // A background download remains in its foreground service. Never start another game
        // until its update lease has been released; ModePicker enforces that boundary too.
        if (isTaskRoot) startActivity(Intent(this, ModePickerActivity::class.java))
        finish()
    }

}
