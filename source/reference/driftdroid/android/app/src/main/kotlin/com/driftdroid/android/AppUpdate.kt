package com.driftdroid.android

import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInfo
import android.content.pm.PackageInstaller
import android.content.pm.PackageManager
import android.os.Build
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest

/**
 * DriftDroid updating itself: looks for a newer release on GitHub, downloads its APK, checks it
 * against the release's checksum and against this app (same package, same signing key, not
 * older), and hands it to Android's installer, which asks the player to confirm. Saves and game
 * data stay, as with any update installed over the app.
 *
 * Ported from KartPad's KartPadUpdateCheck / KartPadUpdateInstaller / KartPadReleaseVersion
 * (chrissotraidis/kartpad, GPL-3.0), with DriftDroid's release naming and two differences: the
 * checksum comes from the release's own asset digest when the release publishes no SHA256SUMS
 * file, and a test build ("1.3.0-gpu-test2") counts as older than the release it leads up to.
 *
 * This is also what makes a new Retro Rewind release installable without the player hunting for
 * an APK: one that changes game code needs a DriftDroid built for it (see RetroRewindRelease),
 * and that build arrives through here.
 */
internal object AppUpdate {
    const val ACTION_INSTALL_STATUS = "com.driftdroid.android.UPDATE_STATUS"

    private const val RELEASES = "https://github.com/dorPXP/driftdroid/releases/"
    private const val LATEST = "https://api.github.com/repos/dorPXP/driftdroid/releases/latest"
    private const val USER_AGENT = "DriftDroid-Android"
    private const val PREFERENCES = "driftdroid_update"
    private const val CHECKED_AT = "checked_at"
    private const val VERSION = "version"
    private const val PAGE = "page"
    private const val APK = "apk"
    private const val APK_SHA256 = "apk_sha256"

    // Hourly, so a release shows up the next time the chooser opens; a daily check could hide one
    // for a whole day, including after a failed attempt.
    private const val CHECK_INTERVAL_MILLIS = 60L * 60 * 1000
    private const val CHECK_TIMEOUT_MILLIS = 10_000
    private const val DOWNLOAD_TIMEOUT_MILLIS = 20_000
    private const val MAXIMUM_RELEASE_JSON_BYTES = 512 * 1024
    private const val MAXIMUM_SUMS_BYTES = 64 * 1024
    private const val MAXIMUM_APK_BYTES = 1024L * 1024 * 1024
    private const val MAXIMUM_VERSION_PARTS = 4

    data class Update(val version: String, val pageUrl: String, val apkUrl: String?, val apkSha256: String?)

    class Cancelled : IOException("cancelled")

    // ---- Version order and trusted release URLs ----------------------------------------------

    /** "v1.3.0" or "1.3.0-gpu-test2" as numbers, or null for anything else. */
    private fun parseVersion(value: String?): IntArray? {
        val text = value?.trim()?.removePrefix("v")?.substringBefore('-') ?: return null
        val parts = text.split('.')
        if (parts.size < 2 || parts.size > MAXIMUM_VERSION_PARTS) return null
        val numbers = IntArray(MAXIMUM_VERSION_PARTS)
        for ((index, part) in parts.withIndex()) {
            if (part.isEmpty() || part.length > 6 || part.any { it !in '0'..'9' }) return null
            numbers[index] = part.toInt()
        }
        return numbers
    }

    /** True when release [candidate] ("v1.3.0") is newer than the installed [current]. A test
     * build of a version ("1.3.0-gpu-test2") is older than that version's release. */
    fun isNewer(candidate: String, current: String): Boolean {
        val next = parseVersion(candidate) ?: return false
        val installed = parseVersion(current) ?: return false
        for (index in 0 until MAXIMUM_VERSION_PARTS) {
            if (next[index] != installed[index]) return next[index] > installed[index]
        }
        return current.contains('-') && !candidate.contains('-')
    }

    /** "v1.3.0" becomes "1.3.0"; null for anything that is not a plain release version. */
    private fun normalize(tag: String): String? =
        if (tag.contains('-') || parseVersion(tag) == null) null else tag.trim().removePrefix("v")

    private fun apkName(version: String) = "DriftDroid-v$version.apk"

    private fun sumsUrl(version: String) = "${RELEASES}download/v$version/SHA256SUMS"

    private fun isReleasePage(url: String?) = url != null && url.startsWith("${RELEASES}tag/")

    private fun isReleaseDownload(url: String?) = url != null && url.startsWith("${RELEASES}download/")

    private fun isSha256(text: String) = text.length == 64 && text.all { it in '0'..'9' || it in 'a'..'f' }

