package com.driftdroid.android

import android.content.Context
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.net.URL
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.security.MessageDigest
import java.util.UUID
import java.util.zip.ZipFile
import javax.net.ssl.HttpsURLConnection

/** Managed packs live beside the app's data, outside disc installs and saves. */
object TexturePacks {
    internal data class Pack(val id: String, val title: String)

    internal data class Release(
        val title: String,
        val version: String,
        val author: String,
        val description: String,
        val url: String,
        val bytes: Long,
        val sha: String,
        val overlay: Boolean = false,
    ) {
        val id get() = catalogId(sha)
        val downloadSize get() = if (bytes < MIB) "${(bytes + 999) / 1000} KB" else "${(bytes + 999_999) / 1_000_000} MB"
    }

    private const val UHD_RELEASE =
        "https://github.com/qashto/Super_Mario_Sunshine_UHD_Texture_Pack/releases/download/2.1.1/"
    private const val SUNSHINE_HD_SHA = "3df2d643dd52e28ebca34e09471888520a002a643f0589720f1a2073f6549d88"
    private val coronaOverrides = setOf(
        "tex1_128x128_4c7b3deaf05796fc_14.dds",
        "tex1_256x256_784bfd7db0e87b89_14.dds",
        "tex1_256x256_9116036acd8bc4da_14.dds",
        "tex1_256x256_af81713f0ba2785f_14.dds",
        "tex1_256x256_c00c96de6b608bbd_4.dds",
        "tex1_256x256_ee3f271c0576a775_14.dds",
    )
    private const val NO_HUD_SHA = "cc2247b22e63e7b83e456b5a11a67ff13aafec68ba23d3443272f15f9a3990f9"
    private const val MKW_RELOADED_SHA = "034453c06cda8a842564b94d5c6a888af0533105d93817f5c7f4898fcf8e10fa"
    private const val NSMB_HUD_SHA = "aa161d8ab0318c0cf1595e70c8ab215cbb418f428987dbcb3dd1afc8dffe603c"
    private const val BRAWL_HD_SHA = "8d0addf9d54a6b88b7ce7bb17b382ebd3451d497b764bc4d531f97a4d0e0df61"
    private val kartReloaded = Release("MKWii Reloaded", "0.2.0", "GhostlyDark",
        "HD Mario Kart Wii textures. Retro Rewind uses the textures it shares with Mario Kart Wii.",
        "https://github.com/GhostlyDark/MKWii-Reloaded/releases/download/v0.2.0/mkwii-reloaded-v0.2.0-dds.7z",
        409_942_972, MKW_RELOADED_SHA)
    private val nsmbHud = Release("HD HUD Tweaks", "2023", "nymo642",
        "Sharper shared menus, prompts and HUD textures.",
        "https://files.gamebanana.com/mods/hd-hud-tweaks.rar", 375_797, NSMB_HUD_SHA)
    private val brawlHd = Release("SSBHD Retexture", "4.0.3", "Yami 2b",
        "Sharper portraits, menus and other textures for the original Brawl.",
        "https://files.emulationking.com/wii/texturepacks/supersmashbrosbrawl/yami2b/SSBHD-Retexture-4.0.3-Brawl-PNG.7z",
        145_932_883, BRAWL_HD_SHA)
    private const val GALAXY_HD_SHA = "591816b3a8b084832d1b8fa1d8cef6f5d9f28ece08309d4bb0de78f53c454e65"
    private val galaxyHd = Release("Super Mario Galaxy HD", "2018", "CDAMJC, Razius and Bighead",
        "1080p textures for the original game. Kaizo uses the textures it shares with Galaxy.",
        "https://files.emulationking.com/wii/texturepacks/supermariogalaxy/cdamjcandrazius/SMG1-HD-Textures-1-01-2018-1080p-DDS.7z",
        174_569_133, GALAXY_HD_SHA)
    private val doubleDashUi = Release("Double Dash Re-Rendered UI", "UI Only", "Giodude",
        "Redrawn menus, character portraits and race HUD. Plus uses matching original textures.",
        "https://files.emulationking.com/gamecube/texturepacks/mariokartdoubledash/giodude/Mario-Kart-Double-Dash-ReRendered-giodude-UI-Only-PNG.zip",
        59_149_431, "c05e396e82536cd06e0e72530dddf50d2ea4d9c8c6d16c4cb53019c570732ab5")
    private val releases = listOf(
        Release("Sunshine HD", "2.1.1", "qashto and razius", "Sharper environments, characters and menus.",
            UHD_RELEASE + "GMS_1080.zip", 155_892_294, SUNSHINE_HD_SHA),
        Release("HD Texture Extras", "1.1.1", "chasem-dev", "Fills in textures the Sunshine HD pack leaves out.",
            "https://github.com/chasem-dev/sms-hd-texture-extras/releases/download/v1.1.1/sms-hd-texture-extras-1.1.1.zip",
            64_196_311, "2f633b0abce39e8c0e536e46b29853953b6571d945832acf158adcd536980242"),
        Release("PlayStation Button Prompts", "2.1.1", "qashto and razius", "Replaces supported prompts with PS4 icons.",
            UHD_RELEASE + "SMS_PS4_Button_Textures.zip", 424_991,
            "2e1ce05c9ebe12502ac105ce15653713e676c7ccffddfe9093b7f2bccbf5a171", overlay = true),
        Release("No HUD", "Pitius edition", "Sheldon Sharpe and Pitius", "Hides HUD textures for clean screenshots.",
            "https://codeload.github.com/droidpeti/Pitius-Super-Mario-Sunshine-Config/zip/7db83e97c186b1785203a477bf94dab46a90ff0a",
            53_582, NO_HUD_SHA, overlay = true),
    )

