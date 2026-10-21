package com.driftdroid.android

import java.io.File
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest
import java.util.zip.ZipInputStream

/**
 * Live view of what Retro Rewind releases actually exist right now, read from the same official
 * update.rwfc.net service the mod's own updater uses - instead of only ever knowing about the one
 * release that happened to be hardcoded when the app was built.
 *
 * ## What "auto-detect the newest version" can and can't mean here
 *
 * DriftDroid is a static recompiler, not an emulator: Retro Rewind's game logic lives in
 * RetroRewind6/Binaries/Code.pul, a Kamek PowerPC code file that our translator turns into native
 * ARM code ahead of time, at build time on a desktop (see runtime/generated/build_shards and
 * RecompMod in the runtime). There is no PowerPC interpreter on the device, so a Code.pul this
 * app was never built against simply has no code to run - which is why a blind "always install
 * whatever's newest" would hand players a broken install rather than a new version.
 *
 * What genuinely can be automatic is everything else, and that's what this does:
 *  - the set of releases, their order and their URLs are discovered live, so nothing goes stale;
 *  - compatibility is decided by Code.pul's SHA-256 against [RetroRewindRelease.SUPPORTED_CODE_PUL_SHA256],
 *    NOT by version number - so any future release that leaves Code.pul untouched (an
 *    assets/tracks-only update) is adopted automatically the day it ships, with no app update;
 *  - a release that DOES change Code.pul is detected as unsupported *before* downloading 1.9GB,
 *    by reading that version's delta archive first (see [inspectDelta]), and the player
 *    is told exactly that rather than being silently left on an old version.
 *
 * Every URL is constrained to the official HTTPS hosts below; nothing here will follow a redirect
 * off them.
 */
internal object RetroRewindCatalog {

    const val MANIFEST_URL = "https://update.rwfc.net/RetroRewind/RetroRewindVersion.txt"

    /** The only hosts any request here is allowed to end up talking to, redirects included. */
    private val ALLOWED_HOSTS = setOf("update.rwfc.net", "cdn.update.rwfc.net")

    /** One line of RetroRewindVersion.txt: a version and the small delta archive that upgrades
     * the release before it to this one. */
    data class Release(val version: String, val deltaUrl: String) {
        /** Full, standalone ~1.9GB archive of this release. Named from the VERSION, which is not
         * always the delta's filename (6.12.0's delta is "6.12.0-2.zip" while its full archive is
         * "6.12.0-full.zip"), so this is derived from the version rather than from [deltaUrl]. */
        val fullArchiveUrl: String
            get() = "https://cdn.update.rwfc.net/RetroRewind/zip/$version-full.zip"
    }

    /** What a release's delta archive says about whether we can actually run that release. */
    sealed class DeltaVerdict {
        /** The delta changes Code.pul, to a build this app was translated against. */
        object SupportedCodeChange : DeltaVerdict()

        /** The delta doesn't touch Code.pul at all - assets/tracks only, so whatever code the
         * player is already running stays valid and this release is safe to take automatically. */
        object AssetsOnly : DeltaVerdict()

        /** The delta ships a Code.pul this app has no translated code for. */
        data class UnsupportedCodeChange(val sha256: String) : DeltaVerdict()

        data class Failed(val error: String) : DeltaVerdict()
    }

    /**
     * Parses RetroRewindVersion.txt. Format is one release per line:
     * `<version> <delta url> <path> <destination>`, oldest first. Unparseable lines are skipped
     * rather than failing the whole fetch - a new trailing column or a stray blank line upstream
     * shouldn't take version detection down.
     */
    fun parseManifest(text: String): List<Release> =
        text.lineSequence()
            .mapNotNull { line ->
                val fields = line.trim().split(Regex("\\s+"))
                if (fields.size < 2) return@mapNotNull null
                val version = fields[0]
                val url = fields[1]
                if (!VERSION_PATTERN.matches(version)) return@mapNotNull null
                if (!isAllowedUrl(url)) return@mapNotNull null
                Release(version, url)
            }
            .toList()

    fun fetchReleases(): Result<List<Release>> =
        try {
            val body = openStream(MANIFEST_URL, MAX_MANIFEST_BYTES).use { it.readBytes().decodeToString() }
            val releases = parseManifest(body)
            if (releases.isEmpty()) {
                Result.failure(IllegalStateException("the Retro Rewind version list came back empty"))
            } else {
                Result.success(releases)
            }
        } catch (e: Exception) {
            Result.failure(e)
        }

    /** Newest by version number, not by file order - the manifest is written oldest-first, but
     * comparing properly means a line appended out of order can't pin everyone to the wrong
     * release. */
    fun newest(releases: List<Release>): Release? = releases.maxWithOrNull(RELEASE_ORDER)

