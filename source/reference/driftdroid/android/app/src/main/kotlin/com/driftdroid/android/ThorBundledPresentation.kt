package com.driftdroid.android

import android.content.Context
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.security.MessageDigest

/** Installs only the curated, hash-verified presentation assets shipped with this APK.
 * No target can address a disc, save, preference or game configuration directory.
 */
internal object ThorBundledPresentation {
    private const val MANIFEST = "presentation/manifest.json"
    private const val MAX_MANIFEST = 1024 * 1024
    private const val MAX_FILE = 64L * 1024 * 1024
    private const val MAX_TOTAL = 768L * 1024 * 1024
    private val groups = setOf("launcher", "launcher-audio", "thor-map", "nsmbw-hud", "msmbwii-hud", "sms-hud", "eclipse-hud")
    private val extensions = setOf("png", "jpg", "jpeg", "wav", "json", "tsv", "rgba")
    // Release labels use dotted versions (for example thor-2026-10-22-v1.0).
    // This remains one bounded path component; safe() rejects traversal segments.
    private val revisionPattern = Regex("[A-Za-z0-9][A-Za-z0-9_.-]{0,63}")
    private val hashPattern = Regex("[0-9a-f]{64}")
    private data class Entry(val asset: String, val group: String, val relative: String, val sha: String, val bytes: Long)

    fun available(context: Context): Boolean = runCatching { context.assets.open(MANIFEST).use { true } }.getOrDefault(false)

    /** The completed revision used for cases/music, always behind an explicit custom override. */
    fun root(context: Context): File? = runCatching {
        val base = File(context.filesDir.canonicalFile, "bundled-presentation")
        val active = safe(base, "active.json")
        if (!active.isFile || active.length() > 1024) return null
        val value = JSONObject(active.readText())
        val name = value.getString("folder")
        val hash = value.getString("manifest_sha256")
        if (!name.matches(Regex("[A-Za-z0-9][A-Za-z0-9_.-]{0,80}")) || !hash.matches(hashPattern)) return null
        val revision = safe(base, name)
        val complete = safe(revision, ".complete")
        if (!complete.isFile || complete.length() != 64L || complete.readText() != hash) return null
        safe(revision, "ThorMap").takeIf { it.isDirectory }
    }.getOrNull()

    /** Blocking and bounded. Called on a worker before constructing the library UI. */
    @Synchronized
    fun install(context: Context, progress: (Int, Int) -> Unit = { _, _ -> }) {
        val bytes = context.assets.open(MANIFEST).use { input ->
            val output = java.io.ByteArrayOutputStream()
            val buffer = ByteArray(8192)
            while (true) {
                val n = input.read(buffer)
                if (n < 0) break
                if (output.size() + n > MAX_MANIFEST) throw IOException("The presentation manifest is too large.")
                output.write(buffer, 0, n)
            }
            output.toByteArray()
        }
        val manifest = JSONObject(String(bytes, Charsets.UTF_8))
        if (manifest.getInt("schema") != 1 || manifest.getString("kind") != "wiids-bundled-presentation")
            throw IOException("Unsupported bundled presentation manifest.")
        val revision = manifest.getString("revision")
        if (!revision.matches(revisionPattern)) throw IOException("Invalid presentation revision.")
        val files = manifest.getJSONArray("files")
        if (files.length() !in 1..8192) throw IOException("Invalid presentation file count.")
        var total = 0L
        val targets = HashSet<String>()
        val assets = HashSet<String>()
        val entries = (0 until files.length()).map { index ->
            val record = files.getJSONObject(index)
            val asset = record.getString("asset")
            val target = record.getString("target")
            val sha = record.getString("sha256")
            val length = record.getLong("bytes")
            requirePath(target)
            if (asset != "presentation/files/$target") throw IOException("Presentation asset and target do not match.")
            val group = target.substringBefore('/')
            val relative = target.substringAfter('/', "")
            if (group !in groups || relative.isEmpty() || !sha.matches(hashPattern) || length !in 1..MAX_FILE ||
                relative.substringAfterLast('.', "").lowercase() !in extensions || !targets.add(target) || !assets.add(asset))
                throw IOException("Invalid bundled presentation entry.")
            total += length
            if (total > MAX_TOTAL) throw IOException("Bundled presentation exceeds the size limit.")
            Entry(asset, group, relative, sha, length)
        }
        val hash = hex(MessageDigest.getInstance("SHA-256").digest(bytes))
        val base = File(context.filesDir.canonicalFile, "bundled-presentation")
        base.mkdirs()
        val name = "$revision-${hash.take(12)}"
        val folder = safe(base, name)
        val complete = safe(folder, ".complete")
        val ready = complete.isFile && complete.length() == 64L && complete.readText() == hash &&
            entries.filter { it.group == "launcher" || it.group == "launcher-audio" }.all {
                safe(folder, "ThorMap/${it.group}/${it.relative}").let { file -> file.isFile && file.length() == it.bytes }
            }
        val staged = safe(base, "$name-installing")
        if (!ready) {
            if (staged.exists()) staged.deleteRecursively()
            staged.mkdirs()
        }
        var done = 0
        try {
            for (entry in entries) {
                if (entry.group == "launcher" || entry.group == "launcher-audio") {
                    if (!ready) copy(context, entry, safe(staged, "ThorMap/${entry.group}/${entry.relative}"), missingOnly = false)
                } else {
                    val destination = when (entry.group) {
                        "thor-map" -> safe(File(context.filesDir.canonicalFile, "WiiCompiled/ThorMap"), entry.relative)
                        else -> {
                            val external = context.getExternalFilesDir(null) ?: throw IOException("Presentation storage is unavailable.")
                            safe(File(external.canonicalFile, "${entry.group.removeSuffix("-hud")}/ThorHud"), entry.relative)
                        }
                    }
                    copy(context, entry, destination, missingOnly = true)
                }
                progress(++done, entries.size)
            }
            if (!ready) {
                write(safe(staged, ".complete"), hash.toByteArray())
                // This hash-named folder contains only packaged art, never custom art or profiles.
                if (folder.exists() && !folder.deleteRecursively()) throw IOException("An incomplete presentation revision couldn't be repaired.")
                if (!staged.renameTo(folder)) throw IOException("The completed presentation couldn't be installed.")
            }
            write(safe(base, "active.json"), JSONObject().put("folder", name).put("manifest_sha256", hash).toString().toByteArray())
        } finally {
            if (!ready && staged.exists()) staged.deleteRecursively()
        }
    }