    internal fun catalog(game: FrontendGame): List<Release> = when (game) {
        FrontendGame.SUNSHINE -> releases
        FrontendGame.ECLIPSE -> releases.filterNot { it.overlay }
        FrontendGame.MARIO_KART, FrontendGame.RETRO_REWIND -> listOf(kartReloaded)
        FrontendGame.NSMB, FrontendGame.MSMBWII -> listOf(nsmbHud)
        FrontendGame.BRAWL -> listOf(brawlHd)
        FrontendGame.PROJECT_PLUS -> emptyList()
        FrontendGame.GALAXY, FrontendGame.KAIZO -> listOf(galaxyHd)
        FrontendGame.MKDD, FrontendGame.MKDD_PLUS -> listOf(doubleDashUi)

    }

    private val packId = Regex("[a-f0-9-]{36}")
    private val textureName = Regex(
        "tex1_[0-9]{1,5}x[0-9]{1,5}(?:_m)?_[0-9a-f]{16}(?:_[0-9a-f]{16})?_[0-9]{1,3}(?:_mip(?:[1-9]|1[0-5]))?(?:_arb)?\\.(?:png|dds)"
    )
    private const val MIB = 1024L * 1024L
    private const val MAX_ZIP = 12 * 1024 * MIB
    private const val MAX_UNPACKED = 16 * 1024 * MIB
    private const val STORAGE_RESERVE = 256 * MIB
    private const val MAX_TEXTURE = 128 * MIB

    internal fun usesAurora(game: FrontendGame) = when (game) {
        FrontendGame.MARIO_KART, FrontendGame.RETRO_REWIND, FrontendGame.NSMB,
        FrontendGame.MSMBWII, FrontendGame.BRAWL, FrontendGame.PROJECT_PLUS -> true
        else -> false
    }

    private fun usesNativeMatcher(game: FrontendGame) = when (game) {
        FrontendGame.GALAXY, FrontendGame.KAIZO, FrontendGame.MKDD, FrontendGame.MKDD_PLUS -> true
        else -> false
    }

    internal fun supported(game: FrontendGame) =
        game == FrontendGame.SUNSHINE || game == FrontendGame.ECLIPSE || usesAurora(game) || usesNativeMatcher(game)

