package com.driftdroid.android

import org.json.JSONObject
import java.io.InputStream
import java.io.OutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest

/** Public WiiDS release metadata; no account, game, save or device information is sent. */
internal object WiiDsUpdateRelease {
    const val REPOSITORY = "https://github.com/Maddex712/WiiDS"
    const val LATEST = "https://api.github.com/repos/Maddex712/WiiDS/releases/latest"
    const val MAX_APK_BYTES = 2L * 1024 * 1024 * 1024
    private const val MAX_JSON_BYTES = 512 * 1024
    private val digestPattern = Regex("[0-9a-f]{64}")
    private val versionPattern = Regex("[0-9]{1,6}(?:\\.[0-9]{1,6}){1,3}")

    data class Asset(val id: Long, val name: String, val url: String, val bytes: Long, val sha256: String)
    data class Listing(val tag: String, val apk: Asset, val inventory: Asset)
    data class Release(val tag: String, val version: String, val versionCode: Long, val apk: Asset) {
        fun newerThan(installedCode: Long) = versionCode > installedCode
        fun toJson(): JSONObject = JSONObject().put("tag", tag).put("version", version).put("versionCode", versionCode)
            .put("apk", JSONObject().put("id", apk.id).put("name", apk.name).put("url", apk.url)
                .put("bytes", apk.bytes).put("sha256", apk.sha256))
    }

    private fun version(value: String): List<Int>? {
        if (!versionPattern.matches(value)) return null
        return value.split('.').map(String::toInt).let { parts -> parts + List(4 - parts.size) { 0 } }
    }

    private fun asset(json: JSONObject, tag: String, maximum: Long): Asset {
        val name = json.getString("name")
        val url = json.getString("browser_download_url")
        val bytes = json.getLong("size")
        val hash = json.getString("digest").removePrefix("sha256:").lowercase()
        check(json.getString("digest").startsWith("sha256:") && digestPattern.matches(hash)) { "GitHub has not supplied a checksum yet. Try again shortly." }
        check(json.getLong("id") > 0 && json.optString("state") == "uploaded") { "The release upload is not complete yet." }
        check(bytes in 1..maximum) { "The release file has an unsupported size." }
        check(url == "$REPOSITORY/releases/download/$tag/$name" && !name.contains('/') && !name.contains('\\')) { "Unexpected release download address." }
        return Asset(json.getLong("id"), name, url, bytes, hash)
    }

    internal fun parseListing(json: String): Listing {
        val release = JSONObject(json)
        check(!release.optBoolean("draft") && !release.optBoolean("prerelease")) { "No stable WiiDS release is available." }
        val tag = release.getString("tag_name")
        check(tag.startsWith('v') && version(tag.drop(1)) != null) { "Unsupported release version." }
        check(release.getString("html_url") == "$REPOSITORY/releases/tag/$tag") { "The release is not from WiiDS." }
        val entries = release.getJSONArray("assets")
        val apks = ArrayList<Asset>(); val inventories = ArrayList<Asset>()
        for (index in 0 until entries.length()) {
            val entry = entries.getJSONObject(index); val name = entry.optString("name")
            if (Regex("WiiDS-[0-9.]+-arm64\\.apk").matches(name)) {
                check(version(name.removePrefix("WiiDS-").removeSuffix("-arm64.apk")) == version(tag.drop(1))) { "The APK does not match its release version." }
                apks += asset(entry, tag, MAX_APK_BYTES)
            } else if (name == "RELEASE-INVENTORY.json") inventories += asset(entry, tag, MAX_JSON_BYTES.toLong())
        }
        check(apks.size == 1 && inventories.size == 1) { "The release is still being prepared. Please try again shortly." }
        return Listing(tag, apks.single(), inventories.single())
    }

