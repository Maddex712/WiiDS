package com.driftdroid.android

import android.content.Context
import android.media.AudioAttributes
import android.media.SoundPool

/**
 * The launcher's UI click sound (res/raw/ui_click.wav, synthesized) - a single shared SoundPool rather than a
 * MediaPlayer per tap, since MediaPlayer's startup latency (even for a short clip) is noticeable
 * for a "press this button" sound that needs to feel instant; SoundPool decodes once up front and
 * plays from memory. Lazily created on first use rather than in a static initializer so it only
 * ever needs an Android Context once a screen that actually wants sound has been reached.
 */
object UiSounds {
    private var soundPool: SoundPool? = null
    private var clickSoundId: Int = 0
    private var clickSoundReady = false

    fun playClick(context: Context) {
        val pool = soundPool ?: create(context)
        // load() is async - clickSoundId is a valid (nonzero) load request id the instant load()
        // returns, well before the clip is actually decoded, so playing on that alone silently
        // no-ops for every tap until decode finishes. clickSoundReady (set by the
        // OnLoadCompleteListener below) is the actual "safe to play" signal.
        if (clickSoundReady) {
            pool.play(clickSoundId, 1f, 1f, 0, 0, 1f)
        }
    }

    private fun create(context: Context): SoundPool {
        // USAGE_GAME, not USAGE_ASSISTANCE_SONIFICATION: the latter routes to the
        // system/accessibility volume stream, which is a DIFFERENT slider than the one most
        // people actually have turned up for a game (media/game volume) - confirmed directly:
        // the click was completely inaudible with the sonification usage even at full media
        // volume, because that stream was effectively muted independent of it.
        val attributes = AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_GAME)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
            .build()
        val pool = SoundPool.Builder().setMaxStreams(4).setAudioAttributes(attributes).build()
        pool.setOnLoadCompleteListener { _, sampleId, status ->
            if (status == 0 && sampleId == clickSoundId) {
                clickSoundReady = true
            }
        }
        clickSoundId = pool.load(context, R.raw.ui_click, 1)
        soundPool = pool
        return pool
    }
}
