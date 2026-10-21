package com.driftdroid.android

import java.io.File
import java.nio.file.Files
import java.nio.file.LinkOption

/** Only the runtime's text logs belong in a user-requested diagnostics archive. */
internal object DiagnosticsExportFiles {
    private val runName = Regex("(?:base|retro_rewind)_[0-9]+_pid[0-9]+")
    private val crashName = Regex("crash_[A-Za-z0-9_-]+\\.txt")

    fun isAllowed(root: File, file: File): Boolean {
        if (!Files.isDirectory(root.toPath(), LinkOption.NOFOLLOW_LINKS) ||
            !Files.isRegularFile(file.toPath(), LinkOption.NOFOLLOW_LINKS)) return false
        val parent = file.parentFile ?: return false
        if (file.name == "android_runtime_attempt.log" && parent == root)
            return file.canonicalFile.parentFile == root.canonicalFile
        val logs = File(root, "Logs")
        return (file.name == "console.log" || crashName.matches(file.name)) &&
            runName.matches(parent.name) && parent.parentFile == logs &&
            Files.isDirectory(logs.toPath(), LinkOption.NOFOLLOW_LINKS) &&
            Files.isDirectory(parent.toPath(), LinkOption.NOFOLLOW_LINKS) &&
            parent.canonicalFile.parentFile == logs.canonicalFile &&
            logs.canonicalFile.parentFile == root.canonicalFile &&
            file.canonicalFile.parentFile == parent.canonicalFile
    }

    fun collect(root: File): List<File> {
        val logs = File(root, "Logs")
        if (!Files.isDirectory(root.toPath(), LinkOption.NOFOLLOW_LINKS)) return emptyList()
        val runs = if (Files.isDirectory(logs.toPath(), LinkOption.NOFOLLOW_LINKS))
            logs.listFiles().orEmpty().filter { runName.matches(it.name) &&
                Files.isDirectory(it.toPath(), LinkOption.NOFOLLOW_LINKS) }.sortedByDescending { it.lastModified() }
            else emptyList()
        return (listOf(File(root, "android_runtime_attempt.log")) +
            runs.flatMap { it.listFiles().orEmpty().sortedBy { log -> log.name } }).filter { isAllowed(root, it) }
    }
}