    internal fun activeTextureFolders(context: Context, game: FrontendGame): List<File>? =
        enabled(context, game)?.mapNotNull { id ->
            File(root(context, game), "$id/textures").takeIf { it.isDirectory }
        }

    private fun root(context: Context, game: FrontendGame): File {
        require(supported(game))
        return File(context.filesDir, "texture-packs/${game.id}")
    }

    internal fun installed(context: Context, game: FrontendGame): List<Pack> =
        root(context, game).listFiles().orEmpty().mapNotNull { dir ->
            if (!packId.matches(dir.name) || !File(dir, "textures").isDirectory) return@mapNotNull null
            val title = runCatching { File(dir, "title").readText() }.getOrNull() ?: return@mapNotNull null
            Pack(dir.name, title)
        }.sortedWith(compareBy({ it.title.lowercase() }, { it.id }))

    internal fun enabled(context: Context, game: FrontendGame): List<String>? =
        runCatching { File(root(context, game), "active").readLines().filter(packId::matches) }.getOrNull()

    /** A missing choice leaves the runtime's existing settings and manual folders in charge. */
    @JvmStatic
    fun activePath(context: Context, game: FrontendGame): String? {
        val folders = activeTextureFolders(context, game) ?: return null
        return folders.joinToString(";") { it.absolutePath }.ifEmpty { "0" }
    }

    @Synchronized
    internal fun setEnabled(context: Context, game: FrontendGame, id: String, enabled: Boolean) {
        require(installed(context, game).any { it.id == id })
        val remaining = this.enabled(context, game).orEmpty().filterNot { it == id }
        val next = remaining.toMutableList()
        if (enabled) {
            val isBasePack = releases.any { it.id == id && !it.overlay }
            // Keep creator HUD and button overlays above catalog HD packs.
            // A manual import still takes priority when it is enabled.
            val index = if (isBasePack) {
                val overlays = releases.filter { it.overlay }.map { it.id }.toSet()
                next.indexOfLast { it in overlays } + 1
            } else 0
            next.add(index, id)
        }
        val root = root(context, game)
        writeSelection(context, game, root, next)
    }

    @Synchronized
    internal fun useGameDefaults(context: Context, game: FrontendGame) {
        writeSelection(context, game, root(context, game), null)
    }

    private fun writeSelection(context: Context, game: FrontendGame, root: File, ids: List<String>?) {
        val file = File(root, "active")
        val data = AuroraTexturePacks.isolatedData(context, game)
        val previous = if (data != null) activeTextureFolders(context, game).orEmpty() else emptyList()
        if (data != null) {
            if (!data.isDirectory && !data.mkdirs()) throw IOException("Could not prepare the game's texture storage.")
            AuroraTexturePacks.prepare(data, ids.orEmpty().map { File(root, "$it/textures") }.filter { it.isDirectory })
        }
        try {
            if (ids == null) Files.deleteIfExists(file.toPath())
            else AtomicConfigFile.writeText(file, ids.joinToString("\n"))
        } catch (failure: IOException) {
            if (data != null) runCatching { AuroraTexturePacks.prepare(data, previous) }
                .exceptionOrNull()?.let(failure::addSuppressed)
            throw failure
        }
    }

    @Synchronized
    internal fun remove(context: Context, game: FrontendGame, pack: Pack) {
        require(packId.matches(pack.id))
        if (pack.id in enabled(context, game).orEmpty()) setEnabled(context, game, pack.id, false)
        val dir = File(root(context, game), pack.id)
        val retired = File(dir.parentFile, ".removed-${pack.id}")
        Files.move(dir.toPath(), retired.toPath(), StandardCopyOption.ATOMIC_MOVE)
        retired.deleteRecursively()
    }

    internal fun catalogId(sha: String): String =
        UUID.nameUUIDFromBytes(sha.toByteArray(Charsets.US_ASCII)).toString()

