package com.driftdroid.android.rom

import android.content.Context
import java.io.File

/**
 * Finds the player's disc images on device storage and SD cards (needs All files access on
 * Android 11+, see [RomScanner.hasAccess]). Reads only disc headers; the game ID, region and
 * revision decide, not the file name.
 */
object GameScanner {
    data class Found(val source: WiiDsDisc.Source.Files, val info: WiiDsDisc.Info)

    /** A file that looks like a disc image but couldn't be identified (damaged, incomplete...). */
    data class Unreadable(val file: File, val reason: String)

    data class Result(val found: List<Found>, val unreadable: List<Unreadable>) {
        /** Images that are exactly the disc [disc] needs. */
        fun matching(disc: GameCatalog.Disc) = found.filter { disc.matches(it.info) }

        /** Images of the same game that can't be used, with the reason. */
        fun mismatched(disc: GameCatalog.Disc) = found.filter { disc.sameGame(it.info) && !disc.matches(it.info) }
            .map { it to disc.mismatch(it.info)!! }
    }

    private const val MAX_DEPTH = 7
    private const val MAX_DIRECTORIES = 20000
    private val extensions = setOf("iso", "gcm", "wbfs", "rvz", "wia", "gcz", "ciso")
    /** Large media folders that never hold disc images (Android/ is app-private anyway). */
    private val skip = setOf("android", "dcim", "pictures", "movies", "music", "alarms", "ringtones",
        "notifications", "podcasts", "audiobooks", "recordings", ".thumbnails", "lost.dir")

    /** Blocking; call off the main thread. */
    fun scan(context: Context, cancelled: () -> Boolean = { false }): Result {
        val found = ArrayList<Found>()
        val unreadable = ArrayList<Unreadable>()
        val queue = ArrayDeque<Pair<File, Int>>()
        RomScanner.roots(context).forEach { queue.add(it to 0) }
        var visited = 0
        while (queue.isNotEmpty() && visited < MAX_DIRECTORIES && !cancelled()) {
            val (dir, depth) = queue.removeFirst()
            visited++
            val entries = dir.listFiles() ?: continue
            for (entry in entries) {
                val name = entry.name
                if (entry.isDirectory) {
                    if (depth < MAX_DEPTH && !name.startsWith('.') && name.lowercase() !in skip) queue.add(entry to depth + 1)
                    continue
                }
                if (name.substringAfterLast('.', "").lowercase() !in extensions || entry.length() < 1_000_000L) continue
                val source = WiiDsDisc.Source.Files(listOf(entry) + splitParts(entry))
                try {
                    found.add(Found(source, WiiDsDisc.identify(context, source)))
                } catch (e: Exception) {
                    unreadable.add(Unreadable(entry, e.message ?: "It couldn't be read."))
                }
            }
        }
        return Result(found.sortedBy { it.source.name.lowercase() }, unreadable)
    }

    /** A split WBFS continues in game.wbf1, game.wbf2... next to game.wbfs. */
    private fun splitParts(first: File): List<File> {
        if (!first.name.endsWith(".wbfs", ignoreCase = true)) return emptyList()
        val base = first.name.dropLast(5)
        val parts = ArrayList<File>()
        for (i in 1..9) {
            val part = File(first.parentFile, "$base.wbf$i").takeIf { it.isFile }
                ?: File(first.parentFile, "$base.WBF$i").takeIf { it.isFile } ?: break
            parts.add(part)
        }
        return parts
    }
}
