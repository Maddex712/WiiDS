package com.driftdroid.android

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import org.json.JSONObject
import java.io.File
import java.nio.file.Files
import java.security.MessageDigest

/**
 * Where the launcher's cases and music come from. A player's own set (personal use only, never in
 * the APK) in <app external files>/custom/launcher (artwork.json + images) and
 * custom/launcher-audio (audio.json + WAVs) replaces what WiiDS builds from the discs into
 * WiiCompiled/ThorMap; removing a folder goes back to the built one.
 */
internal object ThorLauncherFolders {
    fun cases(context: Context, thorMap: File): File = custom(context, "launcher", "artwork.json")
        ?: bundled(context, "launcher", "artwork.json") ?: File(thorMap, "launcher")
    fun audio(context: Context, thorMap: File): File = custom(context, "launcher-audio", "audio.json")
        ?: bundled(context, "launcher-audio", "audio.json") ?: File(thorMap, "launcher-audio")
    private fun bundled(context: Context, name: String, manifest: String): File? =
        ThorBundledPresentation.root(context)?.let { File(it, name) }?.takeIf { File(it, manifest).isFile }
    private fun custom(context: Context, name: String, manifest: String): File? =
        context.getExternalFilesDir(null)?.let { File(it, "custom/$name") }?.takeIf { File(it, manifest).isFile }
}

/**
 * Game cases, composed on the device from each imported disc (wiids/art art_cases.cpp) or
 * supplied by the owner; shared by both displays without duplicate decoding. A player may own
 * any subset of the games, so every pair is optional; a manifest carries both of a game's
 * images or neither.
 */
internal object ThorLauncherArtwork {
    private val required = emptySet<String>()
    private val optional = listOf(setOf("original-cover", "original-disc"), setOf("retro-rewind-cover", "retro-rewind-disc"),
        setOf("nsmbw-cover", "nsmbw-disc"), setOf("sunshine-cover", "sunshine-disc"), setOf("msmbwii-cover", "msmbwii-disc"),
        setOf("eclipse-cover", "eclipse-disc"))
    private val spines = optional.associate { pair -> "${pair.first().removeSuffix("-cover")}-spine" to pair }
    private val stems = required + optional.flatten() + spines.keys
    private data class Key(val path: String, val modified: Long, val length: Long)
    private data class Cached(val bitmap: Bitmap, val sha256: String, val width: Int, val height: Int, val mime: String)
    private val cache = LinkedHashMap<Key, Cached>()
    private val shaPattern = Regex("[0-9a-fA-F]{64}")
    private const val MAX_MANIFEST = 16 * 1024
    private const val MAX_IMAGE_BYTES = 24L * 1024 * 1024

    /** A scan of a printed spine is naturally narrower than a cover or disc label. */
    private fun validSize(stem: String, width: Int, height: Int): Boolean =
        width in (if (stem in spines) 16 else 128)..4096 && height in 128..4096

    /** A present manifest is authoritative: bad or missing entries never choose stale alternate files. */
    @Synchronized
    fun bitmap(folder: File, stem: String): Bitmap? = try {
        load(folder, stem)
    } catch (_: Exception) {
        null // Optional art must never prevent the native-art launcher from opening.
    }