    @Synchronized
    internal fun install(context: Context, game: FrontendGame, title: String,
                         source: () -> InputStream, expectedBytes: Long? = null, expectedSha: String? = null,
                         onProgress: (Long) -> Unit = {}): Pack {
        val root = root(context, game)
        if (!root.isDirectory && !root.mkdirs()) throw IOException("Could not open texture pack storage.")
        // A process interrupted during import leaves only hidden working files.
        root.listFiles().orEmpty().filter { it.name.startsWith(".incoming-") || it.name.startsWith(".removed-") }
            .forEach { it.deleteRecursively() }
        val id = expectedSha?.let(::catalogId) ?: UUID.randomUUID().toString()
        installed(context, game).firstOrNull { it.id == id }?.let { return it }
        val pack = Pack(id, title.trim().take(100).ifBlank { "Texture pack" })
        val stage = File(root, ".incoming-${pack.id}")
        if (!stage.mkdir()) throw IOException("Could not start the texture pack import.")
        try {
            if (expectedBytes != null) requireSpace(stage, expectedBytes)
            val archive = File(stage, "pack.zip")
            val digest = MessageDigest.getInstance("SHA-256")
            val bytes = source().use { input ->
                archive.outputStream().use { output ->
                    val buffer = ByteArray(64 * 1024)
                    var total = 0L
                    var nextSpaceCheck = 0L
                    while (true) {
                        val count = input.read(buffer)
                        if (count < 0) break
                        total += count
                        if (total > (expectedBytes ?: MAX_ZIP)) throw IOException("The ZIP exceeds this importer's size limit.")
                        if (total >= nextSpaceCheck) {
                            requireSpace(stage, 16 * MIB)
                            nextSpaceCheck = total + 16 * MIB
                        }
                        if (expectedSha != null) digest.update(buffer, 0, count)
                        output.write(buffer, 0, count)
                        onProgress(total)
                    }
                    total
                }
            }
            if (expectedBytes != null && bytes != expectedBytes ||
                expectedSha != null && digest.digest().joinToString("") { "%02x".format(it) } != expectedSha) {
                throw IOException("The download did not match the creator's release.")
            }
            val textures = File(stage, "textures")
            when (expectedSha) {
                MKW_RELOADED_SHA -> extractCatalogArchive(archive, textures, rar = false, kartTitle = true)
                BRAWL_HD_SHA, GALAXY_HD_SHA -> extractCatalogArchive(archive, textures, rar = false)
                NSMB_HUD_SHA -> extractCatalogArchive(archive, textures, rar = true)
                else -> extract(archive, textures, expectedSha)
            }
            if (!archive.delete()) throw IOException("Could not finish the import.")
            File(stage, "title").writeText(pack.title)
            Files.move(stage.toPath(), File(root, pack.id).toPath(), StandardCopyOption.ATOMIC_MOVE)
            return pack
        } finally {
            stage.deleteRecursively()
        }
    }

    /** Retain only matching texture filenames. No archive path is used as an output path. */
    private fun extract(archive: File, destination: File, verifiedSha: String?) {
        if (!destination.mkdir()) throw IOException("Could not create the texture folder.")
        ZipFile(archive).use { zip ->
            if (zip.size() > 100_000) throw IOException("This ZIP contains too many files.")
            val textures = linkedMapOf<String, java.util.zip.ZipEntry>()
            val buffer = ByteArray(64 * 1024)
            var total = 0L
            for (entry in zip.entries()) {
                val path = entry.name.trimEnd('/')
                val segments = path.split('/')
                if (segments.any { it.isEmpty() || it == "." || it == ".." || ':' in it || '\\' in it }) {
                    throw IOException("This ZIP contains an unsafe file path.")
                }
                val name = segments.last().lowercase(java.util.Locale.ROOT)
                if (entry.isDirectory || !textureName.matches(name) ||
                    segments.any { it == "__MACOSX" } ||
                    !includeReleaseTexture(verifiedSha, path)) continue
                if (textures.put(name, entry) != null) throw IOException("This ZIP contains duplicate textures. Choose one version of the pack.")
                if (entry.size !in 1..MAX_TEXTURE || entry.size > MAX_UNPACKED - total)
                    throw IOException("This texture pack is too large.")
                total += entry.size
            }
            if (textures.keys.none { "_mip" !in it })
                throw IOException("No compatible Dolphin-format PNG or DDS textures were found in this ZIP.")
            requireSpace(destination, total)
            for ((name, entry) in textures) {
                val crc = java.util.zip.CRC32()
                var written = 0L
                zip.getInputStream(entry).use { input ->
                    File(destination, name).outputStream().use { output ->
                        while (true) {
                            val count = input.read(buffer)
                            if (count < 0) break
                            written += count
                            if (written > entry.size) throw IOException("Invalid texture size in the ZIP.")
                            crc.update(buffer, 0, count)
                            output.write(buffer, 0, count)
                        }
                    }
                }
                if (written != entry.size || crc.value != entry.crc) throw IOException("A texture in the ZIP is damaged.")
                checkImage(File(destination, name), name.endsWith(".png"))
            }
        }
    }

