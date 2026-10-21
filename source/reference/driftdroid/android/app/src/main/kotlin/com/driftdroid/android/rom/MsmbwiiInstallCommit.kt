package com.driftdroid.android.rom

import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.nio.file.Files

/** A mod replacement never moves the only copy of the player's save into disposable staging. */
internal object MsmbwiiInstallCommit {
    private const val JOURNAL = "wiids-msmbwii-install-v1\n"
    private fun backup(destination: File) = File(destination.parentFile, destination.name + ".previous-install")
    private fun journal(destination: File) = File(destination.parentFile, destination.name + ".install-journal")
    private fun complete(directory: File) =
        File(directory, "msmbwiijala/AnotherRes/SystemUS_1.bin").let { it.isFile && it.length() > 0 } &&
            File(directory, "riivolution/config/SMNE.xml").let { it.isFile && it.length() > 0 }

    /** Recover either side of the atomic directory rename before a new attempt touches staging. */
    fun recover(destination: File): String? {
        val previous = backup(destination)
        val record = journal(destination)
        if (Files.isSymbolicLink(destination.toPath()) || Files.isSymbolicLink(previous.toPath()) ||
            Files.isSymbolicLink(record.toPath())) return "The mod installation paths need recovery. Your files were kept."
        if (!previous.exists() && !record.exists()) return null
        if (!record.isFile || runCatching { record.readText() }.getOrNull() != JOURNAL)
            return "The interrupted mod installation needs recovery. Its backup and saves were kept."
        if (previous.exists()) {
            // A recreated empty folder is not a committed replacement and contains no save.
            if (destination.isDirectory && destination.list()?.isEmpty() == true) destination.delete()
            if (!destination.exists()) {
                if (!previous.renameTo(destination))
                    return "The previous mod couldn't be restored. Its backup and saves were kept."
            } else if (!complete(destination)) {
                return "The interrupted mod installation needs recovery. Both copies and saves were kept."
            } else if (!previous.deleteRecursively()) {
                return "The installed mod is ready, but its recovery backup couldn't be cleared. Your saves were kept."
            }
        }
        if (!record.delete()) return "The mod recovery record couldn't be cleared. Your files were kept."
        return null
    }

    /** Refuse to erase a save stranded by an older installer's move-before-commit behavior. */
    fun orphanedStagedSave(staged: File, destination: File): Boolean =
        !File(destination, "msmbwiijala/save").exists() &&
            File(staged, "msmbwiijala/save").walkTopDown().onFail { _, error -> throw error }.any { it.isFile }

    fun preserveSave(destination: File, staged: File, checkCancel: () -> Unit) {
        val source = File(destination, "msmbwiijala/save")
        if (!source.exists()) return
        if (!source.isDirectory || Files.isSymbolicLink(source.toPath())) throw IOException("The existing mod save couldn't be copied safely. Your files were kept.")
        val target = File(staged, "msmbwiijala/save")
        if (target.exists() && !target.deleteRecursively()) throw IOException("The staged mod save couldn't be prepared. Your files were kept.")
        for (file in source.walkTopDown().onFail { _, error -> throw error }) {
            checkCancel()
            if (Files.isSymbolicLink(file.toPath())) throw IOException("The existing mod save contains a linked file. Your files were kept.")
            val output = File(target, file.relativeTo(source).path)
            if (file.isDirectory) {
                if (!output.isDirectory && !output.mkdirs()) throw IOException("The mod save couldn't be copied. Check free storage.")
            } else {
                file.inputStream().use { input ->
                    FileOutputStream(output).use { stream ->
                        val buffer = ByteArray(64 * 1024)
                        while (true) {
                            checkCancel()
                            val count = input.read(buffer)
                            if (count < 0) break
                            stream.write(buffer, 0, count)
                        }
                        stream.fd.sync()
                    }
                }
            }
        }
        checkCancel()
    }

    fun publish(staged: File, destination: File, checkCancel: () -> Unit,
                move: (File, File) -> Boolean = { from, to -> from.renameTo(to) }): String? {
        if (!complete(staged) || staged.canonicalFile.parentFile != destination.canonicalFile.parentFile ||
            staged.canonicalFile == destination.canonicalFile || Files.isSymbolicLink(staged.toPath()))
            return "The staged mod couldn't be published. Your existing mod and saves were kept."
        recover(destination)?.let { return it }
        checkCancel()
        val previous = backup(destination)
        val record = journal(destination)
        val pending = File(record.parentFile, record.name + ".new")
        if (Files.isSymbolicLink(pending.toPath())) throw IOException("The mod recovery record couldn't be prepared safely. Your files were kept.")
        FileOutputStream(pending).use { stream -> stream.write(JOURNAL.toByteArray()); stream.fd.sync() }
        if (!pending.renameTo(record)) throw IOException("The mod recovery record couldn't be prepared. Your files were kept.")
        val hadPrevious = destination.exists()
        try {
            checkCancel()
            if (hadPrevious && !move(destination, previous)) throw IOException("The existing mod couldn't be backed up. Your files were kept.")
            checkCancel()
            if (!move(staged, destination)) throw IOException("The new mod couldn't be moved into place. Check free storage.")
        } catch (error: Exception) {
            // If either rename failed, preserve all copies until the previous tree is restored.
            if (previous.exists() && !destination.exists() && !runCatching { move(previous, destination) }.getOrDefault(false))
                return "The mod wasn't installed. Its previous version and saves remain in the recovery backup. Try again to restore them."
            if (!previous.exists()) record.delete()
            throw error
        }
        // Atomic staged->destination rename is the commit point. A late cancellation cannot
        // undo it; the save copy already belongs to the complete new tree. Cleanup is retryable.
        if (!previous.exists() || previous.deleteRecursively()) record.delete()
        return null
    }
}
