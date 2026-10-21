package com.driftdroid.android

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import android.media.SoundPool
import android.os.Handler
import android.os.Looper
import android.util.Log
import org.json.JSONObject
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.file.Files
import java.security.MessageDigest
import java.util.concurrent.Executors

/**
 * Launcher music and UI sounds from the owner's own disc, rendered privately by
 * scripts/Extract-ThorLauncherAudio.py into WiiCompiled/ThorMap/launcher-audio. Nothing
 * here is bundled in the APK; without that folder the launcher is simply silent.
 *
 * One process-wide music track: Activity or lower-display recreation calls [show] again
 * and reuses it, so music can never overlap itself. The title theme loops with its
 * original loop points on a static AudioTrack, which is gapless.
 */
internal object ThorLauncherAudio {
    enum class Sound { FOCUS, OPEN, CLOSE, SELECT, LAUNCH, REJECT }

    private const val TAG = "ThorLauncherAudio"
    private const val PREFS = "thor_launcher_audio"
    private const val MAX_MUSIC_BYTES = 16L * 1024 * 1024
    private val main = Handler(Looper.getMainLooper())
    private val worker = Executors.newSingleThreadExecutor { Thread(it, "ThorLauncherAudio").apply { isDaemon = true } }

    private data class Music(val pcm: ByteArray, val rate: Int, val channels: Int, val loopStart: Int, val loopEnd: Int)

    private var app: Context? = null
    private var folder: File? = null
    private var track: AudioTrack? = null
    private var music: Music? = null
    private var loading = false
    private var visible = false
    private var focused = false
    private var launching = false
    private var gain = 0f          // current fade gain 0..1
    private var target = 0f        // fade destination
    private var fadeRate = 0f      // gain per ms
    private var lastStep = 0L
    private var focusRequest: AudioFocusRequest? = null
    private var pool: SoundPool? = null
    private val sounds = HashMap<Sound, Int>()
    private val ready = HashSet<Int>()
    private var lastSound = HashMap<Sound, Long>()

    // --- user settings (Wii-style 0..10 volume and a mute switch) ---
    fun volume(context: Context): Int = prefs(context).getInt("volume", 6).coerceIn(0, 10)
    fun muted(context: Context): Boolean = prefs(context).getBoolean("muted", false)
    fun setVolume(context: Context, value: Int) { prefs(context).edit().putInt("volume", value.coerceIn(0, 10)).apply(); applyGain() }
    fun setMuted(context: Context, value: Boolean) { prefs(context).edit().putBoolean("muted", value).apply(); applyGain() }
    fun available(): Boolean = music != null || loading
    private fun prefs(context: Context) = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    /** The launcher is visible: load once, take audio focus and fade the theme in. */
    fun show(context: Context, thorMap: File) {
        app = context.applicationContext
        val dir = ThorLauncherFolders.audio(context, thorMap)
        if (folder != dir) { folder = dir; sounds.clear(); ready.clear(); pool?.release(); pool = null }
        visible = true
        launching = false
        loadSounds()
        if (music == null) { load(dir); return }
        startMusic(fadeMs = 1400)
    }

    /** Home, sleep, another Activity: pause where we are and give focus back. */
    fun hide() {
        visible = false
        main.removeCallbacks(fade)
        track?.let { runCatching { it.pause() } }
        gain = 0f; target = 0f
        abandonFocus()
    }

    /** The game is about to start: fade out completely, then free the music buffer. */
    fun fadeForLaunch(ms: Long) {
        launching = true
        fadeTo(0f, ms) {
            track?.let { runCatching { it.pause(); it.flush() }; it.release() }
            track = null
            abandonFocus()
        }
    }

    fun play(sound: Sound) {
        val id = sounds[sound] ?: return
        if (id !in ready) return
        val now = android.os.SystemClock.uptimeMillis()
        // A held or repeated button never machine-guns the same cue.
        if (now - (lastSound[sound] ?: 0L) < 70L) return
        lastSound[sound] = now
        pool?.play(id, 0.62f, 0.62f, 1, 0, 1f)
    }

    private fun musicGain(): Float {
        val context = app ?: return 0f
        if (muted(context)) return 0f
        val v = volume(context) / 10f
        return v * v * 0.9f // perceptual curve; 6/10 is a comfortable default
    }