    /** Non-ZIP readers only receive exact catalog payloads verified above. */
    private fun extractCatalogArchive(archive: File, destination: File, rar: Boolean, kartTitle: Boolean = false) {
        if (!destination.mkdir()) throw IOException("Could not create the texture folder.")
        val handle = CatalogArchive.open(archive.absolutePath, rar)
        try {
            val details = LongArray(2)
            val buffer = ByteArray(16 * 1024)
            val names = HashSet<String>()
            var total = 0L
            var entries = 0
            var hasBaseTexture = false
            while (true) {
                val path = CatalogArchive.next(handle, details) ?: break
                if (++entries > 100_000) throw IOException("The archive contains too many files.")
                val segments = path.trimEnd('/').split('/')
                if (segments.any { it.isEmpty() || it == "." || it == ".." || ':' in it || '\\' in it })
                    throw IOException("The archive contains an unsafe file path.")
                val name = segments.last().lowercase(java.util.Locale.ROOT)
                if (details[1] != 1L || !textureName.matches(name) || !names.add(name)) continue
                val size = details[0]
                if (size !in 1..MAX_TEXTURE || size > MAX_UNPACKED - total)
                    throw IOException("This texture pack is too large.")
                total += size
                requireSpace(destination, size)
                val output = File(destination, name)
                output.outputStream().use { stream ->
                    var written = 0L
                    while (true) {
                        val count = CatalogArchive.read(handle, buffer)
                        if (count == 0) break
                        written += count
                        if (written > size) throw IOException("Invalid texture size in the archive.")
                        stream.write(buffer, 0, count)
                    }
                    if (written != size) throw IOException("A texture in the archive is incomplete.")
                }
                // The title artwork is split into tiles. Keep the matching 6656x2528 tiles
                // together so the original and HD artwork don't form a visible seam.
                val titleTile = kartTitle && segments.drop(1).take(2) == listOf("UI", "Title")
                if (!checkImage(output, name.endsWith(".png"), allowOversized = !rar, kartTitleTile = titleTile)) {
                    if (!output.delete()) throw IOException("Could not remove an oversized texture.")
                } else if ("_mip" !in name) hasBaseTexture = true
            }
            if (!hasBaseTexture) throw IOException("No compatible textures were found in the archive.")
        } finally {
            CatalogArchive.close(handle)
        }
    }

    private fun includeReleaseTexture(verifiedSha: String?, path: String): Boolean = when (verifiedSha) {
        SUNSHINE_HD_SHA -> {
            // The port uses its own widescreen handling. Keep the shared textures instead of
            // the six Corona Mountain overrides that duplicate their hashes in this release.
            !path.startsWith("widescreen_stars_fix/") &&
                !(path.startsWith("environment/corona_mountain/") && path.substringAfterLast('/') in coronaOverrides) &&
                path != "pollution_maps/tex1_512x512_78e99302b44e7fa1_1.dds"
        }
        NO_HUD_SHA -> path.substringAfter('/').startsWith("invisible_hud/GMSE01/")
        else -> true
    }

