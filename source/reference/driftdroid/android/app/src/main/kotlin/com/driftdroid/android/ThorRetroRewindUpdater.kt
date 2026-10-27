package com.driftdroid.android

import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.InputStream
import java.io.RandomAccessFile
import java.net.HttpURLConnection
import java.net.URL
import java.nio.channels.FileChannel
import java.nio.file.StandardOpenOption.READ
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.StandardCopyOption.ATOMIC_MOVE
import java.security.MessageDigest
import java.util.Locale
import java.util.Properties
import java.util.UUID
import java.util.concurrent.CancellationException
import java.util.zip.CRC32
import java.util.zip.ZipFile

/**
 * Updates an installed pack, or installs the first one. Network/cache work never overwrites the
 * installed pack. Every delta is checked first, then applied to a sibling copy, verified, and
 * published by rename. Ghosts (apart from the official expert ghosts a release refreshes), MyStuff
 * and unknown user files are copied unchanged; NAND and
 * riivolution are outside this transaction. A completed update intentionally does not claim
 * compatibility for new code.
 *
 * First install (no pack yet): the newest official full archive on the newest code lineage this
 * build runs is downloaded, entry-checked (the pinned release also by size and SHA-256), its
 * RetroRewind6 tree unpacked into the stage, newer deltas overlaid, and published the same way.
 * Recovery rolls an unacknowledged first install back to "not installed".
 */