    private fun applyGain() { track?.let { runCatching { it.setVolume(gain * musicGain()) } } }

    private fun startMusic(fadeMs: Long) {
        if (!visible || launching) return
        val data = music ?: return
        if (!requestFocus()) return
        val t = track ?: createTrack(data) ?: return
        runCatching { t.setVolume(0f); if (t.playState != AudioTrack.PLAYSTATE_PLAYING) t.play() }
            .onFailure { Log.w(TAG, "Music playback unavailable", it); return }
        gain = 0f
        fadeTo(1f, fadeMs)
    }

    private fun createTrack(data: Music): AudioTrack? = runCatching {
        val frameBytes = 2 * data.channels
        val t = AudioTrack.Builder()
            .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
            .setAudioFormat(AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                .setSampleRate(data.rate)
                .setChannelMask(if (data.channels == 2) AudioFormat.CHANNEL_OUT_STEREO else AudioFormat.CHANNEL_OUT_MONO).build())
            .setTransferMode(AudioTrack.MODE_STATIC)
            .setBufferSizeInBytes(data.pcm.size)
            .build()
        t.write(data.pcm, 0, data.pcm.size)
        val frames = data.pcm.size / frameBytes
        // Native title loop: intro once, then loopStart..loopEnd forever without a gap.
        if (data.loopEnd in (data.loopStart + 1)..frames) t.setLoopPoints(data.loopStart, data.loopEnd, -1)
        track = t
        t
    }.onFailure { Log.w(TAG, "Cannot create music track", it) }.getOrNull()

    private fun fadeTo(value: Float, ms: Long, done: (() -> Unit)? = null) {
        target = value
        fadeRate = if (ms <= 0) 1f else 1f / ms
        fadeDone = done
        lastStep = android.os.SystemClock.uptimeMillis()
        main.removeCallbacks(fade)
        main.post(fade)
    }

    private var fadeDone: (() -> Unit)? = null
    private val fade = object : Runnable {
        override fun run() {
            val now = android.os.SystemClock.uptimeMillis()
            val step = (now - lastStep) * fadeRate
            lastStep = now
            gain = if (gain < target) minOf(target, gain + step) else maxOf(target, gain - step)
            applyGain()
            if (gain != target) main.postDelayed(this, 16L)
            else { val d = fadeDone; fadeDone = null; d?.invoke() }
        }
    }

    // --- audio focus: another app or the game's own audio always wins ---
    private val focusListener = AudioManager.OnAudioFocusChangeListener { change ->
        when (change) {
            AudioManager.AUDIOFOCUS_LOSS, AudioManager.AUDIOFOCUS_LOSS_TRANSIENT,
            AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK -> {
                focused = false
                main.removeCallbacks(fade)
                track?.let { runCatching { it.pause() } }
                gain = 0f
            }
            AudioManager.AUDIOFOCUS_GAIN -> { focused = true; if (visible && !launching) startMusic(900) }
        }
    }

    private fun requestFocus(): Boolean {
        if (focused) return true
        val manager = app?.getSystemService(AudioManager::class.java) ?: return false
        val request = focusRequest ?: AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
            .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
            .setOnAudioFocusChangeListener(focusListener, main).build().also { focusRequest = it }
        focused = manager.requestAudioFocus(request) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED
        return focused
    }

    private fun abandonFocus() {
        val manager = app?.getSystemService(AudioManager::class.java) ?: return
        focusRequest?.let { manager.abandonAudioFocusRequest(it) }
        focused = false
    }

    // --- private asset loading (validated against the manifest, off the main thread) ---
    private fun manifest(dir: File): JSONObject? = runCatching {
        val file = File(dir, "audio.json")
        if (!file.isFile || Files.isSymbolicLink(file.toPath()) || file.length() !in 1L..32768L) return null
        JSONObject(file.readText()).takeIf { it.getInt("schema") == 1 && it.getString("kind") == "private-launcher-audio" }
    }.getOrNull()

