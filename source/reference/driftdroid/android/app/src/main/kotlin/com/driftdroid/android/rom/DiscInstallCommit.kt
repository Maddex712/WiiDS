package com.driftdroid.android.rom

import java.io.File

/** Publishes only disc data. Save/settings folders are outside these sibling directories. */
internal object DiscInstallCommit {
    private fun backup(destination: File) = File(destination.parentFile, destination.name + ".previous-import")
    private fun complete(directory: File) = listOf("sys/main.dol", "game.iso").any {
        val marker = File(directory, it)
        marker.isFile && marker.length() > 0
    }

    /** A process killed between the two renames leaves the old complete import recoverable. */
    fun recover(destination: File): String? {
        val previous = backup(destination)
        if (!previous.exists()) return null
        // A concurrently recreated empty directory is not a committed new import.
        if (destination.isDirectory && destination.list()?.isEmpty() == true) destination.delete()
        if (destination.exists() && !complete(destination))
            return "The interrupted disc import needs recovery. Your previous disc backup and saves were kept."
        if (!destination.exists() && !previous.renameTo(destination))
            return "The previous disc import couldn't be restored. Its backup was kept; no saves were changed."
        if (previous.exists() && !previous.deleteRecursively())
            return "The previous disc backup couldn't be cleared. Your existing game and saves were kept."
        return null
    }

    fun publish(staged: File, destination: File,
                move: (File, File) -> Boolean = { from, to -> from.renameTo(to) }): String? {
        if (!complete(staged) || staged.canonicalFile.parentFile != destination.canonicalFile.parentFile ||
            staged.canonicalFile == destination.canonicalFile)
            return "The imported disc couldn't be published. Your existing game and saves were kept."
        recover(destination)?.let { return it }
        val previous = backup(destination)
        val hadPrevious = destination.exists()
        if (hadPrevious && !move(destination, previous))
            return "The existing disc couldn't be backed up. Your game and saves were kept."
        if (!move(staged, destination)) {
            val restored = !hadPrevious || move(previous, destination)
            return if (restored) "The new disc couldn't be installed. Your existing game and saves were kept."
            else "The new disc couldn't be installed. Your previous disc remains in its recovery backup; no saves were changed."
        }
        // A failed cleanup must not undo a complete new import. The next recovery retries it.
        previous.deleteRecursively()
        return null
    }
}
