package com.driftdroid.android

import android.content.Context
import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.Path
import java.nio.file.StandardCopyOption.ATOMIC_MOVE
import java.util.Locale
import java.util.stream.Collectors

internal object RetroRewindStartupMods {
    const val EXTRA_PREPARED = "rr_startup_mods_prepared"
    private const val PREFS = "retro_rewind_mods"
    private const val ENABLED = "load_at_startup"
    private const val OVERLAY = ".retro-rewind-startup-mods"
    private const val STAGE = "$OVERLAY-stage"
    private const val MANIFEST = "riivolution/wiids-startup.xml"
    private val assetTypes = setOf(
        "szs", "brres", "brstm", "brsar", "bti", "tpl", "thp", "bmg",
        "brlyt", "brlan", "breff", "breft", "kcl", "kmp"
    )
    private val archiveTag = Regex("[A-Za-z0-9_]+")
    private val priorityPrefix = Regex("^\\d+\\.")

    fun enabled(context: Context): Boolean =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean(ENABLED, false)

    fun setEnabled(context: Context, enabled: Boolean) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean(ENABLED, enabled).apply()
    }

    private fun work(context: Context) = File(context.filesDir, "WiiCompiled").toPath()

    fun shouldPrepare(context: Context): Boolean =
        enabled(context) || Files.exists(work(context).resolve(OVERLAY), NOFOLLOW_LINKS) ||
            Files.exists(work(context).resolve(STAGE), NOFOLLOW_LINKS)

    fun preparedRoot(context: Context): File? =
        work(context).resolve(OVERLAY).takeIf {
            Files.isDirectory(it, NOFOLLOW_LINKS) && Files.isRegularFile(it.resolve(MANIFEST), NOFOLLOW_LINKS)
        }?.toFile()

    fun prepare(context: Context, packs: List<File>): Boolean =
        prepare(work(context), packs.map(File::toPath), enabled(context))

    /** Publishes a separate, disposable DVD overlay. The installed distribution and mods are read-only. */
    @Synchronized
    internal fun prepare(work: Path, packs: List<Path>, enabled: Boolean): Boolean {
        val target = work.resolve(OVERLAY)
        val stage = work.resolve(STAGE)
        removeTree(stage)
        if (!enabled) {
            removeTree(target)
            return false
        }
        val myStuff = work.resolve("RetroRewind6/MyStuff")
        val replacements = linkedMapOf<String, Pair<String, Path>>()
        val patches = linkedMapOf<String, Pair<String, Path>>()

        for ((priority, root) in (packs + listOf(myStuff)).distinct().withIndex()) {
            if (!Files.isDirectory(root, NOFOLLOW_LINKS)) continue
            val personal = root == myStuff
            if (!personal && hasRiivolutionXml(root.resolve("riivolution"))) continue

            val files = Files.walk(root).use { entries ->
                entries.filter { Files.isRegularFile(it, NOFOLLOW_LINKS) }
                    .filter { path -> root.relativize(path).none { it.toString().startsWith(".") || it.toString() == "__MACOSX" } }
                    .sorted().collect(Collectors.toList())
            }
            val names = files.map { key(it.fileName.toString()) }.toSet()
            for (file in files) {
                val name = file.fileName.toString()
                if (name.any { it.code !in 32..126 || it in "/\\:" } || Files.size(file) !in 1L..0xffffffffL) continue
                if (isTagged(name)) {
                    val tagged = "${priority.coerceAtMost(999)}.${name.replaceFirst(priorityPrefix, "")}"
                    if (tagged.length < 64) patches.putIfAbsent(key(tagged), tagged to file)
                } else if (key(name.substringAfterLast('.', "")) in assetTypes) {
                    if (personal) replacements.putIfAbsent(key(name), name to file)
                    if (name.endsWith("_U.szs", ignoreCase = true)) {
                        val alias = name.dropLast(6) + "_E.szs"
                        if (key(alias) !in names) replacements.putIfAbsent(key(alias), alias to file)
                    }
                }
            }
        }
        if (patches.isEmpty() && replacements.isEmpty()) {
            removeTree(target)
            return false
        }

        Files.createDirectories(stage)
        try {
            linkAssets(stage.resolve("replacements"), replacements.values)
            linkAssets(stage.resolve("patches"), patches.values)
            val manifest = stage.resolve(MANIFEST)
            Files.createDirectories(manifest.parent)
            manifest.toFile().writeText(
                """
                <wiidisc version="1">
                  <id game="RMC"/>
                  <options><section name="WiiDS">
                    <option name="Startup mods" default="1">
                      <choice name="Enabled"><patch id="wiids-startup"/></choice>
                    </option>
                  </section></options>
                  <patch id="wiids-startup">
                    <folder external="/replacements" recursive="true"/>
                    <folder external="/patches" disc="/patches" recursive="true" create="true"/>
                  </patch>
                </wiidisc>
                """.trimIndent()
            )
            removeTree(target)
            Files.move(stage, target, ATOMIC_MOVE)
            return true
        } finally {
            removeTree(stage)
        }
    }

    private fun key(value: String) = value.lowercase(Locale.ROOT)

    private fun hasRiivolutionXml(directory: Path): Boolean =
        Files.isDirectory(directory, NOFOLLOW_LINKS) && Files.list(directory).use { files ->
            files.anyMatch { Files.isRegularFile(it, NOFOLLOW_LINKS) && key(it.fileName.toString()).endsWith(".xml") }
        }

    private fun isTagged(name: String): Boolean {
        val parts = name.replaceFirst(priorityPrefix, "").split('.')
        if (parts.size < 3) return false
        val archive = parts.last().equals("szs", ignoreCase = true)
        val penultimate = parts[parts.lastIndex - 1]
        return archiveTag.matches(if (archive) penultimate else parts.last()) &&
            (archive || key(penultimate) in assetTypes)
    }

    private fun linkAssets(folder: Path, files: Collection<Pair<String, Path>>) {
        if (files.isEmpty()) return
        Files.createDirectory(folder)
        for ((name, source) in files) {
            try {
                Files.createLink(folder.resolve(name), source)
            } catch (error: IOException) {
                throw IOException("Could not prepare $name for Retro Rewind.", error)
            }
        }
    }

    private fun removeTree(root: Path) {
        if (!Files.exists(root, NOFOLLOW_LINKS)) return
        Files.walk(root).use { entries -> entries.sorted(Comparator.reverseOrder()).forEach(Files::delete) }
    }
}