    private fun requirePath(relative: String) {
        if (relative.length !in 1..240 || relative.contains('\\') || relative.startsWith('/') ||
            relative.split('/').let { parts -> parts.size > 12 || parts.any { it.isEmpty() || it == "." || it == ".." || !it.matches(Regex("[A-Za-z0-9_.-]+")) } })
            throw IOException("Unsafe presentation path.")
    }

    /** Rejects every symlink in the target chain, including the root itself. */
    private fun safe(root: File, relative: String): File {
        requirePath(relative)
        val absoluteRoot = root.absoluteFile.toPath().normalize().toFile()
        var ancestor: File? = absoluteRoot
        while (ancestor != null) {
            if (Files.isSymbolicLink(ancestor.toPath())) throw IOException("Presentation storage contains a symbolic link.")
            ancestor = ancestor.parentFile
        }
        var target = absoluteRoot
        for (segment in relative.split('/')) {
            target = File(target, segment)
            if (Files.isSymbolicLink(target.toPath())) throw IOException("Presentation storage contains a symbolic link.")
        }
        if (!target.canonicalFile.path.startsWith(absoluteRoot.canonicalFile.path + File.separator)) throw IOException("Presentation target escaped its folder.")
        return target
    }

    private fun copy(context: Context, entry: Entry, target: File, missingOnly: Boolean) {
        if (missingOnly && target.isFile && target.length() > 0) return
        if (missingOnly && target.exists() && !target.isFile) throw IOException("A presentation filename is occupied by a directory.")
        target.parentFile?.mkdirs()
        val temp = File(target.parentFile, target.name + ".wiids-bundled-part")
        if (Files.isSymbolicLink(temp.toPath())) throw IOException("Unsafe presentation temporary file.")
        try {
            val digest = MessageDigest.getInstance("SHA-256")
            var written = 0L
            context.assets.open(entry.asset).use { input ->
                FileOutputStream(temp).use { output ->
                    val buffer = ByteArray(256 * 1024)
                    while (true) {
                        val n = input.read(buffer)
                        if (n < 0) break
                        written += n
                        if (written > entry.bytes) throw IOException("Bundled presentation length mismatch.")
                        digest.update(buffer, 0, n); output.write(buffer, 0, n)
                    }
                    output.fd.sync()
                }
            }
            if (written != entry.bytes || hex(digest.digest()) != entry.sha) throw IOException("Bundled presentation checksum mismatch.")
            if (missingOnly) {
                // Recheck after the potentially lengthy copy: an importer/customizer may
                // have created a valid file while this worker was reading the APK asset.
                if (target.isFile && target.length() > 0) return
                if (target.exists() && !target.isFile) throw IOException("A presentation filename is occupied by a directory.")
                if (target.exists()) Files.delete(target.toPath()) // only an empty placeholder
                try {
                    // Same-directory rename, deliberately without REPLACE_EXISTING:
                    // a file created by another writer wins instead of being replaced.
                    Files.move(temp.toPath(), target.toPath())
                } catch (_: java.nio.file.FileAlreadyExistsException) { return }
            } else {
                Files.move(temp.toPath(), target.toPath(), StandardCopyOption.ATOMIC_MOVE)
            }
        } finally { temp.delete() }
    }

    private fun write(target: File, bytes: ByteArray) {
        target.parentFile?.mkdirs()
        val temp = File(target.parentFile, target.name + ".wiids-bundled-part")
        if (Files.isSymbolicLink(temp.toPath())) throw IOException("Unsafe presentation temporary file.")
        try {
            FileOutputStream(temp).use { it.write(bytes); it.fd.sync() }
            Files.move(temp.toPath(), target.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
        } finally { temp.delete() }
    }

    private fun hex(bytes: ByteArray) = bytes.joinToString("") { "%02x".format(it.toInt() and 255) }
}