    private fun requireSpace(directory: File, bytes: Long) {
        if (directory.usableSpace < bytes + STORAGE_RESERVE)
            throw IOException("Free more internal storage to install this texture pack.")
    }

    private fun checkImage(file: File, png: Boolean, allowOversized: Boolean = false, kartTitleTile: Boolean = false): Boolean {
        val header = ByteArray(32)
        val input = java.io.DataInputStream(file.inputStream())
        input.use { it.readFully(header) }
        val bytes = java.nio.ByteBuffer.wrap(header)
        val dimensions = if (png) {
            if (bytes.getLong(0) != 0x89504e470d0a1a0aUL.toLong() ||
                bytes.getInt(8) != 13 || bytes.getInt(12) != 0x49484452)
                throw IOException("A PNG texture has an invalid header.")
            bytes.getInt(16) to bytes.getInt(20)
        } else {
            bytes.order(java.nio.ByteOrder.LITTLE_ENDIAN)
            if (file.length() < 128 || bytes.getInt(0) != 0x20534444 || bytes.getInt(4) != 124)
                throw IOException("A DDS texture has an invalid header.")
            bytes.getInt(16) to bytes.getInt(12)
        }
        val fits = (kartTitleTile && !png && dimensions == (6656 to 2528)) ||
            dimensions.first in 1..8192 && dimensions.second in 1..8192 &&
            dimensions.first.toLong() * dimensions.second <= 16L * 1024 * 1024
        if (!fits && !allowOversized) throw IOException("A texture is too large for this pack importer.")
        return fits
    }

    /** Read only the renderer's bounded texture-index summary from its most recent launch. */
    internal fun lastIndexedCount(context: Context, game: FrontendGame): Int? {
        if (usesAurora(game) || usesNativeMatcher(game)) return null
        val external = context.getExternalFilesDir(null) ?: return null
        val profile = if (game == FrontendGame.SUNSHINE) "sms" else "eclipse"
        val log = File(external, "$profile/game/game.log")
        val prefix = root(context, game).absolutePath + "/"
        val scan = Regex("texture pack: ([0-9]+) textures under (.+)")
        return runCatching {
            if (log.length() !in 1L..131_072L) return null
            val counts = log.useLines { lines ->
                lines.mapNotNull { line ->
                    val match = scan.find(line) ?: return@mapNotNull null
                    if (!match.groupValues[2].startsWith(prefix)) return@mapNotNull null
                    match.groupValues[1].toIntOrNull()
                }.toList()
            }
            counts.takeIf { it.isNotEmpty() }?.sum()
        }.getOrNull()
    }

    internal fun openDownload(url: String): InputStream {
        var target = URL(url)
        val originHost = target.host
        val cacheHost = Regex("""filecache[0-9]{1,3}\.gamebanana\.com""")
        repeat(5) {
            val trusted = when (originHost) {
                "files.gamebanana.com" -> target.host == originHost || cacheHost.matches(target.host)
                "files.emulationking.com" -> target.host == originHost
                "github.com", "codeload.github.com" ->
                    target.host in setOf("github.com", "release-assets.githubusercontent.com", "codeload.github.com")
                else -> false
            }
            if (target.protocol != "https" || !trusted)
                throw IOException("The release redirected to an unexpected server.")
            val connection = target.openConnection() as HttpsURLConnection
            connection.instanceFollowRedirects = false
            connection.connectTimeout = 15_000
            connection.readTimeout = 30_000
            try {
                when (connection.responseCode) {
                    200 -> return object : java.io.FilterInputStream(connection.inputStream) {
                        override fun close() { try { super.close() } finally { connection.disconnect() } }
                    }
                    301, 302, 303, 307, 308 -> target = URL(target,
                        connection.getHeaderField("Location") ?: throw IOException("Missing download location."))
                    else -> throw IOException("The download server returned HTTP ${connection.responseCode}.")
                }
            } catch (e: Exception) {
                connection.disconnect()
                throw e
            }
            connection.disconnect()
        }
        throw IOException("Too many download redirects.")
    }
}
