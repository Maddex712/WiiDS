package com.driftdroid.android

import java.io.File
import java.io.IOException
import java.io.InputStream
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.Path
import java.nio.file.StandardCopyOption.ATOMIC_MOVE
import java.util.Locale
import java.util.zip.CRC32
import java.util.zip.CheckedInputStream
import java.util.zip.ZipFile

internal object RiivolutionPackStore {
    private const val RESERVE = 256L * 1024 * 1024
    internal data class Limits(
        val archive: Long = 4L * 1024 * 1024 * 1024,
        val total: Long = 8L * 1024 * 1024 * 1024,
        val file: Long = 2L * 1024 * 1024 * 1024,
        val entries: Int = 32_768,
        val reserve: Long = RESERVE
    )

    private fun directory(path: Path): Path {
        Files.createDirectories(path)
        if (!Files.isDirectory(path, NOFOLLOW_LINKS)) throw IOException("Invalid mod storage.")
        return path
    }

    @Synchronized
    internal fun importZip(
        root: Path, input: InputStream, name: String, limits: Limits = Limits(), beforePublish: () -> Unit = {}
    ): Path {
        directory(root)
        require(name.isNotBlank() && name !in setOf(".", "..") && name.none { it in "/\\:" })
        val staging = directory(root.resolveSibling(".riivolution-imports"))
        Files.list(staging).use { paths ->
            paths.filter { it.fileName.toString().startsWith("job-") }.forEach(::removeTree)
        }
        val stage = Files.createTempDirectory(staging, "job-")
        try {
            val archive = stage.resolve("pack.zip")
            Files.newOutputStream(archive).use {
                BoundedInput.copy(input, it, minOf(limits.archive, freeFor(stage, limits)))
            }
            val content = Files.createDirectory(stage.resolve("content"))
            extract(archive, content, limits)
            val children = content.toFile().listFiles().orEmpty()
            val folders = children.filter { it.isDirectory }
            val pack = if (!Files.isDirectory(content.resolve("riivolution")) && folders.size == 1 &&
                File(folders.single(), "riivolution").isDirectory) folders.single().toPath() else content
            beforePublish()
            return Files.move(pack, availableName(root, name), ATOMIC_MOVE)
        } finally {
            removeTree(stage)
        }
    }

    private fun removeTree(path: Path) {
        Files.walk(path).use { paths -> paths.sorted(Comparator.reverseOrder()).forEach(Files::delete) }
    }

    private fun extract(archive: Path, content: Path, limits: Limits) {
        val budget = minOf(limits.total, freeFor(content, limits))
        val explicit = HashSet<String>()
        val spelling = HashMap<String, String>()
        var total = 0L
        var files = 0
        ZipFile(archive.toFile()).use { zip ->
            if (zip.size() > limits.entries) throw IOException("The ZIP contains too many entries.")
            for (entry in zip.entries()) {
                val name = entry.name.removeSuffix("/")
                val parts = name.split('/')
                if (name.length > 1024 || parts.size > 32 || name.any { it == '\\' || it == ':' || it.isISOControl() } ||
                    parts.any { it.isEmpty() || it == "." || it == ".." }) {
                    throw IOException("The ZIP contains an unsafe file path.")
                }
                if (!explicit.add(name.lowercase(Locale.ROOT))) throw IOException("The ZIP repeats a path.")
                var prefix = ""
                for (part in parts) {
                    prefix = if (prefix.isEmpty()) part else "$prefix/$part"
                    val previous = spelling.putIfAbsent(prefix.lowercase(Locale.ROOT), prefix)
                    if (previous != null && previous != prefix) throw IOException("The ZIP has conflicting path names.")
                }
                val target = content.resolve(name)
                if (entry.isDirectory) {
                    if (entry.size != 0L) throw IOException("Invalid folder entry.")
                    Files.createDirectories(target)
                } else {
                    if (entry.size !in 0L..minOf(limits.file, budget - total)) {
                        throw IOException("The ZIP exceeds the mod import size limit or available storage.")
                    }
                    Files.createDirectories(target.parent)
                    val crc = CRC32()
                    val copied = CheckedInputStream(zip.getInputStream(entry), crc).use { input ->
                        Files.newOutputStream(target).use { output ->
                            BoundedInput.copy(input, output, entry.size)
                        }
                    }
                    if (copied != entry.size || crc.value != entry.crc) throw IOException("A ZIP entry is corrupt.")
                    total += copied
                    files++
                }
            }
        }
        if (files == 0) throw IOException("The ZIP contains no mod files.")
    }

    private fun freeFor(path: Path, limits: Limits): Long {
        val available = path.toFile().usableSpace - limits.reserve
        if (available <= 0) throw IOException("Free more internal storage to import this mod.")
        return available
    }

    private fun availableName(parent: Path, name: String): Path {
        val existing = parent.toFile().list().orEmpty().map { it.lowercase(Locale.ROOT) }.toSet()
        var path = name
        var suffix = 2
        while (path.lowercase(Locale.ROOT) in existing) path = "$name ($suffix)".also { suffix++ }
        return parent.resolve(path)
    }

    private fun archiveRoot(root: Path) = root.resolveSibling("RemovedRiivolutionPacks")

    /** A pack can be the live NAND redirect. Move the entire directory without touching its contents. */
    @Synchronized
    fun remove(root: Path, pack: Path) {
        require(pack.parent == root && Files.isDirectory(pack, NOFOLLOW_LINKS))
        val slot = Files.createTempDirectory(directory(archiveRoot(root)), "removed-")
        try {
            Files.move(pack, slot.resolve(pack.fileName), ATOMIC_MOVE)
        } catch (failure: IOException) {
            Files.deleteIfExists(slot)
            throw failure
        }
    }

    fun removed(root: Path): List<File> {
        val archive = archiveRoot(root)
        if (!Files.isDirectory(archive, NOFOLLOW_LINKS)) return emptyList()
        return archive.toFile().listFiles().orEmpty()
            .filter { it.name.startsWith("removed-") && Files.isDirectory(it.toPath(), NOFOLLOW_LINKS) }
            .flatMap { it.listFiles().orEmpty().asList() }
            .filter { Files.isDirectory(it.toPath(), NOFOLLOW_LINKS) }
            .sortedBy { it.name.lowercase(Locale.ROOT) }
    }

    @Synchronized
    fun restore(root: Path, archivedPack: Path) {
        val slot = archivedPack.parent
        val archive = archiveRoot(root)
        require(slot.parent == archive && slot.fileName.toString().startsWith("removed-"))
        require(Files.isDirectory(archive, NOFOLLOW_LINKS))
        require(Files.isDirectory(slot, NOFOLLOW_LINKS) && Files.isDirectory(archivedPack, NOFOLLOW_LINKS))
        Files.move(archivedPack, availableName(directory(root), archivedPack.fileName.toString()), ATOMIC_MOVE)
        runCatching { Files.deleteIfExists(slot) }
    }
}
