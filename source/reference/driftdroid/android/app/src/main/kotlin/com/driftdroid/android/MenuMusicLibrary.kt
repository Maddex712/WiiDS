package com.driftdroid.android

import android.content.Context
import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.provider.OpenableColumns
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.security.MessageDigest
import java.util.concurrent.Executors

/** Packaged menu music and app-private copies of tracks added in Settings. */
internal object MenuMusicLibrary {
    private const val MAX_TRACK_BYTES = 32L * 1024 * 1024
    private const val MAX_TRACKS = 24
    private const val SELECTED = "selected"
    private const val TITLE_PREFIX = "track."
    private const val ORIGINAL_THEME = "original"
    // Reuse the imported files' hashes as IDs so existing selections resolve to the bundle.
    private val kart = Track.Bundled(
        "ca4177b12638c2f50db787d6d8ac994c30c6e81c3926f57f117f9c663743c5e2",
        "Kart Theme", "Noctron", R.raw.menu_music_kart,
    )
    private val wii = Track.Bundled(
        "3c794848864f82143b2a3cd644788a583dbac1116bf5eb8ce6a813dd98164578",
        "Wii Theme", "Noctron", R.raw.menu_music_wii,
    )
    val settingsTrack = Track.Bundled(
        "6ab6f33d4dff8bdaf63dd428f20c2615f4e2efbf8251a41a26c1425456319434",
        "Are You Supposed To Be Here? (Settings Panel)", "Noctron", R.raw.menu_music_settings,
    )
    private val bundledChoices = listOf(kart, wii)
    private val bundledIds = setOf(kart.id, wii.id, settingsTrack.id)
    private val trackId = Regex("[a-f0-9]{64}")
    private val worker = Executors.newSingleThreadExecutor { Thread(it, "MenuMusicImport").apply { isDaemon = true } }
    private val main = Handler(Looper.getMainLooper())

    sealed class Track(val id: String, val title: String) {
        val menuLabel: String
            get() = when (this) {
                is Bundled -> "$title by $artist"
                is Imported -> title
            }

        class Bundled(id: String, title: String, val artist: String, @androidx.annotation.RawRes val resource: Int) :
            Track(id, title)

        class Imported(id: String, title: String, val file: File) : Track(id, title)
    }
    data class ImportResult(val added: Int, val existing: Int, val skipped: Int)

    private fun prefs(context: Context) = context.getSharedPreferences("menu_music_library", Context.MODE_PRIVATE)
    private fun directory(context: Context) = File(context.filesDir, "menu-music")
    private fun audioFile(context: Context, id: String) = File(directory(context), "$id.audio")

    fun tracks(context: Context): List<Track> = bundledChoices + importedTracks(context)

    private fun importedTracks(context: Context): List<Track.Imported> = prefs(context).all.mapNotNull { (key, value) ->
        if (!key.startsWith(TITLE_PREFIX)) return@mapNotNull null
        val id = key.removePrefix(TITLE_PREFIX)
        val title = value as? String ?: return@mapNotNull null
        if (id in bundledIds || !trackId.matches(id)) return@mapNotNull null
        val file = audioFile(context, id)
        if (file.isFile && file.length() in 1..MAX_TRACK_BYTES) Track.Imported(id, title, file) else null
    }.sortedWith(compareBy(String.CASE_INSENSITIVE_ORDER) { it.title })

    fun selected(context: Context): Track? {
        val id = prefs(context).getString(SELECTED, null) ?: return kart
        if (id == ORIGINAL_THEME) return null
        return tracks(context).firstOrNull { it.id == id } ?: kart
    }

    fun select(context: Context, track: Track?) {
        prefs(context).edit().putString(SELECTED, track?.id ?: ORIGINAL_THEME).apply()
    }

    fun resetSelection(context: Context) {
        prefs(context).edit().remove(SELECTED).apply()
    }

    fun import(context: Context, uris: List<Uri>, onDone: (ImportResult) -> Unit) {
        val app = context.applicationContext
        worker.execute {
            val picked = uris.distinct()
            var added = 0
            var existing = 0
            var skipped = (picked.size - MAX_TRACKS).coerceAtLeast(0)
            for (uri in picked.take(MAX_TRACKS)) {
                when (runCatching { copyTrack(app, uri) }.getOrNull()) {
                    true -> added++
                    false -> existing++
                    null -> skipped++
                }
            }
            main.post { onDone(ImportResult(added, existing, skipped)) }
        }
    }

    /** true adds a track; false means the same audio file is already available. */
    private fun copyTrack(context: Context, uri: Uri): Boolean {
        require(uri.scheme == "content")
        val directory = directory(context)
        check(directory.isDirectory || directory.mkdirs())
        val draft = File.createTempFile("audio-", ".part", directory)
        try {
            val digest = MessageDigest.getInstance("SHA-256")
            val input = context.contentResolver.openInputStream(uri) ?: throw IOException("Cannot read audio")
            input.use { source ->
                FileOutputStream(draft).use { output ->
                    val buffer = ByteArray(64 * 1024)
                    var total = 0L
                    while (true) {
                        val read = source.read(buffer)
                        if (read == -1) break
                        total += read
                        require(read > 0 && total <= MAX_TRACK_BYTES)
                        output.write(buffer, 0, read)
                        digest.update(buffer, 0, read)
                    }
                    require(total > 0L)
                    output.fd.sync()
                }
            }
            val id = digest.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
            val file = audioFile(context, id)
            val store = prefs(context)
            if (id in bundledIds || (store.contains(TITLE_PREFIX + id) && file.isFile)) return false
            require(importedTracks(context).size < MAX_TRACKS)
            verifyAudio(draft)
            val title = readTitle(draft) ?: pickedTitle(context, uri) ?: "Imported track"

            // A file left by an interrupted import has the same hash and can be replaced safely.
            check(draft.renameTo(file))
            if (!store.edit().putString(TITLE_PREFIX + id, title).commit()) {
                file.delete()
                throw IOException("Cannot save track")
            }
            return true
        } finally {
            draft.delete()
        }
    }

    private fun verifyAudio(file: File) {
        val extractor = MediaExtractor()
        try {
            extractor.setDataSource(file.path)
            val audio = (0 until extractor.trackCount).firstOrNull { index ->
                extractor.getTrackFormat(index).getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true
            } ?: throw IOException("No audio track")
            extractor.selectTrack(audio)
            require(extractor.sampleTime >= 0)
        } finally {
            extractor.release()
        }
    }

    private fun readTitle(file: File): String? = runCatching {
        val retriever = MediaMetadataRetriever()
        try {
            retriever.setDataSource(file.path)
            cleanTag(retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_TITLE))
        } finally {
            retriever.release()
        }
    }.getOrNull()

    private fun pickedTitle(context: Context, uri: Uri): String? = runCatching {
        context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
            if (cursor.moveToFirst()) cleanTag(cursor.getString(0)?.substringBeforeLast('.')) else null
        }
    }.getOrNull()

    private fun cleanTag(title: String?): String? =
        title?.filterNot { it.isISOControl() }?.trim()?.take(96)?.takeIf { it.isNotBlank() }
}
