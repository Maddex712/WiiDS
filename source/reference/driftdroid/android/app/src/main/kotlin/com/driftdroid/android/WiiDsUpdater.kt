package com.driftdroid.android

import android.app.DownloadManager
import android.content.Context
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Environment
import android.os.StatFs
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean

/** Android owns long downloads, so leaving the update screen does not lose the download. */
internal object WiiDsUpdater {
    val installing = AtomicBoolean(false)
    private const val PREFS = "wiids_app_update"
    data class Pending(val id: Long, val release: WiiDsUpdateRelease.Release)
    data class Progress(val status: Int, val downloaded: Long, val total: Long, val reason: Int)
    private fun prefs(context: Context) = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
    private fun manager(context: Context) = context.getSystemService(DownloadManager::class.java)
    fun installedCode(context: Context) = context.packageManager.getPackageInfo(context.packageName, 0).longVersionCode
    fun installedName(context: Context) = context.packageManager.getPackageInfo(context.packageName, 0).versionName ?: ""
    fun session(context: Context) = prefs(context).getInt("session", -1)
    fun session(context: Context, id: Int) { prefs(context).edit().putInt("session", id).commit() }

    fun pending(context: Context): Pending? = runCatching {
        val value = prefs(context); val id = value.getLong("download", -1L)
        if (id < 0) null else Pending(id, WiiDsUpdateRelease.restore(checkNotNull(value.getString("release", null))))
    }.getOrNull()

    fun file(context: Context, release: WiiDsUpdateRelease.Release): File {
        val base = checkNotNull(context.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS)) { "Update storage is unavailable." }.canonicalFile
        val folder = File(base, "wiids-updates")
        check(!java.nio.file.Files.isSymbolicLink(folder.toPath()) && folder.canonicalFile.parentFile == base) { "Update storage is invalid." }
        check(folder.isDirectory || folder.mkdirs()) { "Could not prepare update storage." }
        return File(folder, "release-${release.apk.id}.apk")
    }

    fun begin(context: Context, release: WiiDsUpdateRelease.Release): Pending {
        check(pending(context) == null) { "An update download already exists." }
        check(release.newerThan(installedCode(context))) { "WiiDS is already up to date." }
        val destination = file(context, release)
        check(!destination.exists() || destination.delete()) { "Could not clear the previous partial download." }
        val reserve = 256L * 1024 * 1024
        check(StatFs(destination.parent!!).availableBytes > release.apk.bytes * 2 + reserve &&
            StatFs(context.filesDir.path).availableBytes > release.apk.bytes * 2 + reserve) {
            "Free at least ${((release.apk.bytes * 2 + reserve) / 1_073_741_824.0).let { java.lang.String.format(java.util.Locale.US, "%.1f", it) }} GB before updating. Your games and saves do not need to be removed."
        }
        val request = DownloadManager.Request(Uri.parse(release.apk.url))
            .setTitle("WiiDS ${release.version} update").setDescription("Return to WiiDS > Options > Check for updates to install.")
            .setMimeType("application/vnd.android.package-archive")
            .setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED)
            .setAllowedOverRoaming(false)
            .setDestinationInExternalFilesDir(context, Environment.DIRECTORY_DOWNLOADS, "wiids-updates/${destination.name}")
        val id = manager(context).enqueue(request)
        if (!prefs(context).edit().putLong("download", id).putString("release", release.toJson().toString()).commit()) {
            manager(context).remove(id); error("Could not remember the update download. Please try again.")
        }
        return Pending(id, release)
    }

    fun progress(context: Context, pending: Pending): Progress? = manager(context)
        .query(DownloadManager.Query().setFilterById(pending.id))?.use { cursor ->
            if (!cursor.moveToFirst()) null else Progress(
                cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_STATUS)),
                cursor.getLong(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_BYTES_DOWNLOADED_SO_FAR)),
                cursor.getLong(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_TOTAL_SIZE_BYTES)),
                cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_REASON)))
        }

    fun cancel(context: Context) {
        check(!installing.get()) { "Wait for the installer to finish." }
        pending(context)?.let { pending ->
            manager(context).remove(pending.id)
            val file = file(context, pending.release)
            if (file.exists()) check(file.delete()) { "Could not remove the update download." }
        }
        prefs(context).edit().remove("download").remove("release").commit()
    }

    internal fun verifyPackage(context: Context, apk: File, release: WiiDsUpdateRelease.Release) {
        val manager = context.packageManager
        val flags = PackageManager.GET_SIGNING_CERTIFICATES
        val archive = checkNotNull(manager.getPackageArchiveInfo(apk.path, flags)) { "The download is not a valid Android app." }
        val installed = manager.getPackageInfo(context.packageName, flags)
        check(archive.packageName == context.packageName && context.packageName == "io.github.maddex712.wiids") { "The download is not WiiDS." }
        check(archive.versionName == release.version && archive.longVersionCode == release.versionCode && release.newerThan(installed.longVersionCode)) { "The downloaded build does not match the available update." }
        check(archive.applicationInfo?.flags?.and(android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) == 0) { "This is a development APK." }
        val next = archive.signingInfo?.apkContentsSigners.orEmpty().map { it.toCharsString() }.toSet()
        val current = installed.signingInfo?.apkContentsSigners.orEmpty().map { it.toCharsString() }.toSet()
        check(next.isNotEmpty() && next == current) { "The update is signed with a different key and cannot replace this copy of WiiDS." }
    }
}