    /** The lowercase SHA-256 a SHA256SUMS file lists for [fileName], or null. */
    internal fun sha256For(sums: String, fileName: String): String? {
        for (line in sums.lineSequence()) {
            val trimmed = line.trim()
            if (trimmed.length < 66 || trimmed[64] != ' ') continue
            val hash = trimmed.substring(0, 64).lowercase()
            val name = trimmed.substring(64).trim().removePrefix("*")
            if (name == fileName && isSha256(hash)) return hash
        }
        return null
    }

    fun installedVersionName(context: Context): String =
        try {
            context.packageManager.getPackageInfo(context.packageName, 0).versionName ?: ""
        } catch (_: Exception) {
            ""
        }

    // ---- Looking for a release ---------------------------------------------------------------

    /** The last release seen, if it is newer than this app. No network access. */
    fun known(context: Context): Update? {
        if (BuildConfig.THOR_BUILD) return null
        val preferences = context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)
        val version = preferences.getString(VERSION, null) ?: return null
        val page = preferences.getString(PAGE, null) ?: return null
        if (!isNewer(version, installedVersionName(context))) return null
        return Update(version, page, preferences.getString(APK, null), preferences.getString(APK_SHA256, null))
    }

    /**
     * Refreshes the stored release, at most once an hour unless [force]d. Sends nothing about the
     * player or their game, and any failure stays silent: the notice simply does not appear.
     * Blocks; call it off the main thread.
     */
    fun refresh(context: Context, force: Boolean = false, now: Long = System.currentTimeMillis()) {
        if (BuildConfig.THOR_BUILD) return
        val preferences = context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)
        val last = preferences.getLong(CHECKED_AT, 0L)
        if (!force && now >= last && now - last < CHECK_INTERVAL_MILLIS) return
        preferences.edit().putLong(CHECKED_AT, now).apply()
        val update = runCatching { parseRelease(fetchLatest()) }.getOrNull() ?: return
        preferences.edit()
            .putString(VERSION, update.version)
            .putString(PAGE, update.pageUrl)
            .putString(APK, update.apkUrl)
            .putString(APK_SHA256, update.apkSha256)
            .apply()
    }

    internal fun parseRelease(json: String): Update? {
        val release = JSONObject(json)
        if (release.optBoolean("draft") || release.optBoolean("prerelease")) return null
        val version = normalize(release.optString("tag_name")) ?: return null
        val page = release.optString("html_url").takeIf(::isReleasePage) ?: return null
        val assets = release.optJSONArray("assets")
        var apk: String? = null
        var sha256: String? = null
        for (index in 0 until (assets?.length() ?: 0)) {
            val asset = assets?.optJSONObject(index) ?: continue
            val url = asset.optString("browser_download_url")
            if (asset.optString("name") == apkName(version) && isReleaseDownload(url)) {
                apk = url
                // GitHub's own digest of the uploaded file ("sha256:<hex>").
                sha256 = asset.optString("digest").removePrefix("sha256:").lowercase().takeIf(::isSha256)
            }
        }
        return Update(version, page, apk, sha256)
    }

    private fun fetchLatest(): String {
        val connection = URL(LATEST).openConnection() as HttpURLConnection
        try {
            connection.instanceFollowRedirects = false
            connection.connectTimeout = CHECK_TIMEOUT_MILLIS
            connection.readTimeout = CHECK_TIMEOUT_MILLIS
            connection.setRequestProperty("Accept", "application/vnd.github+json")
            connection.setRequestProperty("User-Agent", USER_AGENT)
            check(connection.responseCode == HttpURLConnection.HTTP_OK) { "HTTP ${connection.responseCode}" }
            return connection.inputStream.use { String(readLimited(it, MAXIMUM_RELEASE_JSON_BYTES), Charsets.UTF_8) }
        } finally {
            connection.disconnect()
        }
    }

    // ---- Downloading, checking and installing ------------------------------------------------

    private fun directory(context: Context) = File(context.cacheDir, "updates")

    /** Removes downloaded updates; called when no newer release is pending. */
    fun clear(context: Context) {
        directory(context).listFiles()?.forEach { it.delete() }
    }

    /**
     * Downloads and verifies the update. Blocks; call it off the main thread. Throws [Cancelled]
     * when [cancelled] turns true, and IllegalStateException with a player-facing message when
     * the download can't be trusted.
     */
    fun download(
        context: Context,
        update: Update,
        progress: (done: Long, total: Long) -> Unit,
        cancelled: () -> Boolean,
    ): File {
        check(!BuildConfig.THOR_BUILD) { "Update this Thor edition from your PC to keep your game data and saves." }
        val apkUrl = checkNotNull(update.apkUrl) { "This release has no Android download." }
        val name = apkName(update.version)
        // A SHA256SUMS file published with the release wins; the asset digest GitHub reported
        // for the APK covers releases that don't have one.
        val listed =
            runCatching {
                var sums = ByteArray(0)
                open(sumsUrl(update.version)) { input, _ -> sums = readLimited(input, MAXIMUM_SUMS_BYTES) }
                sha256For(String(sums, Charsets.UTF_8), name)
            }.getOrNull()
        val expected = checkNotNull(listed ?: update.apkSha256) { "The release doesn't list a checksum for $name." }

        val folder = directory(context).apply { mkdirs() }
        clear(context)
        val partial = File(folder, "$name.partial")
        val digest = MessageDigest.getInstance("SHA-256")
        open(apkUrl) { input, total ->
            check(total <= MAXIMUM_APK_BYTES) { "The download is larger than expected." }
            partial.outputStream().use { output ->
                val buffer = ByteArray(256 * 1024)
                var done = 0L
                while (true) {
                    if (cancelled()) throw Cancelled()
                    val read = input.read(buffer)
                    if (read < 0) break
                    done += read
                    check(done <= MAXIMUM_APK_BYTES) { "The download is larger than expected." }
                    digest.update(buffer, 0, read)
                    output.write(buffer, 0, read)
                    progress(done, total)
                }
            }
        }
        val actual = digest.digest().joinToString("") { "%02x".format(it) }
        check(actual == expected) { "The download didn't match the release's checksum. Please try again." }
        val apk = File(folder, name)
        check(partial.renameTo(apk)) { "The download couldn't be saved." }
        verify(context, apk, update.version)
        return apk
    }

    /** Same app, same signing key, and not older than this one; Android checks the key again on
     * install. */
    private fun verify(context: Context, apk: File, version: String) {
        val manager = context.packageManager
        val flags = PackageManager.GET_SIGNING_CERTIFICATES
        val archive = checkNotNull(manager.getPackageArchiveInfo(apk.path, flags)) {
            "The download isn't a valid Android app."
        }
        val installed = manager.getPackageInfo(context.packageName, flags)
        check(archive.packageName == context.packageName) { "The download is a different app." }
        check(archive.versionName == version) { "The download is version ${archive.versionName}, not $version." }
        check(versionCode(archive) >= versionCode(installed)) { "The download is older than this DriftDroid." }
        check(signers(archive).isNotEmpty() && signers(archive) == signers(installed)) {
            "The download isn't signed like this DriftDroid, so Android wouldn't install it over this one."
        }
    }

    @Suppress("DEPRECATION")
    private fun versionCode(info: PackageInfo): Long =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) info.longVersionCode else info.versionCode.toLong()

    private fun signers(info: PackageInfo): Set<String> =
        info.signingInfo?.apkContentsSigners.orEmpty().map { it.toCharsString() }.toSet()

    /** Hands the verified APK to Android's installer; the result arrives as
     * [ACTION_INSTALL_STATUS]. */
    fun install(context: Context, apk: File) {
        check(!BuildConfig.THOR_BUILD) { "Update this Thor edition from your PC to keep your game data and saves." }
        val installer = context.packageManager.packageInstaller
        val params = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL).apply {
            setAppPackageName(context.packageName)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                setPackageSource(PackageInstaller.PACKAGE_SOURCE_DOWNLOADED_FILE)
            }
        }
        val sessionId = installer.createSession(params)
        installer.openSession(sessionId).use { session ->
            session.openWrite("DriftDroid.apk", 0, apk.length()).use { output ->
                apk.inputStream().use { it.copyTo(output, 256 * 1024) }
                session.fsync(output)
            }
            val status = Intent(ACTION_INSTALL_STATUS).setPackage(context.packageName)
            val flags = PendingIntent.FLAG_UPDATE_CURRENT or
                (if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0)
            session.commit(PendingIntent.getBroadcast(context, sessionId, status, flags).intentSender)
        }
    }

    private fun readLimited(input: InputStream, limit: Int): ByteArray {
        val output = ByteArrayOutputStream()
        val buffer = ByteArray(16 * 1024)
        while (true) {
            val read = input.read(buffer)
            if (read < 0) break
            output.write(buffer, 0, read)
            check(output.size() <= limit) { "The release sent more data than expected." }
        }
        return output.toByteArray()
    }

    /** GitHub release downloads redirect once to its file host; HTTPS is required throughout. */
    private fun open(url: String, body: (InputStream, Long) -> Unit) {
        check(isReleaseDownload(url)) { "Unexpected download address." }
        val connection = URL(url).openConnection() as HttpURLConnection
        try {
            connection.instanceFollowRedirects = true
            connection.connectTimeout = DOWNLOAD_TIMEOUT_MILLIS
            connection.readTimeout = DOWNLOAD_TIMEOUT_MILLIS
            connection.setRequestProperty("User-Agent", USER_AGENT)
            check(connection.responseCode == HttpURLConnection.HTTP_OK) {
                "GitHub answered ${connection.responseCode}. Please try again."
            }
            check(connection.url.protocol == "https") { "The download wasn't secure." }
            connection.inputStream.use { body(it, connection.contentLengthLong) }
        } finally {
            connection.disconnect()
        }
    }
}
