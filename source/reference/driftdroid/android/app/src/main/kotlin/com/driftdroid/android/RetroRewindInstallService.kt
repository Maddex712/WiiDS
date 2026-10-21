package com.driftdroid.android

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.net.Uri
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import androidx.documentfile.provider.DocumentFile
import java.io.File

/**
 * Runs the ~2GB Retro Rewind copy/extract as a foreground service instead of a plain Thread owned
 * by ModePickerActivity (see git history/session notes: a bare background Thread died the instant
 * Android reclaimed the backgrounded activity's process for memory - a real risk for an operation
 * that realistically takes several minutes and invites the user to go do something else while it
 * runs). A foreground service with an active notification tells Android to treat this process as
 * foreground-priority for the duration, so it survives the Activity being destroyed/recreated and
 * ordinary background memory reclaim. It does NOT survive a force-stop or reboot - that would need
 * WorkManager's durable, persisted work requests, which is a bigger lift than this problem
 * currently warrants (queued as follow-up if it turns out to matter in practice).
 *
 * Progress is published to [RetroRewindInstallStatus] rather than any direct callback, since the
 * whole point is that no particular Activity instance can be assumed to be alive to receive one.
 */
class RetroRewindInstallService : Service() {

    // Volatile: written by the worker thread when it finishes, read on the main thread in
    // onStartCommand.
    @Volatile private var worker: Thread? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (BuildConfig.THOR_BUILD) {
            // Thor uses a staged transaction and a runtime-write lease. None of the
            // legacy live-folder/ZIP/delta mutation paths may bypass that boundary.
            RetroRewindInstallStatus.complete("Use the Retro Rewind updates page on this Thor build.")
            stopSelf(startId)
            return START_NOT_STICKY
        }
        if (intent == null || worker != null) {
            // Already running (or a stray restart with no intent) - nothing new to start.
            return START_NOT_STICKY
        }

        ensureNotificationChannel()
        startForegroundCompat(buildNotification("Preparing Retro Rewind install..."))

