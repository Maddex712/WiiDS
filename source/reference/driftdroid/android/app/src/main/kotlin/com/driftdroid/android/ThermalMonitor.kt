package com.driftdroid.android

import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.PowerManager

/**
 * Reports how close the phone is to thermal throttling so the runtime can trade resolution for
 * frame rate before the SoC does it for us (ThermalQuality in the runtime; see thermal_quality.h).
 *
 * getThermalHeadroom is API 31; on older devices this simply never reports and quality stays where
 * the player put it. The platform rate-limits the call and returns NaN if polled faster than about
 * once a second, so this stays well under that.
 */
class ThermalMonitor(context: Context, private val report: (Float) -> Unit) {
    private val powerManager = context.getSystemService(PowerManager::class.java)
    private val handler = Handler(Looper.getMainLooper())
    private var running = false

    private val tick =
        object : Runnable {
            override fun run() {
                if (!running) return
                sample()
                handler.postDelayed(this, POLL_INTERVAL_MS)
            }
        }

    private fun sample() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) return
        val manager = powerManager ?: return
        // The forecast argument is seconds ahead; 0 asks for right now.
        val headroom = runCatching { manager.getThermalHeadroom(0) }.getOrNull() ?: return
        if (headroom.isNaN() || headroom <= 0f) return
        report(headroom)
    }

    fun start() {
        if (running) return
        running = true
        handler.postDelayed(tick, POLL_INTERVAL_MS)
    }

    fun stop() {
        running = false
        handler.removeCallbacks(tick)
    }

    companion object {
        // Android's guidance is to poll on the order of ten seconds, not per frame.
        const val POLL_INTERVAL_MS = 10_000L
    }
}
