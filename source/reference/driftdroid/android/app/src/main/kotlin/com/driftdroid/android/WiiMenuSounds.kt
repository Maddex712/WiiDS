package com.driftdroid.android

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import kotlin.math.PI
import kotlin.math.exp
import kotlin.math.sin

/**
 * The Wii Home Menu theme's interface sounds: soft glassy chimes in the spirit of the Wii Menu, made here from
 * sine tones (no game or console audio is used). They follow the menu-music mute and volume setting.
 */
internal object WiiMenuSounds {
    enum class Cue { FOCUS, SELECT, BACK, LAUNCH, PAGE, REJECT, PICK, DROP }

    private const val RATE = 44100
    private val tracks = HashMap<Cue, AudioTrack>()
    private val last = HashMap<Cue, Long>()

    /** A short tone: (frequency Hz, start s, length s, level). */
    private data class Note(val hz: Double, val at: Double, val len: Double, val level: Double)

    private fun notes(cue: Cue): List<Note> = when (cue) {
        // A light tick, like the pointer passing over a channel.
        Cue.FOCUS -> listOf(Note(2093.0, 0.0, 0.06, 0.20), Note(4186.0, 0.0, 0.03, 0.06))
        // Two rising glassy notes.
        Cue.SELECT -> listOf(Note(1318.5, 0.0, 0.16, 0.30), Note(1975.5, 0.06, 0.22, 0.28))
        Cue.BACK -> listOf(Note(1567.98, 0.0, 0.14, 0.26), Note(1046.5, 0.06, 0.20, 0.24))
        // A rising arpeggio before the fade to white.
        Cue.LAUNCH -> listOf(Note(1046.5, 0.0, 0.30, 0.24), Note(1318.5, 0.08, 0.30, 0.24),
            Note(1567.98, 0.16, 0.34, 0.24), Note(2093.0, 0.24, 0.50, 0.24))
        Cue.PAGE -> listOf(Note(1760.0, 0.0, 0.09, 0.22), Note(2349.3, 0.04, 0.12, 0.18))
        Cue.REJECT -> listOf(Note(392.0, 0.0, 0.18, 0.28), Note(370.0, 0.10, 0.22, 0.26))
        Cue.PICK -> listOf(Note(987.77, 0.0, 0.10, 0.26), Note(1479.98, 0.05, 0.16, 0.24))
        Cue.DROP -> listOf(Note(1479.98, 0.0, 0.10, 0.24), Note(987.77, 0.05, 0.18, 0.26))
    }

    private fun build(cue: Cue): AudioTrack {
        val list = notes(cue)
        val seconds = list.maxOf { it.at + it.len } + 0.02
        val pcm = ShortArray((seconds * RATE).toInt())
        for (n in list) {
            val start = (n.at * RATE).toInt(); val count = (n.len * RATE).toInt()
            for (i in 0 until count) {
                val t = i.toDouble() / RATE
                val attack = (i / (RATE * 0.004)).coerceAtMost(1.0)
                val env = attack * exp(-t * 5.0 / n.len)
                // A touch of the octave makes it glassy rather than a pure beep.
                val v = (sin(2 * PI * n.hz * t) + 0.25 * sin(4 * PI * n.hz * t)) * env * n.level
                val k = start + i
                if (k < pcm.size) pcm[k] = (pcm[k] + v * 32767 * 0.8).toInt().coerceIn(-32767, 32767).toShort()
            }
        }
        val track = AudioTrack.Builder()
            .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build())
            .setAudioFormat(AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                .setSampleRate(RATE).setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build())
            .setTransferMode(AudioTrack.MODE_STATIC)
            .setBufferSizeInBytes(pcm.size * 2)
            .build()
        track.write(pcm, 0, pcm.size)
        return track
    }

    fun play(context: Context, cue: Cue) {
        if (ThorLauncherAudio.muted(context)) return
        val now = android.os.SystemClock.uptimeMillis()
        if (now - (last[cue] ?: 0L) < 60L) return
        last[cue] = now
        runCatching {
            val track = tracks.getOrPut(cue) { build(cue) }
            val volume = context.applicationContext.getSharedPreferences("thor_launcher_audio", Context.MODE_PRIVATE)
                .getInt("volume", 7).coerceIn(0, 10) / 10f
            track.setVolume(volume)
            if (track.playState == AudioTrack.PLAYSTATE_PLAYING) track.stop()
            track.reloadStaticData()
            track.play()
        }
    }
}
