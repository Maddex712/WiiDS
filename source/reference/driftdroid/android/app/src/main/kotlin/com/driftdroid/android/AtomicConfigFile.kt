package com.driftdroid.android

import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.nio.file.Files
import java.nio.file.StandardCopyOption

/** Publish complete file contents without truncating the previous copy. */
internal object AtomicConfigFile {
    fun writeText(file: File, text: String) = writeBytes(file, text.toByteArray(Charsets.UTF_8))

    @Synchronized
    fun writeBytes(file: File, bytes: ByteArray) {
        val target = file.absoluteFile
        val directory = target.parentFile ?: throw IOException("Config has no parent directory")
        if (!directory.isDirectory && !directory.mkdirs() && !directory.isDirectory) {
            throw IOException("Could not create the config directory")
        }
        val staged = File.createTempFile(".${target.name}.", ".tmp", directory)
        try {
            FileOutputStream(staged).use { output ->
                output.write(bytes)
                output.fd.sync()
            }
            // A sibling file keeps publication on the same filesystem. Do not
            // fall back to deleting/truncating the live config if this fails.
            Files.move(staged.toPath(), target.toPath(),
                StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
        } finally {
            staged.delete()
        }
    }
}
