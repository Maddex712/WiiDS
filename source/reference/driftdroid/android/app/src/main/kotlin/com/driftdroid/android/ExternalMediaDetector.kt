package com.driftdroid.android

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioManager
import android.media.AudioPlaybackConfiguration
import android.os.Handler
import android.os.Looper

/**
 * Tells native whether another app is playing music, for "mute game music while external media
 * is playing". Purely passive: it watches the system's list of active players and never requests
 * audio focus. Every earlier attempt used audio focus, and any focus request made by the game
 * paused or ducked the player's own music app.
 *
 * A normal app only sees players that are currently playing, with just their usage and content
 * type, so an empty (or media-free) list is a reliable "the music stopped" signal. The game's own
 * stream is tagged USAGE_GAME (audio_backend.cpp) and so does not count.
 */
class ExternalMediaDetector(context: Context, private val report: (Boolean) -> Unit) {
    private val audioManager = context.getSystemService(AudioManager::class.java)
    private val handler = Handler(Looper.getMainLooper())
    private var reported: Boolean? = null
    private var pending: Runnable? = null
    private var registered = false

    private val callback =
        object : AudioManager.AudioPlaybackCallback() {
            override fun onPlaybackConfigChanged(configs: MutableList<AudioPlaybackConfiguration>) {
                evaluate(configs)
            }
        }

    private fun isExternalMedia(config: AudioPlaybackConfiguration): Boolean =
        when (config.audioAttributes.usage) {
            AudioAttributes.USAGE_MEDIA, AudioAttributes.USAGE_UNKNOWN -> true
            else -> false
        }

    private fun evaluate(configs: List<AudioPlaybackConfiguration>) {
        val playing = configs.any(::isExternalMedia)
        pending?.let(handler::removeCallbacks)
        pending = null
        if (playing == reported) return
        // Wait out short blips before muting, and track skips/buffering before unmuting.
        val runnable = Runnable {
            pending = null
            reported = playing
            report(playing)
        }
        pending = runnable
        handler.postDelayed(runnable, if (playing) START_DELAY_MS else STOP_DELAY_MS)
    }

    fun start() {
        val manager = audioManager ?: return
        if (registered) return
        manager.registerAudioPlaybackCallback(callback, handler)
        registered = true
        // Catch music started or stopped while the game was in the background.
        evaluate(manager.activePlaybackConfigurations)
    }

    fun stop() {
        if (!registered) return
        audioManager?.unregisterAudioPlaybackCallback(callback)
        registered = false
        pending?.let(handler::removeCallbacks)
        pending = null
    }

    companion object {
        const val START_DELAY_MS = 400L
        const val STOP_DELAY_MS = 1500L
    }
}