    private fun load(folder: File, stem: String): Bitmap? {
        if (stem !in stems || !folder.isDirectory || Files.isSymbolicLink(folder.toPath())) return null
        val parent = folder.canonicalFile
        val manifest = File(folder, "artwork.json")
        var expectedHash: String? = null
        var expectedWidth = 0
        var expectedHeight = 0
        val filename = if (manifest.exists() || Files.isSymbolicLink(manifest.toPath())) {
            if (!manifest.isFile || Files.isSymbolicLink(manifest.toPath()) || manifest.canonicalFile.parentFile != parent ||
                manifest.length() !in 1L..MAX_MANIFEST.toLong()) return null
            val metadata = manifest.inputStream().use { input ->
                val bytes = ByteArray(MAX_MANIFEST + 1)
                var count = 0
                while (count < bytes.size) {
                    val read = input.read(bytes, count, bytes.size - count)
                    if (read < 0) break
                    if (read == 0) continue
                    count += read
                }
                if (count > MAX_MANIFEST) return null
                JSONObject(String(bytes, 0, count, Charsets.UTF_8))
            }
            if (metadata.getInt("schema") != 1 || metadata.getString("kind") != "private-launcher-artwork") return null
            val images = metadata.getJSONObject("images")
            val keys = images.keys().asSequence().toSet()
            if (!keys.containsAll(required) || !stems.containsAll(keys) ||
                optional.any { set -> set.any { it in keys } && !keys.containsAll(set) } ||
                spines.any { (name, pair) -> name in keys && !keys.containsAll(pair) }) return null
            if (stem !in keys) return null
            // Validate the complete selection table before trusting any one entry.
            for (name in keys) {
                val declaredEntry = images.getJSONObject(name)
                val declared = declaredEntry.getString("file")
                if (declared != "$name.png" && declared != "$name.jpg") return null
                if (!declaredEntry.getString("sha256").matches(shaPattern)) return null
                val declaredSize = declaredEntry.getJSONArray("size")
                if (declaredSize.length() != 2 || !validSize(name, declaredSize.getInt(0), declaredSize.getInt(1))) return null
            }
            val entry = images.getJSONObject(stem)
            expectedHash = entry.getString("sha256").lowercase()
            if (!expectedHash.matches(shaPattern)) return null
            val size = entry.getJSONArray("size")
            if (size.length() != 2) return null
            expectedWidth = size.getInt(0)
            expectedHeight = size.getInt(1)
            if (!validSize(stem, expectedWidth, expectedHeight)) return null
            entry.getString("file")
        } else {
            // Legacy folders have no table, but a spine must still belong to a valid case pair.
            if (spines[stem]?.any { load(folder, it) == null } == true) return null
            sequenceOf("$stem.png", "$stem.jpg").firstOrNull { File(folder, it).isFile } ?: return null
        }
        val expectedMime = if (filename.endsWith(".png")) "image/png" else "image/jpeg"
        val source = File(folder, filename)
        if (!source.isFile || Files.isSymbolicLink(source.toPath())) return null
        val canonical = source.canonicalFile
        if (canonical.parentFile != parent || canonical.name != filename || source.length() !in 1L..MAX_IMAGE_BYTES) return null
        val key = Key(canonical.path, source.lastModified(), source.length())
        cache[key]?.let { cached ->
            if (cached.bitmap.isRecycled || !validSize(stem, cached.width, cached.height) || cached.mime != expectedMime) return null
            if (expectedHash != null && (expectedHash != cached.sha256 || expectedWidth != cached.width || expectedHeight != cached.height ||
                cached.mime != expectedMime)) return null
            return cached.bitmap
        }
        val dimensions = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeFile(canonical.path, dimensions)
        if (!validSize(stem, dimensions.outWidth, dimensions.outHeight) || dimensions.outMimeType != expectedMime) return null
        if (expectedHash != null && (dimensions.outWidth != expectedWidth || dimensions.outHeight != expectedHeight ||
            dimensions.outMimeType != expectedMime)) return null
        val digest = MessageDigest.getInstance("SHA-256")
        canonical.inputStream().use { input ->
            val buffer = ByteArray(8192)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                if (count > 0) digest.update(buffer, 0, count)
            }
        }
        val hash = digest.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
        if (expectedHash != null && hash != expectedHash) return null
        var sample = 1
        val largest = maxOf(dimensions.outWidth, dimensions.outHeight)
        while ((largest + sample - 1) / sample > 1024) sample *= 2
        val decoded = BitmapFactory.decodeFile(canonical.path, BitmapFactory.Options().apply {
            inSampleSize = sample
            inScaled = false
            inPreferredConfig = Bitmap.Config.ARGB_8888
        }) ?: return null
        if (maxOf(decoded.width, decoded.height) > 1024 || source.lastModified() != key.modified || source.length() != key.length) return null
        // Replacing a file drops only our old reference. Views may still draw it;
        // recycling an evicted bitmap here would invalidate those live displays.
        cache.keys.filter { it.path == key.path }.forEach { cache.remove(it) }
        if (cache.size >= stems.size) cache.remove(cache.keys.first())
        cache[key] = Cached(decoded, hash, dimensions.outWidth, dimensions.outHeight, dimensions.outMimeType)
        return decoded
    }
}
