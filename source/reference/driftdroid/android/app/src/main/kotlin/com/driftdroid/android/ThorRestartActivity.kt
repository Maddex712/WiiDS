package com.driftdroid.android

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.Process
import android.os.SystemClock
import android.system.ErrnoException
import android.system.Os
import android.system.OsConstants
import android.view.WindowInsets
import android.view.WindowInsetsController
import java.io.File

/** A foreground handoff in a small, separate process; never loads SDL or game libraries. */
class ThorRestartActivity : Activity() {
    private val handler = Handler(Looper.getMainLooper())
    private var page: ThorLowerStatusView? = null
    private var companion: ThorLowerCompanion? = null
    private var oldPid = -1
    private var deadline = 0L
    private var resumed = false
    private val awaitExit = object : Runnable {
        override fun run() {
            if (isFinishing || isDestroyed) return
            val gone = try { Os.kill(oldPid, 0); false }
                catch (e: ErrnoException) { e.errno == OsConstants.ESRCH }
            if (gone && resumed && hasWindowFocus()) {
                val next = Intent(this@ThorRestartActivity, ModePickerActivity::class.java)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                intent.getStringExtra(ModePickerActivity.EXTRA_RESTART_PRODUCT)?.let {
                    next.putExtra(ModePickerActivity.EXTRA_RESTART_PRODUCT, it)
                }
                startActivity(next)
                finish()
            } else if (SystemClock.uptimeMillis() < deadline) handler.postDelayed(this, 20)
            else page?.setStatus("Restart interrupted", "Close and reopen the app to continue.")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        oldPid = intent.getIntExtra(EXTRA_OLD_PID, -1)
        if (!BuildConfig.THOR_BUILD || oldPid <= 0 || oldPid == Process.myPid()) { finish(); return }
        val folder = File(filesDir, "WiiCompiled/ThorMap")
        val title = if (intent.getBooleanExtra(EXTRA_UPDATING, false)) "Retro Rewind updates" else "Mario Kart Wii"
        page = ThorLowerStatusView(this, folder, title).also {
            it.setStatus("Restarting", "Your saved progress stays in place.")
            setContentView(it)
        }
        companion = ThorLowerCompanion(this, folder).also {
            it.setStatus(title, "Restarting. Your saved progress stays in place.")
        }
        deadline = SystemClock.uptimeMillis() + 5000
        handler.post(awaitExit)
    }

    override fun onResume() {
        super.onResume()
        resumed = true
        handler.removeCallbacks(awaitExit)
        handler.post(awaitExit)
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            window.setDecorFitsSystemWindows(false)
            window.insetsController?.apply {
                systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                hide(WindowInsets.Type.systemBars())
            }
        }
        companion?.start()
    }
    override fun onPause() {
        resumed = false
        handler.removeCallbacks(awaitExit)
        companion?.stop()
        super.onPause()
    }
    override fun onDestroy() {
        handler.removeCallbacks(awaitExit)
        companion?.stop()
        page?.close()
        super.onDestroy()
    }

    companion object {
        internal const val EXTRA_OLD_PID = "thor_restart_old_pid"
        internal const val EXTRA_UPDATING = "thor_restart_updating"
    }
}