internal class ThorRetroRewindUpdater(
    private val root: File,
    private val cache: File,
    private val repository: Repository = OfficialRepository(),
    private val gameRetained: () -> Boolean,
    private val cancelled: () -> Boolean = { false },
    private val progress: (ThorRetroRewindUpdateStatus.Snapshot) -> Unit = {},
    private val supportedCode: (String?) -> Boolean = RetroRewindCatalog::isSupportedCodePul,
    private val move: (File, File) -> Unit = { a, b -> Files.move(a.toPath(), b.toPath(), ATOMIC_MOVE); Unit },
    private val reserveBytes: Long = 256L * 1024 * 1024,
    // Android injects Os.open(O_RDONLY)/fsync; a host can provide its own durability barrier.
    private val syncDirectory: (File) -> Unit = { folder ->
        FileChannel.open(folder.toPath(), READ).use { it.force(true) }
    },
    private val supportedLoaderXml: (String) -> Boolean = { it == LOADER_XML_SHA256 },
    /** Exact size and SHA-256 of a release's full archive, when known (the pinned release). */
    private val pinnedArchive: (String) -> Pair<Long, String>? = { version ->
        if (version == RetroRewindRelease.VERSION) RetroRewindRelease.ARCHIVE_BYTES to RetroRewindRelease.ARCHIVE_SHA256 else null
    },
    /** Code.pul SHA-256 a release is already known to set, without downloading anything. */
    private val knownCode: (String) -> String? = { RetroRewindRelease.KNOWN_CODE_PUL_SHA256[it] },
    /** A full archive smaller than this can't be a real pack (for example an error page). */
    private val minFullArchiveBytes: Long = 64L * 1024 * 1024,
) {
    interface Repository {
        fun releases(): List<RetroRewindCatalog.Release>
        fun inspect(release: RetroRewindCatalog.Release): RetroRewindCatalog.DeltaVerdict
        fun download(release: RetroRewindCatalog.Release, file: File, cancelled: () -> Boolean,
                     progress: (Long, Long) -> Unit)
        /** Byte size of [release]'s full archive, or -1 when that release has none. */
        fun fullArchiveSize(release: RetroRewindCatalog.Release): Long = -1
        fun downloadFull(release: RetroRewindCatalog.Release, file: File, cancelled: () -> Boolean,
                         progress: (Long, Long) -> Unit): Unit = throw IOException("No full Retro Rewind download is available.")
    }

    /** [base] is set for a first install: the full archive the [deltas] are applied on top of. */
    data class Plan(val installedVersion: String?, val codeSha256: String?,
                    val latestVersion: String, val deltas: List<RetroRewindCatalog.Release>,
                    val base: RetroRewindCatalog.Release? = null, val baseBytes: Long = -1) {
        val firstInstall: Boolean get() = base != null
        val targetVersion: String get() = deltas.lastOrNull()?.version ?: base?.version ?: installedVersion!!
    }
    /** [full]: a first install's full archive (its RetroRewind6 tree is the pack). */
    private data class PreparedArchive(val file: File, val hashes: Map<String, String>, val full: Boolean = false)
    private fun entryName(archive: PreparedArchive): (String, Boolean) -> String? =
        if (archive.full) ::fullEntry else ::relativeEntry

    private var snapshot = ThorRetroRewindUpdateStatus.Snapshot()
    /** A first install is being planned or run (wording only). */
    private var installing = false
    /** Recovery verifies to the end: a cancel there must never be read as a mismatch. */
    private var recovering = false
    private val parent = root.absoluteFile.parentFile ?: throw IOException("Missing pack directory")
    private val journal = File(parent, ".thor-rr-update.properties")
    private val receipt = File(parent, ".thor-rr-update.sha256")

    fun check(): Plan? {
        emit(ThorRetroRewindUpdateStatus.State.CHECKING, "Checking Retro Rewind releases…", running = true)
        if (journal.exists()) {
            emit(ThorRetroRewindUpdateStatus.State.BLOCKED,
                "An interrupted update needs recovery. Close the game, then recover the installation.")
            return null
        }
        installing = false
        val installed = RetroRewindCatalog.installedVersion(root)
        val code = RetroRewindCatalog.installedCodePulSha256(root)
        snapshot = snapshot.copy(currentVersion = installed)
        publish()
        if (installed == null && code == null && packAbsent()) return checkFirstInstall()
        if (installed != null) pruneFullArchives(null)
        if (installed == null || code == null) {
            emit(ThorRetroRewindUpdateStatus.State.BLOCKED,
                "The installed Retro Rewind pack is incomplete. Replace it with a complete copy from your PC.")
            return null
        }
        ensureRegularDirectory(root)
        val releases = fetchReleases()
        val latest = RetroRewindCatalog.newest(releases)!!
        snapshot = snapshot.copy(latestVersion = latest.version)
        publish()
        val chain = RetroRewindCatalog.deltaChain(releases, installed, latest.version)
        // What counts is the code each step leaves installed. A release whose code this build can't
        // run may be passed through when a later one in the same update replaces it with code it
        // can: an install from before this build's Retro Rewind (6.13.1 -> 6.13.3 -> 6.13.4) is
        // brought forward instead of refused. update() checks the staged pack's code again.
        var runnable = supportedCode(code)
        val compatible = mutableListOf<RetroRewindCatalog.Release>()
        val pending = mutableListOf<RetroRewindCatalog.Release>()
        for (group in chain.groupBy { normalizedVersion(it.version) }.values) {
            // A multipart version is all-or-nothing. Never mark half a release as installed.
            for (release in group) {
                stopIfCancelled()
                val verdict = repository.inspect(release)
                stopIfCancelled()
                when (verdict) {
                    is RetroRewindCatalog.DeltaVerdict.AssetsOnly -> Unit
                    is RetroRewindCatalog.DeltaVerdict.SupportedCodeChange -> runnable = true
                    is RetroRewindCatalog.DeltaVerdict.UnsupportedCodeChange -> runnable = false
                    is RetroRewindCatalog.DeltaVerdict.Failed -> throw IOException(verdict.error)
                }
            }
            pending += group
            if (runnable) { compatible += pending; pending.clear() }
        }
        val blocked = pending.isNotEmpty()
        snapshot = snapshot.copy(compatibleVersion = compatible.lastOrNull()?.version ?: installed)
        if (compatible.isEmpty()) {
            val needBuild = blocked || !supportedCode(code)
            emit(if (needBuild) ThorRetroRewindUpdateStatus.State.BLOCKED else ThorRetroRewindUpdateStatus.State.UP_TO_DATE,
                if (needBuild) NEED_BUILD else "Retro Rewind $installed is up to date.")
            return null
        }
        val plan = Plan(installed, code, latest.version, compatible.toList())
        snapshot = snapshot.copy(canUpdate = true)
        emit(ThorRetroRewindUpdateStatus.State.READY,
            "Retro Rewind ${plan.targetVersion} is ready to install." +
                if (blocked) " A newer release needs a compatible Thor build." else "")
        return plan
    }

    private fun fetchReleases(): List<RetroRewindCatalog.Release> {
        stopIfCancelled()
        val fetched = repository.releases()
        stopIfCancelled()
        require(fetched.isNotEmpty()) { "The Retro Rewind release list is empty. Try again." }
        require(fetched.all { VERSION.matches(it.version) && safeUrl(it.deltaUrl) }) { "Invalid release list" }
        // Upstream has legitimate multipart versions (for example 4.0.0 + its music ZIP).
        // Keep manifest order within each version; collapse only the same version/URL pair.
        return fetched.distinctBy { normalizedVersion(it.version) to it.deltaUrl }
    }

    /** Nothing installed: no pack folder, or one holding no files at all. */
    private fun packAbsent(): Boolean =
        !root.exists() && !Files.isSymbolicLink(root.toPath()) || (Files.isDirectory(root.toPath(), NOFOLLOW_LINKS) && treeFiles(root).isEmpty())

    /**
     * First install. Anchored, when possible, on the newest release whose game code this build is
     * known to run without downloading anything ([knownCode]): the releases above it are judged
     * oldest first, as an update from it would be, up to the first one whose code this build
     * can't run. Without such an anchor the newest releases are judged newest first, as
     * RetroRewindCatalog.newestSupported does. Multipart versions are judged whole.
     * The base is the newest full archive at or above the release that set the code. A release
     * with a pinned archive (known size and SHA-256) is always a candidate; a newer one the server
     * doesn't offer (or answers oddly about) is skipped rather than failing the check.
     */
    private fun checkFirstInstall(): Plan? {
        installing = true
        val releases = fetchReleases()
        val latest = RetroRewindCatalog.newest(releases)!!
        snapshot = snapshot.copy(latestVersion = latest.version)
        publish()
        val versions = releases.groupBy { normalizedVersion(it.version) }.values
            .sortedWith { a, b -> RetroRewindCatalog.compareVersions(a.first().version, b.first().version) }
        fun judge(group: List<RetroRewindCatalog.Release>): List<RetroRewindCatalog.DeltaVerdict> {
            val verdicts = group.map { release ->
                stopIfCancelled()
                repository.inspect(release).also { stopIfCancelled() }
            }
            verdicts.filterIsInstance<RetroRewindCatalog.DeltaVerdict.Failed>().firstOrNull()?.let { throw IOException(it.error) }
            return verdicts
        }
        var goal: String? = null
        var lineage: String? = null
        val anchor = versions.lastOrNull { group -> supportedCode(knownCode(group.first().version)) }
        if (anchor != null) {
            val from = anchor.first().version
            lineage = from
            goal = from
            for (group in versions.filter { RetroRewindCatalog.compareVersions(it.first().version, from) > 0 }) {
                if (judge(group).any { it is RetroRewindCatalog.DeltaVerdict.UnsupportedCodeChange }) break
                goal = group.first().version
            }
        } else {
            var candidate: String? = null   // newest assets-only version above the code being judged
            for (group in versions.asReversed().take(MAX_SUPPORT_PROBES)) {
                val verdicts = judge(group)
                val version = group.first().version
                when {
                    verdicts.any { it is RetroRewindCatalog.DeltaVerdict.UnsupportedCodeChange } -> candidate = null
                    verdicts.any { it is RetroRewindCatalog.DeltaVerdict.SupportedCodeChange } -> {
                        goal = candidate ?: version; lineage = version
                    }
                    else -> if (candidate == null) candidate = version
                }
                if (goal != null) break
            }
        }
        val target = goal
        val codeVersion = lineage
        if (target == null || codeVersion == null) {
            emit(ThorRetroRewindUpdateStatus.State.BLOCKED, NEED_BUILD_FIRST)
            return null
        }
        snapshot = snapshot.copy(compatibleVersion = target)
        val bases = releases.filter {
            RetroRewindCatalog.compareVersions(it.version, codeVersion) >= 0 && RetroRewindCatalog.compareVersions(it.version, target) <= 0
        }.distinctBy { normalizedVersion(it.version) }.sortedWith { a, b -> RetroRewindCatalog.compareVersions(a.version, b.version) }.asReversed()
        var base: RetroRewindCatalog.Release? = null
        var baseBytes = -1L
        var probes = 0
        for (release in bases) {
            stopIfCancelled()
            val pinned = pinnedArchive(release.version)
            val bytes = when {
                pinned != null -> pinned.first
                probes++ < MAX_FULL_PROBES -> try {
                    repository.fullArchiveSize(release)
                } catch (cancel: CancellationException) {
                    throw cancel
                } catch (_: Exception) {
                    -1L
                }
                else -> -1L
            }
            if (bytes in minFullArchiveBytes..MAX_DOWNLOAD) { base = release; baseBytes = bytes; break }
        }
        val full = base
        if (full == null) {
            emit(ThorRetroRewindUpdateStatus.State.BLOCKED,
                "Couldn't find a full download of Retro Rewind $target. Check your connection and try again later.")
            return null
        }
        val plan = Plan(null, null, latest.version, RetroRewindCatalog.deltaChain(releases, full.version, target), full, baseBytes)
        snapshot = snapshot.copy(canUpdate = true, totalBytes = baseBytes)
        emit(ThorRetroRewindUpdateStatus.State.READY,
            String.format(Locale.ROOT, "Retro Rewind %s can be installed: a %.1f GB download from Retro Rewind's official server.",
                plan.targetVersion, baseBytes / 1e9) +
                if (RetroRewindCatalog.compareVersions(latest.version, plan.targetVersion) > 0) " A newer release needs a compatible Thor build." else "")
        return plan
    }

    /** Fresh compatibility check on each authorized attempt; no UI-supplied URL or stale plan. */
    fun update() {
        idleRequired()
        ensureRegularDirectory(parent)
        val lockFile = File(parent, ".thor-rr-update.lock")
        require(!Files.isSymbolicLink(lockFile.toPath())) { "Unsafe update lock" }
        RandomAccessFile(lockFile, "rw").channel.use { channel ->
            val lock = channel.tryLock() ?: throw IOException("Another Retro Rewind update is running.")
            lock.use {
                recover()
                val plan = check() ?: return
                idleRequired()
                snapshot = snapshot.copy(canUpdate = false)
                if (!cache.isDirectory && !cache.mkdirs()) throw IOException("Cannot create update cache")
                ensureRegularDirectory(cache)
                val first = plan.firstInstall
                // First install: the full archive comes first; its newer deltas then overlay it.
                val baseArchive = plan.base?.let { release ->
                    stopIfCancelled()
                    val urlKey = sha256(release.fullArchiveUrl.toByteArray()).take(16)
                    val file = File(cache, "${release.version}-full-$urlKey.zip")
                    pruneFullArchives(file)
                    val have = if (file.isFile) file.length() else File(file.path + ".part").length()
                    val needed = (plan.baseBytes - have).coerceAtLeast(0) + plan.baseBytes / 4 * 5 + reserveBytes
                    require(cache.usableSpace >= needed) {
                        String.format(Locale.ROOT, "Retro Rewind needs about %.1f GB of free space to install. Free some space and try again.", needed / 1e9)
                    }
                    emit(ThorRetroRewindUpdateStatus.State.DOWNLOADING,
                        "Downloading Retro Rewind ${release.version}…", running = true)
                    repository.downloadFull(release, file, cancelled) { done, total ->
                        snapshot = snapshot.copy(downloadedBytes = done, totalBytes = total)
                        publish()
                    }
                    emit(ThorRetroRewindUpdateStatus.State.VERIFYING,
                        "Verifying Retro Rewind ${release.version}…", running = true)
                    val hashes = try { validateFullArchive(file, release) } catch (failure: Throwable) {
                        if (failure !is CancellationException) file.delete()
                        throw failure
                    }
                    PreparedArchive(file, hashes, full = true)
                }
                val archives = plan.deltas.map { release ->
                    stopIfCancelled()
                    val urlKey = sha256(release.deltaUrl.toByteArray()).take(16)
                    val file = File(cache, "${release.version}-$urlKey.zip")
                    emit(ThorRetroRewindUpdateStatus.State.DOWNLOADING,
                        "Downloading Retro Rewind ${release.version}…", running = true)
                    repository.download(release, file, cancelled) { done, total ->
                        snapshot = snapshot.copy(downloadedBytes = done, totalBytes = total)
                        publish()
                    }
                    emit(ThorRetroRewindUpdateStatus.State.VERIFYING,
                        "Verifying Retro Rewind ${release.version}…", running = true)
                    val hashes = try { validateArchive(file, ::relativeEntry) } catch (failure: Throwable) {
                        // A corrupt completed ZIP must not poison every Retry. Interrupted partials
                        // are separate files and remain available for a validated HTTP resume.
                        if (failure !is CancellationException) file.delete()
                        throw failure
                    }
                    PreparedArchive(file, hashes)
                }
                idleRequired()
                if (first) check(packAbsent()) { "Retro Rewind was installed meanwhile. Check for updates again." }
                else check(RetroRewindCatalog.installedVersion(root) == plan.installedVersion &&
                    RetroRewindCatalog.installedCodePulSha256(root) == plan.codeSha256) {
                    "The installed pack changed. Check for updates again."
                }
                val installedBytes = if (first) 0L else treeFiles(root).sumOf { it.length() }
                val expanded = (listOfNotNull(baseArchive) + archives).sumOf { archive -> ZipFile(archive.file).use { zip ->
                    zip.entries().asSequence().filter { !it.isDirectory && entryName(archive)(it.name, false) != null }
                        .sumOf { it.size.coerceAtLeast(0) }
                } }
                require(parent.usableSpace >= installedBytes + expanded + reserveBytes) {
                    if (first) "There is not enough free space to install Retro Rewind." else "There is not enough free space to safely stage this update."
                }
                val token = UUID.randomUUID().toString()
                val stage = File(parent, ".thor-rr-stage-$token")
                val backup = File(parent, ".thor-rr-backup-$token")
                require(!stage.exists() && !backup.exists()) { "Update staging directory already exists" }
                writeJournal(token, "COPYING", first)
                try {
                    emit(ThorRetroRewindUpdateStatus.State.STAGING,
                        if (first) "Installing Retro Rewind…" else "Preparing the update…", running = true)
                    require(stage.mkdir()) { "Cannot create staging directory" }
                    if (baseArchive != null) {
                        extractInto(baseArchive, stage)
                    } else {
                        // Empty personal directories matter too (for example a fresh MyStuff folder).
                        root.walkTopDown().filter { it.isDirectory }.forEach { folder ->
                            ensureRegularDirectory(folder)
                            require(File(stage, folder.relativeTo(root).path).let { it.isDirectory || it.mkdirs() }) {
                                "Cannot copy the installed directory structure"
                            }
                        }
                        for (file in treeFiles(root)) {
                            stopIfCancelled()
                            val target = File(stage, file.relativeTo(root).path)
                            require(target.parentFile!!.let { it.isDirectory || it.mkdirs() }) { "Cannot create staged folder" }
                            copyAndSync(file.inputStream(), target)
                            fileDone()
                        }
                    }
                    for (archive in archives) extractInto(archive, stage)
                    AtomicConfigFile.writeText(File(stage, "version.txt"), plan.targetVersion + "\n")
                    require(supportedCode(RetroRewindCatalog.installedCodePulSha256(stage))) { needBuild() }
                    // File fsync does not persist newly-created directory entries. Flush deepest
                    // directories first, including empty personal folders, before PREPARED is durable.
                    syncTreeDirectories(stage)
                    syncDirectory(parent)
                    // Verify the staged bytes again after writing and before touching the live name.
                    writeReceipt(stage)
                    require(verifyReceipt(stage)) { "Staged update verification failed" }
                    idleRequired()
                    stopIfCancelled()
                    writeJournal(token, "PREPARED", first)
                    emit(ThorRetroRewindUpdateStatus.State.COMMITTING,
                        if (first) "Finishing the installation…" else "Finishing the update…", running = true)
                    // Cancellation is deferred across these renames; recovery handles process death.
                    if (first) {
                        // Only empty folders may be in the way; any file that appeared since the
                        // check makes this fail with the live name untouched.
                        removeEmptyTree(root)
                        syncDirectory(parent)
                    } else {
                        move(root, backup)
                        syncDirectory(parent)
                        writeJournal(token, "SAVED")
                    }
                    move(stage, root)
                    syncDirectory(parent)
                    writeJournal(token, "COMMITTED", first)
                    require(verifyReceipt(root)) { "Installed update verification failed" }
                    writeJournal(token, "COMPLETE", first)
                    // The verified live copy contains every protected file. Cleanup failure is safe;
                    // the journal remains until a later attempt can finish cleanup.
                    cleanupCompleted(stage, backup)
                    // The full download (about 2 GB) is not needed once its pack is live.
                    if (first) pruneFullArchives(null)
                    snapshot = snapshot.copy(currentVersion = plan.targetVersion, compatibleVersion = plan.targetVersion)
                    emit(ThorRetroRewindUpdateStatus.State.COMPLETE,
                        if (first) "Retro Rewind ${plan.targetVersion} is installed."
                        else "Retro Rewind ${plan.targetVersion} is installed. Your saves and custom files are preserved.")
                } catch (failure: Throwable) {
                    try { recover() } catch (recovery: Throwable) { failure.addSuppressed(recovery) }
                    throw failure
                }
            }
        }
    }

    fun recoverPending() {
        idleRequired()
        ensureRegularDirectory(parent)
        val lockFile = File(parent, ".thor-rr-update.lock")
        require(!Files.isSymbolicLink(lockFile.toPath())) { "Unsafe update lock" }
        RandomAccessFile(lockFile, "rw").channel.use { channel ->
            val lock = channel.tryLock() ?: throw IOException("Another Retro Rewind update is running.")
            lock.use { recover() }
        }
        val installed = RetroRewindCatalog.installedVersion(root)
        snapshot = snapshot.copy(currentVersion = installed)
        if (installed == null) emit(ThorRetroRewindUpdateStatus.State.CANCELLED,
            "The interrupted installation was undone. Install again to finish; the download is kept.")
        else emit(ThorRetroRewindUpdateStatus.State.COMPLETE, "The installation is ready. You can check for updates or return to the game.")
    }

    /** Can be called only with the game stopped and the single-writer file lock held. */
    private fun recover() {
        idleRequired()
        if (!journal.exists()) return
        recovering = true
        try { recoverJournal() } finally { recovering = false }
    }

    private fun recoverJournal() {
        regularFile(journal)
        val fields = Properties().apply { journal.inputStream().use { load(it) } }
        val token = fields.getProperty("token") ?: throw IOException("Incomplete update journal")
        require(Regex("[a-f0-9-]{36}").matches(token)) { "Invalid update journal" }
        val stage = File(parent, ".thor-rr-stage-$token")
        val backup = File(parent, ".thor-rr-backup-$token")
        listOf(stage, backup).filter { it.exists() || Files.isSymbolicLink(it.toPath()) }.forEach(::ensureRegularDirectory)
        val phase = fields.getProperty("phase")
        if (fields.getProperty("first") == "true") {
            // Nothing was installed before this transaction, so rolling back means "not installed".
            if (phase == "COMPLETE") {
                // Only a definite mismatch counts (a missing or changed file). A read error
                // propagates and keeps the journal and the pack for a later attempt.
                val verified = try { verifyReceipt(root) } catch (_: IllegalArgumentException) { false }
                if (verified) {
                    cleanupCompleted(stage, backup)
                    pruneFullArchives(null)
                    return
                }
            }
            // From PREPARED on, a missing stage means it was (or was about to be) published as the
            // pack: remove it. Before that, the live name was never touched.
            if (phase in setOf("PREPARED", "COMMITTED", "COMPLETE") && !stage.exists() &&
                (root.exists() || Files.isSymbolicLink(root.toPath()))) safeDeleteTree(root)
            safeDeleteTree(stage)
            syncDirectory(parent)
            clearJournalBeforeReceipt()
            return
        }
        if (phase == "COMPLETE") {
            // A durable COMPLETE journal alone is not proof that the published tree survived.
            // Never discard the previous pack until the actual installed bytes match the receipt.
            if (runCatching { verifyReceipt(root) }.getOrDefault(false)) {
                cleanupCompleted(stage, backup)
                return
            }
            require(backup.isDirectory) { "Cannot verify the completed update; keep the recovery files and restore the pack from your PC." }
        }
        if (backup.exists()) {
            // Even a fully renamed new tree is rolled back after an unacknowledged commit. Never
            // guess success from Code.pul alone or leave a mixed live tree after an exception.
            if (root.exists()) {
                ensureRegularDirectory(root)
                require(!stage.exists()) { "Ambiguous update state; previous pack retained at ${backup.name}" }
                move(root, stage)
                syncDirectory(parent)
            }
            move(backup, root)
            syncDirectory(parent)
        }
        require(root.isDirectory) { "The previous pack could not be restored" }
        safeDeleteTree(stage)
        syncDirectory(parent)
        clearJournalBeforeReceipt()
    }

    private fun cleanupCompleted(stage: File, backup: File) {
        safeDeleteTree(stage)
        safeDeleteTree(backup)
        syncDirectory(parent)
        clearJournalBeforeReceipt()
    }

    private fun clearJournalBeforeReceipt() {
        // The verification receipt must outlive the recovery journal. If power fails during
        // cleanup, recovery can still verify COMPLETE; an orphan receipt is harmless.
        require(journal.delete() || !journal.exists()) { "Update installed; cleanup needs a retry" }
        syncDirectory(parent)
        require(receipt.delete() || !receipt.exists()) { "Cannot clean up update verification receipt" }
        syncDirectory(parent)
    }

    private fun writeJournal(token: String, phase: String, first: Boolean = false) {
        AtomicConfigFile.writeText(journal, "token=$token\nphase=$phase\n" + if (first) "first=true\n" else "")
        syncDirectory(parent)
    }

    private fun syncTreeDirectories(directory: File) {
        directory.walkBottomUp().filter { it.isDirectory }.forEach { folder ->
            ensureRegularDirectory(folder)
            syncDirectory(folder)
        }
    }

    /** First install: the pinned release must match byte for byte; every used entry is checked. */
    private fun validateFullArchive(file: File, release: RetroRewindCatalog.Release): Map<String, String> {
        regularFile(file)
        pinnedArchive(release.version)?.let { (bytes, sha) ->
            require(file.length() == bytes && hashFile(file) == sha) {
                "The download didn't match the official Retro Rewind ${release.version}. Try again."
            }
        }
        val hashes = validateArchive(file, ::fullEntry)
        require(hashes.containsKey("Binaries/Code.pul")) { "The download has no Retro Rewind game code." }
        val version = ZipFile(file).use { zip ->
            zip.getEntry("RetroRewind6/version.txt")?.let { entry -> zip.getInputStream(entry).use { it.readBytes() }.decodeToString().trim() }
        }
        require(version != null && VERSION.matches(version) && normalizedVersion(version) == normalizedVersion(release.version)) {
            "The download isn't Retro Rewind ${release.version}. Try again later."
        }
        return hashes
    }

    private fun validateArchive(file: File, entryName: (String, Boolean) -> String?): Map<String, String> {
        regularFile(file)
        require(file.length() in 1..MAX_DOWNLOAD) { "Invalid update archive size" }
        var bytes = 0L
        val names = hashSetOf<String>()
        val hashes = mutableMapOf<String, String>()
        ZipFile(file).use { zip ->
            val entries = zip.entries().asSequence().toList()
            require(entries.size <= MAX_FILES) { "Too many update files" }
            for (entry in entries) {
                stopIfCancelled()
                val name = entryName(entry.name, entry.isDirectory) ?: continue
                if (name.isEmpty()) continue
                require(names.add(name.lowercase())) { "Duplicate update path: $name" }
                if (entry.isDirectory) continue
                require(entry.size in 0..MAX_EXPANDED) { "Invalid update entry size" }
                bytes += entry.size
                require(bytes <= MAX_EXPANDED) { "Update expands beyond the safety limit" }
                val crc = CRC32()
                var count = 0L
                val digest = MessageDigest.getInstance("SHA-256")
                zip.getInputStream(entry).use { input ->
                    val buffer = ByteArray(128 * 1024)
                    while (true) {
                        stopIfCancelled()
                        val n = input.read(buffer)
                        if (n < 0) break
                        count += n
                        require(count <= entry.size) { "Update entry exceeded its declared size" }
                        crc.update(buffer, 0, n)
                        digest.update(buffer, 0, n)
                    }
                }
                require(count == entry.size && crc.value == entry.crc) { "Corrupt update entry: $name" }
                val hash = hex(digest.digest())
                hashes[name] = hash
                // Official deltas also carry the unchanged external loader XML. Validate its
                // complete bytes, but never extract it or write anything outside the pack.
                if (name == LOADER_XML) require(supportedLoaderXml(hash)) { needBuild() }
                // An intermediate release may carry code this build can't run when a later one in
                // the same update replaces it; update() requires the staged pack's code to be
                // supported before anything is published.
                if (name.equals("Binaries/Code.pul", true)) {
                    require(name == "Binaries/Code.pul") { "Ambiguous code path in update" }
                }
            }
        }
        return hashes
    }

    private fun relativeEntry(raw: String, directory: Boolean): String {
        if (raw == "riivolution/" && directory) return ""
        if (raw == LOADER_XML && !directory) return LOADER_XML
        require(raw.length <= 1024 && '\\' !in raw && ':' !in raw && !raw.startsWith('/') &&
            raw.none { it.code < 32 || it.code == 127 }) { "Unsafe update path" }
        val path = raw.removeSuffix("/")
        val parts = path.split('/')
        require(parts.none { it.isEmpty() || it == "." || it == ".." }) { "Unsafe update path" }
        val relative = if (parts.first() == "RetroRewind6") parts.drop(1) else parts
        if (relative.isEmpty()) { require(directory); return "" }
        val first = relative.first()
        // No update archive may write personal content or arbitrary application data. Official
        // updates (6.13.3) do refresh the expert staff ghosts the full archive seeds; only those
        // folders and their track-name index are accepted under Ghosts.
        val officialGhosts = first == "Ghosts" && (relative.size == 1 && directory ||
            relative.size >= 2 && relative[1] in EXPERT_GHOST_FOLDERS ||
            relative.size == 2 && !directory && GHOST_TRACK_INDEX.matches(relative[1]))
        require(first in STATIC_ROOTS || officialGhosts || relative == listOf("version.txt")) {
            "The update contains a protected or unrecognized folder: $first"
        }
        return relative.joinToString("/")
    }

    /**
     * A full archive also carries the Wii's own installer files (apps/, the WADs,
     * RetroRewindChannel/); only its RetroRewind6 tree (the pack, with its seed Ghosts and
     * MyStuff) and the loader XML (checked, never extracted) are used. Null: not used, not read.
     */
    private fun fullEntry(raw: String, directory: Boolean): String? {
        if (raw == LOADER_XML && !directory) return LOADER_XML
        if (!raw.startsWith("RetroRewind6/")) return null
        require(raw.length <= 1024 && '\\' !in raw && ':' !in raw &&
            raw.none { it.code < 32 || it.code == 127 }) { "Unsafe path in the download" }
        val parts = raw.removeSuffix("/").split('/')
        require(parts.none { it.isEmpty() || it == "." || it == ".." }) { "Unsafe path in the download" }
        val relative = parts.drop(1)
        if (relative.isEmpty()) { require(directory); return "" }
        require(relative.first() in STATIC_ROOTS || relative.first() in SEED_ROOTS || relative == listOf("version.txt")) {
            "The download contains an unrecognized folder: ${relative.first()}"
        }
        return relative.joinToString("/")
    }

    /** Writes [archive]'s used entries into [stage], each checked against its verified hash. */
    private fun extractInto(archive: PreparedArchive, stage: File) = ZipFile(archive.file).use { zip ->
        val entryName = entryName(archive)
        zip.entries().asSequence().forEach { entry ->
            stopIfCancelled()
            val name = entryName(entry.name, entry.isDirectory) ?: return@forEach
            if (name.isEmpty() || name == "version.txt" || name == LOADER_XML) return@forEach
            val target = File(stage, name)
            require(target.canonicalPath.startsWith(stage.canonicalPath + File.separator)) { "Unsafe staged path" }
            // Case aliases could replace code or user files on another filesystem. Fail closed.
            var directory = stage
            val parts = name.split('/')
            parts.forEachIndexed { index, part ->
                directory.listFiles()?.firstOrNull { it.name.equals(part, true) && it.name != part }?.let {
                    throw IOException("Case-conflicting update path: $name")
                }
                val next = File(directory, part)
                require(!Files.isSymbolicLink(next.toPath())) { "Symlink in staged path" }
                if (index < parts.lastIndex || entry.isDirectory) {
                    require((next.isDirectory || next.mkdir()) && !next.isFile) { "Conflicting update path: $name" }
                }
                directory = next
            }
            if (!entry.isDirectory) {
                val copiedHash = copyAndSync(zip.getInputStream(entry), target)
                require(copiedHash == archive.hashes[name]) { "Staged update did not match its verified archive" }
                fileDone()
            }
        }
    }

    private fun treeFiles(directory: File): List<File> {
        ensureRegularDirectory(directory)
        val result = mutableListOf<File>()
        fun visit(folder: File) {
            val children = folder.listFiles() ?: throw IOException("Cannot read ${folder.name}")
            for (file in children) {
                require(!Files.isSymbolicLink(file.toPath())) { "Symlink in installed pack: ${file.name}" }
                if (file.isDirectory) visit(file) else { regularFile(file); result += file }
            }
        }
        visit(directory)
        return result
    }

    private fun writeReceipt(stage: File) {
        val body = treeFiles(stage).sortedBy { it.relativeTo(stage).path }.joinToString("\n", postfix = "\n") {
            val relative = it.relativeTo(stage).invariantSeparatorsPath
            require(relative.none { c -> c == '\n' || c == '\r' || c == '\t' }) { "Unsupported local filename" }
            "${hashFile(it)}\t${it.length()}\t$relative"
        }
        AtomicConfigFile.writeText(receipt, body)
        syncDirectory(parent)
    }

    private fun verifyReceipt(directory: File): Boolean {
        regularFile(receipt)
        val lines = receipt.readLines()
        if (lines.isEmpty()) return false
        val expected = hashSetOf<String>()
        for (line in lines) {
            stopIfCancelledDuringVerification()
            val fields = line.split('\t', limit = 3)
            if (fields.size != 3 || !Regex("[a-f0-9]{64}").matches(fields[0])) return false
            val relative = fields[2]
            if (relative.startsWith('/') || '\\' in relative || relative.split('/').any { it == ".." || it == "." || it.isEmpty() }) return false
            val file = File(directory, relative)
            regularFile(file)
            if (file.length() != fields[1].toLongOrNull() || hashFile(file) != fields[0]) return false
            expected += relative
        }
        return treeFiles(directory).map { it.relativeTo(directory).invariantSeparatorsPath }.toSet() == expected
    }

    private fun copyAndSync(input: InputStream, target: File): String {
        require(!Files.isSymbolicLink(target.toPath())) { "Unsafe output path" }
        val digest = MessageDigest.getInstance("SHA-256")
        input.use { source -> FileOutputStream(target).use { output ->
            val buffer = ByteArray(128 * 1024)
            while (true) {
                stopIfCancelled()
                val n = source.read(buffer)
                if (n < 0) break
                output.write(buffer, 0, n)
                digest.update(buffer, 0, n)
            }
            output.fd.sync()
        } }
        val hash = hex(digest.digest())
        require(hashFile(target) == hash) { "Staged file verification failed" }
        return hash
    }

    private fun safeDeleteTree(directory: File) {
        if (!directory.exists()) return
        ensureRegularDirectory(directory)
        treeFiles(directory) // reject symlinks before deleting anything
        require(directory.deleteRecursively()) { "Cannot clean up ${directory.name}" }
    }

    private fun idleRequired() { check(!gameRetained()) { "Close the game before installing an update." } }
    private fun stopIfCancelled() {
        if (cancelled()) throw CancellationException(
            if (installing) "Installation paused. The download is kept; install again to continue."
            else "Update cancelled. Your installed pack is unchanged.")
    }
    private fun stopIfCancelledDuringVerification() {
        if (!recovering && snapshot.state != ThorRetroRewindUpdateStatus.State.COMMITTING) stopIfCancelled()
    }
    private fun needBuild() = if (installing) NEED_BUILD_FIRST else NEED_BUILD

    /** Full downloads other than [keep] (with their resume files) are dead weight. */
    private fun pruneFullArchives(keep: File?) {
        cache.listFiles()?.filter { it.isFile && "-full-" in it.name && (keep == null || !it.name.startsWith(keep.name)) }
            ?.forEach { it.delete() }
    }

    /** Removes a folder holding only empty folders; any file (or link) fails closed, left in place. */
    private fun removeEmptyTree(directory: File) {
        if (!directory.exists() && !Files.isSymbolicLink(directory.toPath())) return
        ensureRegularDirectory(directory)
        directory.walkBottomUp().forEach {
            require(Files.isDirectory(it.toPath(), NOFOLLOW_LINKS) && it.delete()) {
                "Retro Rewind was installed meanwhile. Check for updates again."
            }
        }
    }
    private fun fileDone() { snapshot = snapshot.copy(filesDone = snapshot.filesDone + 1); publish() }
    private fun publish() { progress(snapshot) }
    private fun emit(state: ThorRetroRewindUpdateStatus.State, message: String, running: Boolean = false) {
        snapshot = snapshot.copy(state = state, message = message, running = running)
        publish()
    }

    class OfficialRepository : Repository {
        override fun releases() = RetroRewindCatalog.fetchReleases().getOrThrow()
        override fun inspect(release: RetroRewindCatalog.Release) = RetroRewindCatalog.inspectDelta(release)
        override fun download(release: RetroRewindCatalog.Release, file: File,
                              cancelled: () -> Boolean, progress: (Long, Long) -> Unit) =
            downloadOfficial(release.deltaUrl, file, cancelled, progress)
        // The pinned release's size is known; others are asked for (not every release has one).
        override fun fullArchiveSize(release: RetroRewindCatalog.Release): Long =
            if (release.version == RetroRewindRelease.VERSION) RetroRewindRelease.ARCHIVE_BYTES
            else contentLengthOfficial(release.fullArchiveUrl)
        override fun downloadFull(release: RetroRewindCatalog.Release, file: File,
                                  cancelled: () -> Boolean, progress: (Long, Long) -> Unit) =
            downloadOfficial(release.fullArchiveUrl, file, cancelled, progress)
    }

    companion object {
        fun needsRecovery(root: File): Boolean = File(root.absoluteFile.parentFile, ".thor-rr-update.properties").exists()
        const val NEED_BUILD = "This release needs a compatible Thor build. Your current installation is unchanged."
        const val NEED_BUILD_FIRST = "This Thor build can't run the current Retro Rewind release yet."
        private const val LOADER_XML = "riivolution/RetroRewind6.xml"
        // Identical in the user's verified 6.13.0 pack and official 6.13.1 delta; changing the
        // loader contract needs a compatible build even when Code.pul happens to be unchanged.
        private const val LOADER_XML_SHA256 = "8759d8d9e14406de3bd5a4981a30a58826c9b952cfc856a997a267b3896e3a41"
        private val VERSION = Regex("[0-9]+(?:\\.[0-9]+){1,3}")
        private val STATIC_ROOTS = setOf("Assets", "Binaries", "BT", "Character", "CT", "Language", "Patches", "strm", "Tracks", "UI", "xml")
        /** Personal folders a full archive seeds (expert ghosts, an empty MyStuff); updates never write them. */
        private val SEED_ROOTS = setOf("Ghosts", "MyStuff")
        /** Official expert ghosts inside Ghosts that releases replace; player ghosts live elsewhere. */
        private val EXPERT_GHOST_FOLDERS = setOf("ExpertsCT", "ExpertsRT")
        private val GHOST_TRACK_INDEX = Regex("FolderToTrackName(CT|RT)\\.txt")
        /** Newest versions judged when looking for a runnable code lineage, and full archives tried. */
        private const val MAX_SUPPORT_PROBES = 8
        private const val MAX_FULL_PROBES = 4
        private const val MAX_DOWNLOAD = 4L * 1024 * 1024 * 1024
        private const val MAX_EXPANDED = 12L * 1024 * 1024 * 1024
        private const val MAX_FILES = 30000
        private fun normalizedVersion(version: String) = version.split('.').map { it.toInt() }.dropLastWhile { it == 0 }
        private fun ensureRegularDirectory(file: File) {
            require(Files.isDirectory(file.toPath(), NOFOLLOW_LINKS) && !Files.isSymbolicLink(file.toPath())) { "Unsafe directory: ${file.name}" }
        }
        private fun regularFile(file: File) {
            require(Files.isRegularFile(file.toPath(), NOFOLLOW_LINKS) && !Files.isSymbolicLink(file.toPath())) { "Unsafe file: ${file.name}" }
        }
        private fun hex(bytes: ByteArray) = bytes.joinToString("") { "%02x".format(it) }
        private fun sha256(bytes: ByteArray) = hex(MessageDigest.getInstance("SHA-256").digest(bytes))
        private fun hashFile(file: File): String {
            val digest = MessageDigest.getInstance("SHA-256")
            file.inputStream().use { input ->
                val buffer = ByteArray(128 * 1024)
                while (true) { val n = input.read(buffer); if (n < 0) break; digest.update(buffer, 0, n) }
            }
            return hex(digest.digest())
        }
        private fun safeUrl(url: String): Boolean = try {
            val parsed = URL(url)
            RetroRewindCatalog.isAllowedUrl(url) && parsed.userInfo == null && parsed.port in setOf(-1, 443) && parsed.ref == null
        } catch (_: Exception) { false }

        /** Size of an official file, or -1 when it doesn't exist (404/410); other failures throw. */
        internal fun contentLengthOfficial(url: String,
                                           openConnection: (URL) -> HttpURLConnection = { it.openConnection() as HttpURLConnection }): Long {
            var current = url
            for (hop in 0..5) {
                require(safeUrl(current)) { "Unofficial download URL" }
                val connection = openConnection(URL(current)).apply {
                    instanceFollowRedirects = false
                    requestMethod = "HEAD"
                    connectTimeout = 30000
                    readTimeout = 30000
                    setRequestProperty("Accept-Encoding", "identity")
                }
                try {
                    val code = connection.responseCode
                    if (code in 300..399) {
                        current = URL(URL(current), connection.getHeaderField("Location") ?: throw IOException("Missing download redirect")).toString()
                        continue
                    }
                    if (code == 404 || code == 410) return -1
                    require(code == 200) { "The Retro Rewind server returned HTTP $code. Try again." }
                    return connection.contentLengthLong
                } finally { connection.disconnect() }
            }
            throw IOException("Too many download redirects")
        }

        /** HTTP validator + exact Content-Range are required to append a cached partial. */
        internal fun downloadOfficial(url: String, file: File, cancelled: () -> Boolean,
                                      progress: (Long, Long) -> Unit,
                                      openConnection: (URL) -> HttpURLConnection = { it.openConnection() as HttpURLConnection }) {
            require(safeUrl(url)) { "Unofficial update URL" }
            val part = File(file.path + ".part")
            val meta = File(file.path + ".resume")
            listOf(file, part, meta).forEach { require(!Files.isSymbolicLink(it.toPath())) { "Unsafe download cache" } }
            // Completed files are still fully ZIP/Code.pul checked on every attempt.
            if (file.isFile && file.length() in 1..MAX_DOWNLOAD) { progress(file.length(), file.length()); return }
            val saved = Properties()
            if (meta.isFile) meta.inputStream().use { saved.load(it) }
            var offset = if (saved.getProperty("url") == url && !saved.getProperty("validator").isNullOrEmpty()) part.length() else 0L
            require(offset <= MAX_DOWNLOAD) { "Cached download is too large" }
            var current = url
            for (hop in 0..5) {
                require(safeUrl(current)) { "Unofficial update redirect" }
                if (cancelled()) throw CancellationException("Update cancelled")
                val connection = openConnection(URL(current)).apply {
                    instanceFollowRedirects = false
                    connectTimeout = 30000
                    readTimeout = 30000
                    setRequestProperty("Accept-Encoding", "identity")
                    if (offset > 0) {
                        setRequestProperty("Range", "bytes=$offset-")
                        setRequestProperty("If-Range", saved.getProperty("validator"))
                    }
                }
                try {
                    val code = connection.responseCode
                    if (code in 300..399) {
                        current = URL(URL(current), connection.getHeaderField("Location") ?: throw IOException("Missing update redirect")).toString()
                        continue
                    }
                    if (code == 416) {
                        part.delete()
                        meta.delete()
                        throw IOException("The cached range expired. Retry to download a fresh copy.")
                    }
                    require(code == 200 || code == 206) { "Update download returned HTTP $code. Retry to resume." }
                    var total = connection.contentLengthLong
                    if (code == 206) {
                        val match = Regex("bytes ([0-9]+)-([0-9]+)/([0-9]+)").matchEntire(connection.getHeaderField("Content-Range") ?: "")
                            ?: throw IOException("Invalid update range response")
                        val start = match.groupValues[1].toLong()
                        val end = match.groupValues[2].toLong()
                        total = match.groupValues[3].toLong()
                        if (!(offset > 0 && start == offset && end >= start && end < total &&
                            (connection.contentLengthLong < 0 || connection.contentLengthLong == end - start + 1))) {
                            part.delete(); meta.delete()
                            throw IOException("Mismatched update range response. Retry to download a fresh copy.")
                        }
                    } else offset = 0
                    require(total <= MAX_DOWNLOAD && total != 0L) { "Update download exceeds the safety limit" }
                    val etag = connection.getHeaderField("ETag")?.takeUnless { it.startsWith("W/") }
                    val validator = etag ?: connection.getHeaderField("Last-Modified") ?: ""
                    if (code == 206 && validator != saved.getProperty("validator")) {
                        part.delete(); meta.delete()
                        throw IOException("Update changed while downloading. Retry to download a fresh copy.")
                    }
                    AtomicConfigFile.writeText(meta, "url=$url\nvalidator=$validator\n")
                    var done = offset
                    connection.inputStream.use { input -> FileOutputStream(part, offset > 0).use { output ->
                        val buffer = ByteArray(128 * 1024)
                        while (true) {
                            if (cancelled()) throw CancellationException("Update cancelled")
                            val n = input.read(buffer)
                            if (n < 0) break
                            done += n
                            require(done <= MAX_DOWNLOAD && (total < 0 || done <= total)) { "Update download exceeded its declared size" }
                            output.write(buffer, 0, n)
                            progress(done, total)
                        }
                        output.fd.sync()
                    } }
                    require(done > 0 && (total < 0 || done == total)) { "Update download is incomplete. Retry to resume." }
                    Files.move(part.toPath(), file.toPath(), ATOMIC_MOVE)
                    meta.delete()
                    return
                } finally { connection.disconnect() }
            }
            throw IOException("Too many update redirects")
        }
    }
}
