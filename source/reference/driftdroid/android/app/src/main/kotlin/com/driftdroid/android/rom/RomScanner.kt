package com.driftdroid.android.rom

import android.content.Context
import android.os.Build
import android.os.Environment
import android.os.storage.StorageManager
import java.io.File
import java.io.RandomAccessFile

/**
 * Finds the player's own Mario Kart Wii disc image on device storage, so setup needs no file
 * picker. Reads only disc headers (a few hundred bytes per file): the game ID decides, not the
 * file name. Needs "All files access" on Android 11+ to see shared storage beyond media files.
 */
object RomScanner {
    enum class Kind { ISO, WBFS, RVZ_UNSUPPORTED }
    data class Candidate(val file: File, val kind: Kind, val gameId: String) {
        val supported get() = kind != Kind.RVZ_UNSUPPORTED && gameId == "RMCP01"
    }

    private const val WII_MAGIC = 0x5D1C9EA3
    private const val MAX_DEPTH = 7
    private const val MAX_DIRECTORIES = 20000
    private val extensions = setOf("iso", "gcm", "wbfs", "rvz")
    /** Large app-private or media folders that never hold disc images. */
    private val skip = setOf("android", "dcim", "pictures", "movies", "music", "alarms", "ringtones",
        "notifications", "podcasts", "audiobooks", "recordings", ".thumbnails", "lost.dir")

    fun hasAccess(): Boolean =
        Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager()

    /** Every storage root: internal shared storage plus SD cards/USB drives. */
    fun roots(context: Context): List<File> {
        val result = LinkedHashSet<File>()
        @Suppress("DEPRECATION")
        result.add(Environment.getExternalStorageDirectory())
        if (Build.VERSION.SDK_INT >= 30) {
            context.getSystemService(StorageManager::class.java)?.storageVolumes?.forEach { volume ->
                volume.directory?.let { result.add(it) }
            }
        }
        return result.filter { it.isDirectory && it.canRead() }
    }

    /** Blocking; call off the main thread. Best matches (supported RMCP01) come first. */
    fun scan(context: Context, cancelled: () -> Boolean = { false }): List<Candidate> {
        val found = ArrayList<Candidate>()
        val queue = ArrayDeque<Pair<File, Int>>()
        roots(context).forEach { queue.add(it to 0) }
        var visited = 0
        while (queue.isNotEmpty() && visited < MAX_DIRECTORIES && !cancelled()) {
            val (dir, depth) = queue.removeFirst()
            visited++
            val entries = dir.listFiles() ?: continue
            for (entry in entries) {
                val name = entry.name
                if (entry.isDirectory) {
                    if (depth < MAX_DEPTH && !name.startsWith('.') && name.lowercase() !in skip) queue.add(entry to depth + 1)
                } else if (name.substringAfterLast('.', "").lowercase() in extensions && entry.length() > 1_000_000L) {
                    identify(entry)?.let { found.add(it) }
                }
            }
        }
        return found.sortedWith(compareByDescending<Candidate> { it.supported }.thenBy { it.file.name.lowercase() })
    }

    /** Game ID from the disc header; null when the file is not a Wii disc image. */
    fun identify(file: File): Candidate? = runCatching {
        RandomAccessFile(file, "r").use { raf ->
            val head = ByteArray(0x220)
            raf.readFully(head, 0, minOf(head.size.toLong(), raf.length()).toInt())
            fun id(at: Int) = String(head, at, 6, Charsets.US_ASCII)
            fun magic(at: Int) = ((head[at].toInt() and 255) shl 24) or ((head[at + 1].toInt() and 255) shl 16) or
                ((head[at + 2].toInt() and 255) shl 8) or (head[at + 3].toInt() and 255)
            when {
                // Plain ISO/GCM: game ID at 0, Wii magic at 0x18.
                magic(0x18) == WII_MAGIC -> Candidate(file, Kind.ISO, id(0))
                // WBFS: "WBFS" header, then the disc header at 0x200.
                String(head, 0, 4, Charsets.US_ASCII) == "WBFS" && magic(0x218) == WII_MAGIC ->
                    Candidate(file, Kind.WBFS, id(0x200))
                // RVZ is compressed; the game ID sits in its embedded disc header copy.
                String(head, 0, 3, Charsets.US_ASCII) == "RVZ" -> Candidate(file, Kind.RVZ_UNSUPPORTED,
                    String(head, 0x58, 6, Charsets.US_ASCII).takeIf { it.all { c -> c.isLetterOrDigit() } } ?: "?")
                else -> null
            }
        }
    }.getOrNull()
}