    private fun checked(dir: File, entry: JSONObject, limit: Long): File? {
        val name = entry.getString("file")
        if (!name.matches(Regex("[a-z]+\\.wav"))) return null
        val file = File(dir, name)
        if (!file.isFile || Files.isSymbolicLink(file.toPath()) || file.canonicalFile.parentFile != dir.canonicalFile ||
            file.length() !in 44L..limit) return null
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().use { input -> val b = ByteArray(65536); while (true) { val n = input.read(b); if (n < 0) break; digest.update(b, 0, n) } }
        val hash = digest.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
        return file.takeIf { hash == entry.getString("sha256").lowercase() }
    }

    private fun load(dir: File) {
        if (loading) return
        loading = true
        worker.execute {
            val result = runCatching {
                val meta = manifest(dir) ?: return@runCatching null
                val entry = meta.getJSONObject("music")
                val file = checked(dir, entry, MAX_MUSIC_BYTES) ?: return@runCatching null
                val (pcm, rate, channels) = readWav(file) ?: return@runCatching null
                val frames = pcm.size / (2 * channels)
                Music(pcm, rate, channels, entry.optInt("loopStart", 0).coerceIn(0, frames - 1),
                    (entry.optInt("loopEnd", frames - 1) + 1).coerceIn(1, frames)) // manifest end is inclusive
            }.onFailure { Log.w(TAG, "Launcher music unavailable", it) }.getOrNull()
            main.post {
                loading = false
                if (dir != folder) return@post
                music = result
                if (result == null) Log.i(TAG, "No private launcher music staged (launcher-audio/audio.json)")
                startMusic(fadeMs = 1400)
            }
        }
    }

    private fun loadSounds() {
        if (pool != null) return
        val dir = folder ?: return
        val context = app ?: return
        val created = SoundPool.Builder().setMaxStreams(4).setAudioAttributes(AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build()).build()
        created.setOnLoadCompleteListener { _, id, status -> if (status == 0) ready.add(id) }
        pool = created
        worker.execute {
            val files = runCatching {
                val meta = manifest(dir) ?: return@runCatching emptyMap()
                val list = meta.getJSONObject("sounds")
                Sound.values().mapNotNull { s ->
                    val key = s.name.lowercase()
                    if (!list.has(key)) null else checked(dir, list.getJSONObject(key), 4L * 1024 * 1024)?.let { s to it }
                }.toMap()
            }.getOrDefault(emptyMap())
            main.post {
                if (pool !== created) return@post
                if (files.isEmpty()) fallbackClick(context, created)
                files.forEach { (s, f) -> sounds[s] = created.load(f.path, 1) }
            }
        }
    }

    /** Without the private set, keep the existing bundled click for confirm-type cues. */
    private fun fallbackClick(context: Context, created: SoundPool) {
        val id = created.load(context, R.raw.ui_click, 1)
        for (s in listOf(Sound.OPEN, Sound.SELECT, Sound.CLOSE)) sounds[s] = id
    }

    /** Minimal RIFF reader: 16-bit PCM, mono or stereo, no compressed formats. */
    private fun readWav(file: File): Triple<ByteArray, Int, Int>? {
        RandomAccessFile(file, "r").use { raf ->
            val header = ByteArray(12); raf.readFully(header)
            if (String(header, 0, 4) != "RIFF" || String(header, 8, 4) != "WAVE") return null
            var rate = 0; var channels = 0; var bits = 0
            while (raf.filePointer + 8 <= raf.length()) {
                val chunk = ByteArray(8); raf.readFully(chunk)
                val id = String(chunk, 0, 4)
                val size = ByteBuffer.wrap(chunk, 4, 4).order(ByteOrder.LITTLE_ENDIAN).int
                if (size < 0 || raf.filePointer + size > raf.length()) return null
                when (id) {
                    "fmt " -> {
                        val fmt = ByteArray(size); raf.readFully(fmt)
                        val b = ByteBuffer.wrap(fmt).order(ByteOrder.LITTLE_ENDIAN)
                        if (b.short.toInt() != 1) return null
                        channels = b.short.toInt(); rate = b.int; b.int; b.short; bits = b.short.toInt()
                    }
                    "data" -> {
                        if (bits != 16 || channels !in 1..2 || rate !in 8000..48000) return null
                        val pcm = ByteArray(size); raf.readFully(pcm)
                        return Triple(pcm, rate, channels)
                    }
                    else -> raf.seek(raf.filePointer + size + (size and 1))
                }
            }
        }
        return null
    }
}