    /** The releases strictly newer than [installedVersion], oldest first: exactly the chain of
     * delta archives that upgrades an existing install, same as the mod's own updater does it. */
    fun deltaChain(releases: List<Release>, installedVersion: String, targetVersion: String): List<Release> =
        releases
            .filter { compareVersions(it.version, installedVersion) > 0 && compareVersions(it.version, targetVersion) <= 0 }
            .sortedWith(RELEASE_ORDER)

    /** Every release at or below [version], newest first. */
    fun atOrBelow(releases: List<Release>, version: String): List<Release> =
        releases.filter { compareVersions(it.version, version) <= 0 }.sortedWith(RELEASE_ORDER).asReversed()

    /** SHA-256 of an install's Binaries/Code.pul, or null if there isn't a readable one. */
    fun installedCodePulSha256(retroRewindRoot: File): String? =
        try {
            File(retroRewindRoot, "Binaries/Code.pul").takeIf { it.isFile }?.let { sha256Hex(it.readBytes()) }
        } catch (_: Exception) {
            null
        }

    /**
     * Reports whether a release is one we can run, by hashing the Code.pul inside its delta
     * archive (usually small - hundreds of KB to a few tens of MB - but 6.13.0's was 614MB). This
     * is the whole reason the app can decide "is the newest release supported?" before
     * committing a player to a huge download on mobile data.
     */
    fun inspectDelta(release: Release): DeltaVerdict {
        // Already known to change code: no need to fetch anything to judge it.
        RetroRewindRelease.KNOWN_CODE_PUL_SHA256[release.version]?.let { return verdictFor(it) }
        return try {
            // Read as it arrives and dropped as soon as Code.pul has gone by, so even a large
            // update archive is never held in memory or written to storage just to be judged.
            val codePul = openStream(release.deltaUrl, MAX_DELTA_BYTES).use { readZipEntry(it, CODE_PUL_ENTRY_SUFFIX) }
            if (codePul == null) DeltaVerdict.AssetsOnly else verdictFor(sha256Hex(codePul))
        } catch (e: Exception) {
            DeltaVerdict.Failed(e.message ?: "couldn't check that release")
        }
    }

    /** Same verdict for an update archive that has already been downloaded to [archive]. */
    fun inspectDeltaFile(archive: File): DeltaVerdict =
        try {
            val codePul = archive.inputStream().use { readZipEntry(it, CODE_PUL_ENTRY_SUFFIX) }
            if (codePul == null) DeltaVerdict.AssetsOnly else verdictFor(sha256Hex(codePul))
        } catch (e: Exception) {
            DeltaVerdict.Failed(e.message ?: "couldn't check that release")
        }

    fun isSupportedCodePul(sha256: String?): Boolean =
        sha256 != null && RetroRewindRelease.SUPPORTED_CODE_PUL_SHA256.any { it.equals(sha256, ignoreCase = true) }

    private fun verdictFor(codePulSha256: String): DeltaVerdict =
        if (isSupportedCodePul(codePulSha256)) DeltaVerdict.SupportedCodeChange else DeltaVerdict.UnsupportedCodeChange(codePulSha256)

    /**
     * Newest release this app can actually run.
     *
     * Deciding this needs a little care, because a release's own delta archive doesn't always say
     * anything about code: most releases ship assets only, and simply inherit the Code.pul set by
     * the last release that did change it. So a release is runnable when the nearest *code-
     * changing* release at or below it is one we have a translated build for - which is found by
     * walking backwards until a delta with a Code.pul in it turns up.
     *
     * Walking back also handles the case that matters in practice: a new release changes code we
     * can't run, and the answer is the newest release still on the previous, supported code -
     * which is exactly the version the player should be offered.
     *
     * [maxProbes] bounds the walk: if many releases have shipped since this app was last rebuilt,
     * there is no point downloading dozens of deltas to prove what the pinned fallback already
     * says.
     */
    fun newestSupported(releases: List<Release>, maxProbes: Int = MAX_SUPPORT_PROBES): Release? {
        val ordered = releases.sortedWith(RELEASE_ORDER).asReversed()
        // The newest release known to run on whatever code the walk is currently inspecting.
        var candidate: Release? = null
        for (release in ordered.take(maxProbes)) {
            when (inspectDelta(release)) {
                // No Code.pul in this delta: it runs on code established by an older release, so
                // keep walking back to find (and judge) that one. Being the newest such release,
                // it's the one worth installing if that code turns out to be supported.
                is DeltaVerdict.AssetsOnly -> if (candidate == null) candidate = release

                // This release sets the code that it, and every assets-only release above it,
                // runs on - and we have a build for it.
                is DeltaVerdict.SupportedCodeChange -> return candidate ?: release

                // This release introduced code we can't run, which also rules out every
                // assets-only release stacked on top of it. Drop them and keep looking further
                // back for the previous, still-supported code lineage.
                is DeltaVerdict.UnsupportedCodeChange -> candidate = null

                is DeltaVerdict.Failed -> return null
            }
        }
        return null
    }