    internal fun bindInventory(listing: Listing, data: ByteArray): Release {
        check(data.size.toLong() == listing.inventory.bytes && sha256(data) == listing.inventory.sha256) { "The release inventory did not match GitHub's checksum." }
        val inventory = JSONObject(String(data, Charsets.UTF_8))
        check(inventory.getString("package") == "io.github.maddex712.wiids" && inventory.getString("tag") == listing.tag) { "The release inventory is for a different app." }
        val name = inventory.getString("version")
        check(version(name) == version(listing.tag.drop(1)) && version(name) != null) { "The release version is inconsistent." }
        check(listing.apk.name == "WiiDS-$name-arm64.apk") { "The release APK name is inconsistent." }
        val code = inventory.getLong("versionCode")
        check(code in 1..Int.MAX_VALUE.toLong()) { "The release build number is invalid." }
        val apk = inventory.getJSONObject("artifacts").getJSONObject(listing.apk.name)
        check(apk.getLong("bytes") == listing.apk.bytes && apk.getString("sha256") == listing.apk.sha256) { "The release files changed during this check. Please check again." }
        check(!inventory.optBoolean("debuggable", true)) { "The release is a development build." }
        return Release(listing.tag, name, code, listing.apk)
    }

    internal fun restore(json: String): Release {
        check(json.length <= 4096)
        val value = JSONObject(json); val a = value.getJSONObject("apk"); val tag = value.getString("tag")
        val name = value.getString("version"); val code = value.getLong("versionCode")
        check(tag.startsWith('v') && version(name) != null && version(tag.drop(1)) == version(name))
        check(code in 1..Int.MAX_VALUE.toLong() && a.getString("name") == "WiiDS-$name-arm64.apk")
        val restored = asset(JSONObject().put("id", a.getLong("id")).put("name", a.getString("name"))
            .put("browser_download_url", a.getString("url")).put("size", a.getLong("bytes"))
            .put("digest", "sha256:" + a.getString("sha256")).put("state", "uploaded"), tag, MAX_APK_BYTES)
        return Release(tag, name, code, restored)
    }

    fun latest(): Release {
        val listing = parseListing(String(fetch(LATEST, MAX_JSON_BYTES), Charsets.UTF_8))
        return bindInventory(listing, fetch(listing.inventory.url, MAX_JSON_BYTES))
    }

    private fun fetch(address: String, limit: Int): ByteArray {
        var url = URL(address)
        repeat(5) {
            check(url.protocol == "https" && url.host in setOf("api.github.com", "github.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com", "github-releases.githubusercontent.com")) { "Unexpected update server." }
            val connection = url.openConnection() as HttpURLConnection
            try {
                connection.instanceFollowRedirects = false; connection.connectTimeout = 15_000; connection.readTimeout = 30_000
                connection.useCaches = false
                connection.setRequestProperty("User-Agent", "WiiDS-Android-Updater")
                connection.setRequestProperty("Accept", "application/vnd.github+json")
                connection.setRequestProperty("Cache-Control", "no-cache")
                val status = connection.responseCode
                if (status in listOf(301, 302, 303, 307, 308)) {
                    url = URL(url, checkNotNull(connection.getHeaderField("Location"))); return@repeat
                }
                check(status == 200) { if (status == 403 || status == 429) "GitHub is limiting update checks. Please try again later." else "Could not check GitHub. Check your connection and try again." }
                return connection.inputStream.use { input ->
                    val output = java.io.ByteArrayOutputStream(); val buffer = ByteArray(8192)
                    while (true) { val n = input.read(buffer); if (n < 0) break; check(output.size() + n <= limit) { "Update metadata is too large." }; output.write(buffer, 0, n) }
                    output.toByteArray()
                }
            } finally { connection.disconnect() }
        }
        error("GitHub redirected the update too many times.")
    }

    internal fun sha256(data: ByteArray): String = hex(MessageDigest.getInstance("SHA-256").digest(data))
    private fun hex(data: ByteArray) = data.joinToString("") { "%02x".format(it.toInt() and 255) }

    /** Verify every byte again while copying into Android's private installer session. */
    internal fun copyVerified(input: InputStream, output: OutputStream, bytes: Long, expected: String,
                              cancelled: () -> Boolean = { false }, progress: (Long) -> Unit = {}) {
        check(bytes in 1..MAX_APK_BYTES && digestPattern.matches(expected))
        val hash = MessageDigest.getInstance("SHA-256"); val buffer = ByteArray(256 * 1024); var done = 0L
        while (true) {
            check(!cancelled()) { "Update cancelled." }
            val n = input.read(buffer); if (n < 0) break
            done += n; check(done <= bytes) { "The download is larger than the verified release." }
            hash.update(buffer, 0, n); output.write(buffer, 0, n); progress(done)
        }
        check(done == bytes && hex(hash.digest()) == expected) { "The download did not match the release. Cancel it and download again." }
    }
}
