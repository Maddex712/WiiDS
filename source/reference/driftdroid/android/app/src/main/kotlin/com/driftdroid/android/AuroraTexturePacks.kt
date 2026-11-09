package com.driftdroid.android

import android.content.Context
import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.FileVisitResult
import java.nio.file.Path
import java.nio.file.SimpleFileVisitor
import java.nio.file.attribute.BasicFileAttributes
import java.nio.file.StandardCopyOption

/** Presents the selected packs to the existing Aurora loader before a new native session. */
object AuroraTexturePacks {
    private const val MANAGED = "WiiDS-managed"
    private const val OWNER = "WiiDS managed Aurora textures v1"
    private const val KEY = "texture_replacements"
    private val videoKey = Regex("""^\s*(?:texture_replacements|"texture_replacements"|'texture_replacements')\s*=.*$""")
    private val videoTable = Regex("""^\s*\[\s*(?:video|"video"|'video')\s*]\s*(?:#.*)?$""")

    internal fun isolatedData(context: Context, game: FrontendGame): File? {
        val app = context.applicationContext
        val external = app.getExternalFilesDir(null)
        return when (game) {
            FrontendGame.NSMB, FrontendGame.MSMBWII -> File(external ?: File(app.filesDir, "data"), game.id)
            FrontendGame.BRAWL, FrontendGame.PROJECT_PLUS -> {
                external?.let { File(it, game.id) } ?: File(app.filesDir, "${game.id}/data")
            }
            else -> null
        }
    }

    @JvmStatic
    fun prepared(context: Context, game: FrontendGame, data: File): Boolean {
        val folders = TexturePacks.activeTextureFolders(context.applicationContext, game).orEmpty()
        return folders.isNotEmpty() &&
            signature(File(data, "texture_replacements/$MANAGED")) == selection(folders)
    }

    private fun selection(folders: List<File>) = folders.joinToString("\n") { it.parentFile!!.name }
    private fun signature(folder: File) = runCatching {
        if (owned(folder)) File(folder, ".selection").readText() else null
    }.getOrNull()

    @Synchronized
    internal fun prepare(data: File, folders: List<File>): Boolean {
        val replacements = File(data, "texture_replacements")
        val target = File(replacements, MANAGED)
        val staging = File(data, ".$MANAGED-next")
        val retired = File(data, ".$MANAGED-old")
        if (folders.isEmpty() && !owned(target) && !owned(retired) && !owned(staging)) return false
        requireOwned(target)
        requireOwned(retired)
        deleteOwned(staging)
        if (present(retired)) {
            if (present(target)) deleteOwned(retired) else move(retired, target)
        }

        if (folders.isEmpty()) {
            if (present(target)) {
                move(target, retired)
                deleteOwned(retired)
            }
            return false
        }

        val signature = selection(folders)
        if (signature(target) == signature) return true
        if ((!replacements.isDirectory && !replacements.mkdirs()) || !staging.mkdir())
            throw IOException("Could not prepare the game's texture directory.")

        File(staging, ".owner").writeText(OWNER)
        try {
            val copied = HashSet<String>()
            for (folder in folders) {
                for (file in folder.listFiles().orEmpty().sortedBy { it.name }) {
                    // Aurora keys the index by the texture identity, without its image extension.
                    if (!Files.isRegularFile(file.toPath(), LinkOption.NOFOLLOW_LINKS) ||
                        !copied.add(file.nameWithoutExtension)) continue
                    val output = File(staging, file.name)
                    val linked = try {
                        Files.createLink(output.toPath(), file.toPath())
                        true
                    } catch (_: IOException) {
                        false
                    } catch (_: UnsupportedOperationException) {
                        false
                    }
                    if (!linked) file.copyTo(output)
                }
            }
            File(staging, ".selection").writeText(signature)
            if (present(target)) move(target, retired)
            try {
                move(staging, target)
            } catch (failure: IOException) {
                if (present(retired)) move(retired, target)
                throw failure
            }
            deleteOwned(retired)
            return true
        } finally {
            deleteOwned(staging)
        }
    }

    /** Mario Kart Wii and Retro Rewind share a runtime configuration, but select packs separately. */
    internal fun prepareKart(context: Context, game: FrontendGame, data: File) {
        val folders = TexturePacks.activeTextureFolders(context.applicationContext, game).orEmpty()
        val saved = File(data, ".$MANAGED-video")
        val config = File(data, "Config.toml")
        if (folders.isNotEmpty() && !config.isFile)
            throw IOException("Start the game once before enabling a texture pack.")
        val active = prepare(data, folders)
        if (!active && !saved.isFile) return
        val lines = if (config.isFile) config.readLines().toMutableList() else mutableListOf()
        var table = lines.indexOfFirst(videoTable::matches)
        val key = if (table < 0) -1 else (table + 1 until lines.size)
            .takeWhile { !lines[it].trimStart().startsWith("[") }
            .firstOrNull { videoKey.matches(lines[it]) } ?: -1
        if (active && !saved.isFile) {
            AtomicConfigFile.writeText(saved, if (key < 0) "" else lines[key])
        }
        val entry = if (active) "$KEY = true" else saved.readText()
        if (key >= 0) {
            if (entry.isEmpty()) lines.removeAt(key) else lines[key] = entry
        } else if (entry.isNotEmpty()) {
            if (table < 0) {
                lines.add("[video]")
                table = lines.lastIndex
            }
            lines.add(table + 1, entry)
        }
        val text = lines.joinToString("\n", postfix = "\n")
        if (!config.isFile || config.readText() != text) AtomicConfigFile.writeText(config, text)
        if (!active) Files.deleteIfExists(saved.toPath())
    }

    private fun present(file: File) = Files.exists(file.toPath(), LinkOption.NOFOLLOW_LINKS)

    private fun move(source: File, target: File) {
        Files.move(source.toPath(), target.toPath(), StandardCopyOption.ATOMIC_MOVE)
    }

    private fun owned(file: File): Boolean {
        val marker = File(file, ".owner")
        return Files.isDirectory(file.toPath(), LinkOption.NOFOLLOW_LINKS) &&
            Files.isRegularFile(marker.toPath(), LinkOption.NOFOLLOW_LINKS) && marker.readText() == OWNER
    }

    private fun requireOwned(file: File) {
        if (present(file) && !owned(file))
            throw IOException("A user folder occupies the WiiDS managed texture location.")
    }

    private fun deleteOwned(file: File) {
        if (!present(file)) return
        requireOwned(file)
        Files.walkFileTree(file.toPath(), object : SimpleFileVisitor<Path>() {
            override fun visitFile(path: Path, attributes: BasicFileAttributes): FileVisitResult {
                Files.delete(path)
                return FileVisitResult.CONTINUE
            }

            override fun postVisitDirectory(path: Path, error: IOException?): FileVisitResult {
                if (error != null) throw error
                Files.delete(path)
                return FileVisitResult.CONTINUE
            }
        })
    }
}