    /** Version string in an install's RetroRewind6/version.txt, or null if it isn't readable. */
    fun installedVersion(retroRewindRoot: File): String? =
        try {
            val text = File(retroRewindRoot, "version.txt").takeIf { it.isFile }?.readText()?.trim()
            text?.takeIf { VERSION_PATTERN.matches(it) }
        } catch (_: Exception) {
            null
        }

    /** Numeric, component-wise: "6.12.10" is newer than "6.12.9", which a string compare gets
     * backwards. Missing components count as 0, so "6.12" and "6.12.0" are the same release. */
    fun compareVersions(a: String, b: String): Int {
        val left = a.split('.').mapNotNull { it.toIntOrNull() }
        val right = b.split('.').mapNotNull { it.toIntOrNull() }
        for (i in 0 until maxOf(left.size, right.size)) {
            val diff = (left.getOrNull(i) ?: 0).compareTo(right.getOrNull(i) ?: 0)
            if (diff != 0) return diff
        }
        return 0
    }

    private val RELEASE_ORDER = Comparator<Release> { a, b -> compareVersions(a.version, b.version) }

    fun isAllowedUrl(url: String): Boolean =
        try {
            val parsed = URL(url)
            parsed.protocol == "https" && parsed.host in ALLOWED_HOSTS
        } catch (_: Exception) {
            false
        }

    /**
     * HTTPS GET with redirects followed by hand, so every hop can be re-checked against
     * [ALLOWED_HOSTS] - HttpURLConnection's own redirect following would happily walk off to
     * wherever a redirect pointed, and also silently drops HTTPS->HTTPS cross-host redirects.
     */
    fun openStream(url: String, maxBytes: Long): InputStream {
        var current = url
        for (hop in 0..MAX_REDIRECTS) {
            if (!isAllowedUrl(current)) {
                throw IllegalArgumentException("refusing a URL outside the official Retro Rewind service")
            }
            val connection = (URL(current).openConnection() as HttpURLConnection).apply {
                connectTimeout = CONNECT_TIMEOUT_MS
                readTimeout = READ_TIMEOUT_MS
                instanceFollowRedirects = false
            }
            connection.connect()
            val code = connection.responseCode
            if (code in 300..399) {
                val location = connection.getHeaderField("Location")
                connection.disconnect()
                current = location ?: throw IllegalStateException("redirect with no Location header")
                continue
            }
            if (code != HttpURLConnection.HTTP_OK) {
                connection.disconnect()
                throw IllegalStateException("HTTP $code")
            }
            val declared = connection.contentLengthLong
            if (declared > maxBytes) {
                connection.disconnect()
                throw IllegalStateException("response is larger than expected ($declared bytes)")
            }
            return BoundedInputStream(connection.inputStream, maxBytes, connection)
        }
        throw IllegalStateException("too many redirects")
    }

    /** Reads the first zip entry whose name ends with [entrySuffix], without extracting the rest. */
    private fun readZipEntry(input: InputStream, entrySuffix: String): ByteArray? =
        ZipInputStream(input).use { zip ->
            while (true) {
                val entry = zip.nextEntry ?: return null
                if (!entry.isDirectory && entry.name.endsWith(entrySuffix, ignoreCase = true)) {
                    return zip.readBytes()
                }
                zip.closeEntry()
            }
            @Suppress("UNREACHABLE_CODE")
            null
        }

    fun sha256Hex(bytes: ByteArray): String =
        MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }

    /** Caps how much a response can actually deliver regardless of what its headers claimed, so a
     * server that under-reports (or omits) Content-Length can't stream unbounded data into a
     * phone's storage. */
    private class BoundedInputStream(
        private val delegate: InputStream,
        private val limit: Long,
        private val connection: HttpURLConnection,
    ) : InputStream() {
        private var read = 0L

        private fun count(n: Int): Int {
            if (n > 0) {
                read += n
                if (read > limit) throw IllegalStateException("response exceeded its expected size")
            }
            return n
        }

        override fun read(): Int {
            val value = delegate.read()
            if (value >= 0) count(1)
            return value
        }

        override fun read(b: ByteArray, off: Int, len: Int): Int = count(delegate.read(b, off, len))

        override fun close() {
            try {
                delegate.close()
            } finally {
                connection.disconnect()
            }
        }
    }

    private val VERSION_PATTERN = Regex("^[0-9]+(?:\\.[0-9]+){1,3}$")
    private const val CODE_PUL_ENTRY_SUFFIX = "Binaries/Code.pul"
    private const val MAX_MANIFEST_BYTES = 1L * 1024 * 1024
    private const val MAX_DELTA_BYTES = 4L * 1024 * 1024 * 1024
    private const val MAX_SUPPORT_PROBES = 8
    private const val MAX_REDIRECTS = 5
    private const val CONNECT_TIMEOUT_MS = 30_000
    private const val READ_TIMEOUT_MS = 30_000
}