        val thread =
            Thread {
                // Anything thrown in here would otherwise be an uncaught exception on a plain
                // Thread, i.e. the whole app disappearing mid-install with no message. A failed
                // import has to come back as an error string the picker can show instead.
                RetroRewindInstallStatus.installIncomplete = false
                val error =
                    try {
                        when (intent.action) {
                            ACTION_INSTALL_FOLDER -> runFolderInstall(intent)
                            ACTION_INSTALL_ZIP -> runZipInstall(intent)
                            ACTION_INSTALL_DOWNLOAD -> runDownloadInstall()
                            ACTION_INSTALL_LATEST -> runLatestInstall()
                            ACTION_UPDATE_LATEST -> runDeltaUpdate()
                            else -> "unknown install action"
                        }
                    } catch (e: Throwable) {
                        "${e.javaClass.simpleName}: ${e.message ?: "unknown error"}"
                    }
                // Cleared before anything else: the service instance can outlive this run (it is
                // only stopped below, and Android may hand the same instance a later start), and
                // a stale non-null worker made every retry after a failed import a silent no-op -
                // the picker just span forever because nothing was ever started for it.
                worker = null
                RetroRewindInstallStatus.complete(error)
                stopForegroundCompat()
                stopSelf(startId)
            }
        worker = thread
        thread.start()
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        // Deliberately does NOT interrupt worker - if Android is stopping this service instance
        // for its own reasons while the copy is still going, letting the copy finish (or fail on
        // its own terms, e.g. a real I/O error) is safer than tearing it down mid-write. The
        // service being foreground-priority is what's supposed to prevent this from happening
        // under ordinary memory pressure in the first place.
        super.onDestroy()
    }

    private fun runFolderInstall(intent: Intent): String? {
        val sourceUri = intent.getParcelableUriExtra(EXTRA_SOURCE_URI) ?: return "missing source"
        // fromTreeUri, NOT fromSingleUri: this is a document inside the picked SAF tree, and a
        // single-document DocumentFile has no directory support at all - listFiles()/findFile()
        // on one throw UnsupportedOperationException, which escaped this worker thread and killed
        // the process. fromTreeUri on a tree-backed document URI keeps pointing at that same
        // document (it re-derives the id via buildDocumentUriUsingTree), so the resolved
        // RetroRewind6 folder is still what gets copied - now as a real directory.
        val source = DocumentFile.fromTreeUri(this, sourceUri) ?: return "couldn't open the selected folder"
        if (!source.isDirectory) return "the selected item isn't a folder"
        RetroRewindInstallStatus.start("Copying Retro Rewind files")

        // Checked before copying ~2GB, not after: source is already the resolved RetroRewind6
        // folder (ModePickerActivity.resolveRetroRewind6 picked it), so Binaries/Code.pul is
        // directly underneath it.
        val codePul = source.findFile("Binaries")?.findFile("Code.pul")
            ?: return "the selected folder doesn't contain RetroRewind6/Binaries/Code.pul"
        checkRetroRewindVersion(sha256OfDocument(codePul))?.let { return it }

        val destRoot = retroRewindRoot()

        val requiredBytes = sumTreeSize(source)
        checkAvailableSpace((requiredBytes.toDouble() * FOLDER_COPY_SPACE_FACTOR).toLong())?.let { return it }

        // Same fail-closed ordering as the zip path: only once the version check and the space
        // check have both passed does the existing install get cleared. Copying over the top of
        // it instead would leave files from the previous install mixed into the new one.
        RetroRewindInstallStatus.installIncomplete = true
        destRoot.deleteRecursively()

        val filesCopied = intArrayOf(0)
        val error = copyTree(source, destRoot, filesCopied)
        if (error == null) RetroRewindInstallStatus.installIncomplete = false
        return error
    }

    private fun runZipInstall(intent: Intent): String? {
        val zipUri = intent.getParcelableUriExtra(EXTRA_SOURCE_URI) ?: return "missing source"
        val name = intent.getStringExtra(EXTRA_ZIP_NAME) ?: "the archive"
        RetroRewindInstallStatus.start("Extracting \"$name\"")

        val zipSize = queryFileSize(zipUri)
        if (zipSize > 0) {
            checkAvailableSpace((zipSize.toDouble() * ZIP_EXTRACT_SPACE_FACTOR).toLong())?.let { return it }
        }

        val scratchDir = File(filesDir, "WiiCompiled/.retro_rewind_zip_tmp")
        val filesExtracted = intArrayOf(0)
        val error = extractZipAndInstall({ contentResolver.openInputStream(zipUri) }, scratchDir, filesExtracted)
        scratchDir.deleteRecursively()
        return error
    }

    /**
     * Fetches the one official, pinned Retro Rewind release directly from update.rwfc.net (see
     * RetroRewindRelease.kt) instead of requiring the player to have already downloaded a zip
     * themselves - matches how KartPad's Android port does its Retro Rewind install. Verified by
     * exact byte size and SHA-256 before it's ever unzipped, so a corrupted/truncated/tampered
     * download fails closed rather than silently installing something unexpected.
     */
    private fun runDownloadInstall(): String? {
        RetroRewindInstallStatus.start("Downloading Retro Rewind ${RetroRewindRelease.VERSION}")
        // Enough room for the downloaded archive AND its extracted contents to coexist, plus the
        // same scratch/rename headroom the SAF zip path budgets for.
        checkAvailableSpace(
            RetroRewindRelease.ARCHIVE_BYTES + (RetroRewindRelease.ARCHIVE_BYTES.toDouble() * DOWNLOAD_EXTRACT_SPACE_FACTOR).toLong(),
        )?.let { return it }

        val downloadFile = File(cacheDir, "RetroRewind-${RetroRewindRelease.VERSION}.zip")
        downloadToFile(
            RetroRewindRelease.ARCHIVE_URL,
            downloadFile,
            RetroRewindRelease.ARCHIVE_BYTES,
            "Downloading Retro Rewind ${RetroRewindRelease.VERSION}",
        )?.let { return it }
        // Hashed after the fact rather than while streaming, because a resumed download only ever
        // sees the tail of the file - the whole thing has to be read back to verify it.
        RetroRewindInstallStatus.start("Verifying Retro Rewind ${RetroRewindRelease.VERSION}")
        val actualHash = sha256OfFile(downloadFile)
        if (actualHash == null || !actualHash.equals(RetroRewindRelease.ARCHIVE_SHA256, ignoreCase = true)) {
            downloadFile.delete()
            return "downloaded file failed verification (hash mismatch) - try again"
        }

        RetroRewindInstallStatus.start("Extracting Retro Rewind ${RetroRewindRelease.VERSION}")
        val scratchDir = File(filesDir, "WiiCompiled/.retro_rewind_zip_tmp")
        val filesExtracted = intArrayOf(0)
        val error = extractZipAndInstall({ downloadFile.inputStream() }, scratchDir, filesExtracted)
        scratchDir.deleteRecursively()
        downloadFile.delete()
        if (error == null) {
            RetroRewindInstallStatus.successMessage = "Retro Rewind ${RetroRewindRelease.VERSION} installed."
        }
        return error
    }

    /**
     * Installs the newest release the live catalog offers that this build can actually run.
     *
     * The expensive part (1.9GB) is only ever started once the cheap part has proven it is worth
     * starting: [RetroRewindCatalog.newestSupported] settles compatibility from ~700KB delta
     * archives first. If the newest release changed game code we have no translated build for,
     * this says so plainly instead of installing something broken; if the network is unavailable
     * it falls back to the pinned, hash-verified release rather than failing outright.
     */
    private fun runLatestInstall(): String? {
        RetroRewindInstallStatus.start("Checking for the newest Retro Rewind")
        val releases =
            RetroRewindCatalog.fetchReleases().getOrElse {
                // Offline or the service is down - the pinned build is still a perfectly good
                // install, so quietly do that rather than leave the player with nothing.
                return runDownloadInstall()
            }
        val newest = RetroRewindCatalog.newest(releases)
        val target = RetroRewindCatalog.newestSupported(releases) ?: return unsupportedReleaseMessage(newest?.version)

        // Not every release has a full archive: 6.13.1 exists only as a small update on top of
        // 6.13.0's (the site's own "Full Download" link points at 6.13.0's too). So start from
        // the newest release at or below the target that does have one, then apply the updates
        // above it - the same two steps the mod's own installer takes.
        var base: RetroRewindCatalog.Release? = null
        var baseBytes = -1L
        for (candidate in RetroRewindCatalog.atOrBelow(releases, target.version).take(MAX_FULL_ARCHIVE_PROBES)) {
            // The pinned release has a known archive size AND hash - no need to ask.
            val bytes =
                if (candidate.version == RetroRewindRelease.VERSION) RetroRewindRelease.ARCHIVE_BYTES
                else contentLengthOf(candidate.fullArchiveUrl)
            if (bytes > 0) {
                base = candidate
                baseBytes = bytes
                break
            }
        }
        if (base == null) {
            return "couldn't find a full download for Retro Rewind ${target.version} on the update service - try again later"
        }

        // When the pinned release IS the base, take the stricter path that verifies the whole
        // download against its known hash up front.
        val installError =
            if (base.version == RetroRewindRelease.VERSION) runDownloadInstall() else installFullArchive(base, baseBytes)
        if (installError != null) return installError
        writeInstalledVersion(base.version)

        if (base.version != target.version) {
            val updateError = applyDeltaChain(RetroRewindCatalog.deltaChain(releases, base.version, target.version))
            if (updateError != null) {
                // The full install itself is complete and playable, so this is not a failed
                // install - say what happened and leave the rest to "Check for updates".
                val reached = RetroRewindCatalog.installedVersion(retroRewindRoot()) ?: base.version
                RetroRewindInstallStatus.successMessage =
                    "Retro Rewind $reached installed. Updating it to ${target.version} didn't finish " +
                        "($updateError) - use \"Check for updates\" to try again."
                return null
            }
        }
        RetroRewindInstallStatus.successMessage = "Retro Rewind ${target.version} installed."
        return null
    }

    private fun installFullArchive(release: RetroRewindCatalog.Release, expectedBytes: Long): String? {
        RetroRewindInstallStatus.start("Downloading Retro Rewind ${release.version}")
        checkAvailableSpace(expectedBytes + (expectedBytes.toDouble() * DOWNLOAD_EXTRACT_SPACE_FACTOR).toLong())?.let { return it }

        val downloadFile = File(cacheDir, "RetroRewind-${release.version}.zip")
        // Note: a failed download deliberately leaves its partial file in place - downloadToFile
        // resumes from it next time, which is the difference between "eventually finishes on a
        // flaky connection" and "never finishes".
        downloadToFile(release.fullArchiveUrl, downloadFile, expectedBytes, "Downloading Retro Rewind ${release.version}")
            ?.let { return it }

        RetroRewindInstallStatus.start("Extracting Retro Rewind ${release.version}")
        val scratchDir = File(filesDir, "WiiCompiled/.retro_rewind_zip_tmp")
        val filesExtracted = intArrayOf(0)
        // extractZipAndInstall re-checks Code.pul against the supported set before it replaces
        // anything, so an archive that isn't what the catalog implied still can't get installed.
        val error = extractZipAndInstall({ downloadFile.inputStream() }, scratchDir, filesExtracted)
        scratchDir.deleteRecursively()
        downloadFile.delete()
        return error
    }

    /**
     * Brings an existing install up to date by applying the same small per-version delta archives
     * the mod's own updater uses - a few hundred KB to a few tens of MB per version, instead of
     * re-downloading the whole 1.9GB pack. Deltas are applied oldest-first so intermediate
     * versions aren't skipped, exactly as upstream intends them to be.
     */
    private fun runDeltaUpdate(): String? {
        val destRoot = retroRewindRoot()
        if (!File(destRoot, "Binaries/Code.pul").isFile) {
            return "Retro Rewind isn't installed yet - install it first"
        }
        RetroRewindInstallStatus.start("Checking for Retro Rewind updates")
        val installed =
            RetroRewindCatalog.installedVersion(destRoot)
                ?: return "couldn't tell which Retro Rewind version is installed (no readable version.txt) - " +
                    "reinstalling will fix that"
        val releases = RetroRewindCatalog.fetchReleases().getOrElse {
            return "couldn't reach the Retro Rewind update service (${it.message ?: "no connection"})"
        }
        val newest = RetroRewindCatalog.newest(releases)
        val target = RetroRewindCatalog.newestSupported(releases) ?: return unsupportedReleaseMessage(newest?.version)

        if (RetroRewindCatalog.compareVersions(target.version, installed) <= 0) {
            RetroRewindInstallStatus.successMessage =
                if (newest != null && RetroRewindCatalog.compareVersions(newest.version, installed) > 0) {
                    "Retro Rewind $installed is up to date for this build. (${newest.version} exists but " +
                        "changes game code DriftDroid hasn't been compiled for yet.)"
                } else {
                    "Retro Rewind $installed is already up to date."
                }
            return null
        }

        val chain = RetroRewindCatalog.deltaChain(releases, installed, target.version)
        if (chain.isEmpty()) {
            RetroRewindInstallStatus.successMessage = "Retro Rewind $installed is already up to date."
            return null
        }

        applyDeltaChain(chain)?.let { return it }
        RetroRewindInstallStatus.successMessage = "Retro Rewind updated to ${target.version}."
        return null
    }

    /**
     * Downloads and applies [chain]'s update archives over the live install, oldest first.
     * Returns null once every one of them is applied.
     */
    private fun applyDeltaChain(chain: List<RetroRewindCatalog.Release>): String? {
        val applied = intArrayOf(0)
        for ((index, release) in chain.withIndex()) {
            val step = if (chain.size > 1) " (${index + 1} of ${chain.size})" else ""
            val deltaFile = File(cacheDir, "RetroRewind-delta-${release.version}.zip")
            val beforeThisRelease = applied[0]
            // Most of these are a few MB, but one that swaps a lot of tracks can be several
            // hundred (6.13.0's is 614MB) - so it gets the same resumable, progress-reporting
            // download as a full install, and a free-space check before it starts.
            RetroRewindInstallStatus.start("Downloading the ${release.version} update$step")
            val deltaBytes = contentLengthOf(release.deltaUrl)
            val downloadError =
                (if (deltaBytes > 0) checkAvailableSpace(deltaBytes * 2) else null)
                    ?: downloadToFile(release.deltaUrl, deltaFile, deltaBytes, "Downloading the ${release.version} update")
            if (downloadError != null) {
                // Nothing of this release has been applied, and its partial download is kept so
                // the next attempt resumes instead of starting over.
                return downloadError
            }
            RetroRewindInstallStatus.start("Updating to ${release.version}$step")
            RetroRewindInstallStatus.filesDone = applied[0]
            val error =
                when (val verdict = RetroRewindCatalog.inspectDeltaFile(deltaFile)) {
                    is RetroRewindCatalog.DeltaVerdict.Failed -> verdict.error
                    is RetroRewindCatalog.DeltaVerdict.UnsupportedCodeChange ->
                        // newestSupported() already ruled this out for the target, so reaching it
                        // here means an intermediate version in the chain changes code we can't
                        // run. Stopping at the last good version is correct: the install stays
                        // playable at whatever it reached.
                        unsupportedReleaseMessage(release.version)
                    else -> applyDeltaArchive(deltaFile, retroRewindRoot(), applied)
                }
            deltaFile.delete()
            if (error != null) {
                // Earlier deltas in the chain each completed and recorded their version, so the
                // install is consistent at whichever one last finished.
                // A delta that failed PART WAY through is the one case that can't be patched up
                // here: deltas replace files in place, so some of ${release.version}'s files are
                // now mixed into an otherwise older install and there's nothing to roll back to.
                // Say so, instead of leaving the player wondering why the game misbehaves.
                return if (applied[0] > beforeThisRelease) {
                    "$error - the update to ${release.version} stopped part way through, so this " +
                        "install is now a mix of versions. Reinstall Retro Rewind to get back to a " +
                        "clean copy."
                } else {
                    error
                }
            }
            writeInstalledVersion(release.version)
        }
        return null
    }

    /**
     * Unpacks a delta archive straight over the live install. Entries are rooted at
     * "RetroRewind6/", which IS the install directory here, so that prefix is stripped rather
     * than creating a nested copy. Deltas only ever add or replace files - nothing is deleted -
     * which is what makes applying one over an existing install safe.
     */
    private fun applyDeltaArchive(archive: File, destRoot: File, filesApplied: IntArray): String? {
        val destCanonical = destRoot.canonicalFile
        return try {
            archive.inputStream().use { raw ->
                java.util.zip.ZipInputStream(raw).use { zip ->
                    var entry = zip.nextEntry
                    while (entry != null) {
                        val relative = entry.name.substringAfter("RetroRewind6/", entry.name)
                        if (relative.isNotEmpty()) {
                            val outFile = File(destRoot, relative)
                            if (!outFile.canonicalPath.startsWith(destCanonical.path + File.separator) &&
                                outFile.canonicalPath != destCanonical.path
                            ) {
                                return "that update archive contains an unsafe file path"
                            }
                            if (entry.isDirectory) {
                                outFile.mkdirs()
                            } else {
                                outFile.parentFile?.mkdirs()
                                outFile.outputStream().use { output -> zip.copyTo(output) }
                                filesApplied[0]++
                                RetroRewindInstallStatus.filesDone = filesApplied[0]
                                updateNotificationThrottled("Updating... ${filesApplied[0]} files applied")
                            }
                        }
                        zip.closeEntry()
                        entry = zip.nextEntry
                    }
                }
            }
            null
        } catch (e: Exception) {
            e.message ?: "unknown error"
        }
    }

    private fun writeInstalledVersion(version: String) {
        try {
            File(retroRewindRoot(), "version.txt").writeText(version)
        } catch (_: Exception) {
            // Cosmetic only - a missing version.txt just means the next update check asks for a
            // reinstall instead of applying deltas.
        }
    }

    private fun unsupportedReleaseMessage(version: String?): String =
        if (BuildConfig.THOR_BUILD) {
            "This Retro Rewind release needs a newer Thor edition. Your current installation is unchanged. " +
                "Update the Thor app from your PC, then try again."
        } else "Retro Rewind ${version ?: "(newest)"} changes game code this build of DriftDroid hasn't been " +
            "compiled for yet, so it hasn't been installed. Your current install is untouched. If an " +
            "\"Update available\" button appears below, tap it to update DriftDroid, then try again."

    /** HEAD probe for a download's size, used for the free-space check and the progress bar
     * before a big download starts. Returns -1 when the server doesn't say. */
    private fun contentLengthOf(url: String): Long {
        var current = url
        try {
            for (hop in 0..MAX_REDIRECTS) {
                if (!RetroRewindCatalog.isAllowedUrl(current)) return -1L
                val connection = (java.net.URL(current).openConnection() as java.net.HttpURLConnection).apply {
                    requestMethod = "HEAD"
                    connectTimeout = CONNECT_TIMEOUT_MS
                    readTimeout = READ_TIMEOUT_MS
                    instanceFollowRedirects = false
                }
                connection.connect()
                val code = connection.responseCode
                if (code in 300..399) {
                    val location = connection.getHeaderField("Location")
                    connection.disconnect()
                    current = location ?: return -1L
                    continue
                }
                val length = if (code == java.net.HttpURLConnection.HTTP_OK) connection.contentLengthLong else -1L
                connection.disconnect()
                return length
            }
            return -1L
        } catch (_: Exception) {
            return -1L
        }
    }

    /**
     * Resumable download. A 1.9GB pack over phone data realistically gets interrupted, and
     * restarting from zero every time is how an install becomes impossible on a bad connection -
     * so a partial file is kept and continued with a Range request. A server that ignores Range
     * (answering 200 instead of 206) is handled by simply starting the file over.
     */
    private fun downloadToFile(url: String, dest: File, expectedBytes: Long, label: String): String? {
        var current = url
        try {
            for (hop in 0..MAX_REDIRECTS) {
                if (!RetroRewindCatalog.isAllowedUrl(current)) {
                    return "refusing a URL outside the official Retro Rewind service"
                }
                val existing = if (dest.isFile) dest.length() else 0L
                val connection = (java.net.URL(current).openConnection() as java.net.HttpURLConnection).apply {
                    connectTimeout = CONNECT_TIMEOUT_MS
                    readTimeout = READ_TIMEOUT_MS
                    instanceFollowRedirects = false
                    if (existing > 0) setRequestProperty("Range", "bytes=$existing-")
                }
                connection.connect()
                val code = connection.responseCode
                if (code in 300..399) {
                    val location = connection.getHeaderField("Location")
                    connection.disconnect()
                    current = location ?: return "redirect with no Location header"
                    continue
                }
                val resuming = code == java.net.HttpURLConnection.HTTP_PARTIAL
                if (code != java.net.HttpURLConnection.HTTP_OK && !resuming) {
                    connection.disconnect()
                    return "download failed (HTTP $code)"
                }
                val startAt = if (resuming) existing else 0L
                val total = if (expectedBytes > 0) expectedBytes else startAt + connection.contentLengthLong
                if (total > MAX_FULL_ARCHIVE_BYTES) {
                    connection.disconnect()
                    return "that download is larger than expected ($total bytes)"
                }

                RetroRewindInstallStatus.totalBytes = total
                var downloaded = startAt
                connection.inputStream.use { input ->
                    java.io.FileOutputStream(dest, resuming).use { output ->
                        val buffer = ByteArray(1 shl 16)
                        while (true) {
                            val read = input.read(buffer)
                            if (read < 0) break
                            output.write(buffer, 0, read)
                            downloaded += read
                            if (downloaded > MAX_FULL_ARCHIVE_BYTES) return "that download is larger than expected"
                            RetroRewindInstallStatus.downloadedBytes = downloaded
                            updateNotificationThrottled(
                                "$label... ${downloaded / (1024 * 1024)}MB" +
                                    if (total > 0) " / ${total / (1024 * 1024)}MB" else "",
                            )
                        }
                    }
                }
                connection.disconnect()
                if (expectedBytes > 0 && dest.length() != expectedBytes) {
                    // Wrong content, not a dropped connection - a resume would keep building on
                    // bad bytes, so start the next attempt from scratch.
                    val got = dest.length()
                    dest.delete()
                    return "download size mismatch (got $got bytes, expected $expectedBytes)"
                }
                return null
            }
            return "too many redirects"
        } catch (e: Exception) {
            return e.message ?: "download failed"
        }
    }

    // --- Copy/extract logic, moved verbatim in spirit from ModePickerActivity - only the
    // notification progress hook and RetroRewindInstallStatus writes are new. ---

    private fun retroRewindRoot(): File = File(filesDir, MainActivity.RETRO_REWIND_STAGING_SUBDIR)

    private fun checkAvailableSpace(requiredBytes: Long): String? {
        val stat = android.os.StatFs(filesDir.path)
        val available = stat.availableBytes
        val needed = requiredBytes + SPACE_RESERVE_BYTES
        if (available < needed) {
            val availableMb = available / (1024 * 1024)
            val neededMb = needed / (1024 * 1024)
            return "Not enough free storage: ${availableMb}MB available, need about ${neededMb}MB. Free up space and try again."
        }
        return null
    }

    private fun sumTreeSize(doc: DocumentFile): Long {
        var total = 0L
        for (child in doc.listFiles()) {
            if (Thread.currentThread().isInterrupted) return total
            total += if (child.isDirectory) sumTreeSize(child) else child.length()
        }
        return total
    }

    private fun copyTree(source: DocumentFile, destDir: File, filesCopied: IntArray): String? {
        return try {
            destDir.mkdirs()
            for (child in source.listFiles()) {
                val name = child.name ?: continue
                if (child.isDirectory) {
                    val error = copyTree(child, File(destDir, name), filesCopied)
                    if (error != null) return error
                } else {
                    contentResolver.openInputStream(child.uri)?.use { input ->
                        File(destDir, name).outputStream().use { output -> input.copyTo(output) }
                    } ?: return "couldn't open $name"
                    filesCopied[0]++
                    RetroRewindInstallStatus.filesDone = filesCopied[0]
                    updateNotificationThrottled("Copying Retro Rewind files... ${filesCopied[0]} files done")
                }
            }
            null
        } catch (e: Exception) {
            e.message ?: "unknown error"
        }
    }

    private fun queryFileSize(uri: Uri): Long =
        try {
            contentResolver.query(uri, null, null, null, null)?.use { cursor ->
                val index = cursor.getColumnIndex(android.provider.OpenableColumns.SIZE)
                if (index >= 0 && cursor.moveToFirst() && !cursor.isNull(index)) cursor.getLong(index) else -1L
            } ?: -1L
        } catch (_: Exception) {
            -1L
        }

    private fun extractZipAndInstall(
        openInput: () -> java.io.InputStream?,
        scratchDir: File,
        filesExtracted: IntArray,
    ): String? {
        return try {
            scratchDir.deleteRecursively()
            scratchDir.mkdirs()
            val scratchCanonical = scratchDir.canonicalFile
            openInput()?.use { input ->
                java.util.zip.ZipInputStream(input).use { zip ->
                    var entry = zip.nextEntry
                    while (entry != null) {
                        val outFile = File(scratchDir, entry.name)
                        if (!outFile.canonicalPath.startsWith(scratchCanonical.path + File.separator) &&
                            outFile.canonicalPath != scratchCanonical.path
                        ) {
                            return "archive contains an unsafe file path"
                        }
                        if (entry.isDirectory) {
                            outFile.mkdirs()
                        } else {
                            outFile.parentFile?.mkdirs()
                            outFile.outputStream().use { output -> zip.copyTo(output) }
                            filesExtracted[0]++
                            RetroRewindInstallStatus.filesDone = filesExtracted[0]
                            updateNotificationThrottled("Extracting... ${filesExtracted[0]} files done")
                        }
                        zip.closeEntry()
                        entry = zip.nextEntry
                    }
                }
            } ?: return "couldn't open the archive"

            val resolved = resolveRetroRewind6(scratchDir) ?: return "the archive doesn't contain RetroRewind6/Binaries/Code.pul"
            // Checked before touching the existing install: on a version mismatch this returns
            // without ever deleting/replacing destRoot, so a previously-working install is left
            // alone instead of being wiped out by a bad import.
            checkRetroRewindVersion(sha256OfFile(File(resolved, "Binaries/Code.pul")))?.let { return it }
            val destRoot = retroRewindRoot()
            RetroRewindInstallStatus.installIncomplete = true
            destRoot.deleteRecursively()
            destRoot.parentFile?.mkdirs()
            if (!resolved.renameTo(destRoot)) {
                resolved.copyRecursively(destRoot, overwrite = true)
            }
            RetroRewindInstallStatus.installIncomplete = false
            null
        } catch (e: Exception) {
            e.message ?: "unknown error"
        }
    }

    private fun resolveRetroRewind6(picked: File): File? {
        val candidates = mutableListOf(picked, File(picked, "RetroRewind6"))
        picked.listFiles()?.forEach { child ->
            if (child.isDirectory) {
                candidates.add(File(child, "RetroRewind6"))
            }
        }
        return candidates.firstOrNull { File(it, "Binaries/Code.pul").isFile }
    }

    /**
     * Our Retro Rewind support is a static, ahead-of-time recompilation of specific Code.pul
     * builds (see RetroRewindRelease.SUPPORTED_CODE_PUL_SHA256) - a zip/folder from any other
     * release genuinely differs at the byte level and desyncs from that recompiled logic at
     * runtime instead of failing cleanly. Reject it here, before it ever replaces a working
     * install, rather than let the app crash/hang later during gameplay.
     *
     * Matching on the hash rather than the version string is deliberate: it is what lets a new
     * Retro Rewind release that only changed assets/tracks install automatically, with no app
     * update, while one that changed game code is caught.
     */
    private fun checkRetroRewindVersion(codePulSha256: String?): String? {
        if (codePulSha256 == null) {
            return "couldn't read Binaries/Code.pul to verify its version"
        }
        if (!RetroRewindCatalog.isSupportedCodePul(codePulSha256)) {
            if (BuildConfig.THOR_BUILD) return unsupportedReleaseMessage(null)
            return "this copy of Retro Rewind has different game code from the one this build of " +
                "DriftDroid is compiled for (Retro Rewind ${RetroRewindRelease.VERSION} and the " +
                "updates on top of it) - installing it would crash or misbehave, so it's been left alone"
        }
        return null
    }

    private fun sha256OfFile(file: File): String? =
        try {
            val digest = java.security.MessageDigest.getInstance("SHA-256")
            file.inputStream().use { input ->
                val buffer = ByteArray(1 shl 16)
                while (true) {
                    val read = input.read(buffer)
                    if (read < 0) break
                    digest.update(buffer, 0, read)
                }
            }
            digest.digest().joinToString("") { "%02x".format(it) }
        } catch (_: Exception) {
            null
        }

    private fun sha256OfDocument(doc: DocumentFile): String? {
        return try {
            val digest = java.security.MessageDigest.getInstance("SHA-256")
            val stream = contentResolver.openInputStream(doc.uri) ?: return null
            stream.use { input ->
                val buffer = ByteArray(1 shl 16)
                while (true) {
                    val read = input.read(buffer)
                    if (read < 0) break
                    digest.update(buffer, 0, read)
                }
            }
            digest.digest().joinToString("") { "%02x".format(it) }
        } catch (_: Exception) {
            null
        }
    }

    // --- Notification plumbing ---

    private var lastNotificationUpdateMs = 0L

    private fun updateNotificationThrottled(text: String) {
        val now = System.currentTimeMillis()
        if (now - lastNotificationUpdateMs < NOTIFICATION_UPDATE_INTERVAL_MS) return
        lastNotificationUpdateMs = now
        val manager = getSystemService(NotificationManager::class.java)
        try {
            manager?.notify(NOTIFICATION_ID, buildNotification(text))
        } catch (_: SecurityException) {
            // POST_NOTIFICATIONS not granted - the service (and the copy) still runs fine, the
            // user just won't see live progress in the notification shade.
        }
    }

    private fun buildNotification(text: String): Notification =
        NotificationCompat.Builder(this, NOTIFICATION_CHANNEL_ID)
            .setContentTitle("Installing Retro Rewind")
            .setContentText(text)
            .setSmallIcon(R.mipmap.ic_launcher)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .build()

    private fun ensureNotificationChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return
        val manager = getSystemService(NotificationManager::class.java) ?: return
        if (manager.getNotificationChannel(NOTIFICATION_CHANNEL_ID) != null) return
        val channel =
            NotificationChannel(
                NOTIFICATION_CHANNEL_ID,
                "Retro Rewind install",
                NotificationManager.IMPORTANCE_LOW,
            )
        manager.createNotificationChannel(channel)
    }

    private fun startForegroundCompat(notification: Notification) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
    }

    private fun stopForegroundCompat() {
        @Suppress("DEPRECATION")
        stopForeground(true)
    }

    companion object {
        const val ACTION_INSTALL_FOLDER = "com.driftdroid.android.INSTALL_RETRO_REWIND_FOLDER"
        const val ACTION_INSTALL_ZIP = "com.driftdroid.android.INSTALL_RETRO_REWIND_ZIP"
        const val ACTION_INSTALL_DOWNLOAD = "com.driftdroid.android.INSTALL_RETRO_REWIND_DOWNLOAD"
        const val ACTION_INSTALL_LATEST = "com.driftdroid.android.INSTALL_RETRO_REWIND_LATEST"
        const val ACTION_UPDATE_LATEST = "com.driftdroid.android.UPDATE_RETRO_REWIND_LATEST"
        const val EXTRA_SOURCE_URI = "source_uri"
        const val EXTRA_ZIP_NAME = "zip_name"

        private const val NOTIFICATION_CHANNEL_ID = "retro_rewind_install"
        private const val NOTIFICATION_ID = 0x52520100
        private const val NOTIFICATION_UPDATE_INTERVAL_MS = 700L
        private const val SPACE_RESERVE_BYTES = 256L * 1024 * 1024
        private const val ZIP_EXTRACT_SPACE_FACTOR = 2.5
        // A downloaded archive is unpacked next to the install directory and moved into place, so
        // it needs the archive plus one unpacked copy (2.20GB against a 1.94GB archive for 6.13.0),
        // not the margin an archive on some other storage gets. At 2.5 a phone with 6GB free was
        // refused a 4.2GB job.
        private const val DOWNLOAD_EXTRACT_SPACE_FACTOR = 1.25
        private const val FOLDER_COPY_SPACE_FACTOR = 1.15
        private const val MAX_REDIRECTS = 5
        private const val MAX_FULL_ARCHIVE_PROBES = 4
        private const val MAX_FULL_ARCHIVE_BYTES = 8L * 1024 * 1024 * 1024
        private const val CONNECT_TIMEOUT_MS = 30_000
        private const val READ_TIMEOUT_MS = 30_000

        fun startFolderInstall(context: Context, sourceUri: Uri) {
            val intent = Intent(context, RetroRewindInstallService::class.java)
            intent.action = ACTION_INSTALL_FOLDER
            intent.putExtra(EXTRA_SOURCE_URI, sourceUri)
            context.startForegroundService(intent)
        }

        fun startZipInstall(context: Context, zipUri: Uri, name: String) {
            val intent = Intent(context, RetroRewindInstallService::class.java)
            intent.action = ACTION_INSTALL_ZIP
            intent.putExtra(EXTRA_SOURCE_URI, zipUri)
            intent.putExtra(EXTRA_ZIP_NAME, name)
            context.startForegroundService(intent)
        }

        fun startDownloadInstall(context: Context) {
            val intent = Intent(context, RetroRewindInstallService::class.java)
            intent.action = ACTION_INSTALL_DOWNLOAD
            context.startForegroundService(intent)
        }

        /** Fresh install of the newest release the live catalog offers that this build can run. */
        fun startLatestInstall(context: Context) {
            val intent = Intent(context, RetroRewindInstallService::class.java)
            intent.action = ACTION_INSTALL_LATEST
            context.startForegroundService(intent)
        }

        /** Upgrades an existing install in place by applying the small per-version delta archives,
         * the way the mod's own updater does - megabytes instead of another 1.9GB. */
        fun startDeltaUpdate(context: Context) {
            val intent = Intent(context, RetroRewindInstallService::class.java)
            intent.action = ACTION_UPDATE_LATEST
            context.startForegroundService(intent)
        }
    }
}

private fun Intent.getParcelableUriExtra(key: String): Uri? =
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        getParcelableExtra(key, Uri::class.java)
    } else {
        @Suppress("DEPRECATION")
        getParcelableExtra(key)
    }
