package com.driftdroid.android

import android.app.Activity
import android.app.AlertDialog
import android.app.DownloadManager
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageInstaller
import android.graphics.Color
import android.graphics.drawable.BitmapDrawable
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView
import com.driftdroid.android.rom.EclipseMod
import com.driftdroid.android.rom.GameInstaller
import com.driftdroid.android.rom.MsmbwiiMod
import java.io.File
import java.util.UUID

/** Browser-free WiiDS updates. Android still owns and confirms installation. */
class WiiDsUpdateActivity : Activity() {
    private lateinit var status: TextView
    private lateinit var primary: Button
    private lateinit var cancel: Button
    private lateinit var back: Button
    private lateinit var progress: ProgressBar
    private val handler = Handler(Looper.getMainLooper())
    private var release: WiiDsUpdateRelease.Release? = null
    private var checking = false
    private var preparing = false
    private var resumed = false
    private var permissionPending = false
    private var terminalNotice: String? = null
    private val sessionLock = Any()
    @Volatile private var cancelled = false
    private var receiver: BroadcastReceiver? = null
    private var companion: ThorLowerCompanion? = null
    private var art: ThorMenuArtwork? = null
    private val poll = object : Runnable {
        override fun run() { if (resumed) { refreshDownload(); handler.postDelayed(this, 750) } }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (!BuildConfig.THOR_BUILD || packageName != "io.github.maddex712.wiids") { finish(); return }
        val folder = File(filesDir, "WiiCompiled/ThorMap")
        art = runCatching { ThorMenuArtwork(folder) }.getOrNull()
        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; setPadding(dp(30), dp(20), dp(30), dp(20)); setBackgroundColor(Color.rgb(238, 243, 247))
            runCatching { org.json.JSONObject(File(folder, "menu.json").readText()).getJSONObject("images").getJSONObject("background").getString("texture") }
                .getOrNull()?.let { name -> art?.bitmap(name)?.let { background = BitmapDrawable(resources, it) } }
        }
        fun label(text: String, size: Float) = TextView(this).apply {
            this.text = styled(text); textSize = size; setTextColor(Color.rgb(35, 48, 62)); setPadding(0, dp(5), 0, dp(5))
        }
        layout.addView(label("WiiDS updates", 28f))
        layout.addView(label("Installed: ${WiiDsUpdater.installedName(this)} (build ${WiiDsUpdater.installedCode(this)})", 15f))
        layout.addView(label("Download directly from the WiiDS GitHub. Android will ask before installing over this app. Your games and saves stay in place.", 16f))
        status = label("Ready to check for updates.", 18f).apply { minLines = 3 }
        layout.addView(status)
        progress = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply { max = 1000; visibility = View.GONE }
        layout.addView(progress, LinearLayout.LayoutParams(-1, dp(22)))
        fun button(text: String, action: () -> Unit) = Button(this).apply {
            this.text = styled(text); textSize = 18f; isAllCaps = false; minHeight = dp(56); isFocusableInTouchMode = true
            setOnTouchListener { view, event -> if (event.actionMasked == MotionEvent.ACTION_DOWN) view.requestFocus(); false }
            setOnClickListener { action() }; layout.addView(this, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(5) })
        }
        primary = button("Check for updates") { primaryAction() }
        cancel = button("Cancel download") { runCatching { WiiDsUpdater.cancel(this) }.onSuccess { terminalNotice = null; release = null; say("Update download cancelled."); idle() }.onFailure { fail(it) } }
        cancel.visibility = View.GONE
        back = button("Back to library") { finish() }
        layout.addView(label("A  Select     B  Back     D-pad  Move", 13f))
        setContentView(ScrollView(this).apply { addView(layout) })
        companion = ThorLowerCompanion(this, folder, "WiiDS updates")
        primary.requestFocus()
        // A killed installer preparation must not strand an unfinished session or lock games.
        if (!WiiDsUpdater.installing.get()) {
            val old = WiiDsUpdater.session(this)
            if (old >= 0) runCatching { packageManager.packageInstaller.abandonSession(old) }
            WiiDsUpdater.session(this, -1)
        }
        if (WiiDsUpdater.pending(this) == null) checkLatest()
    }

    private fun dp(n: Int) = (n * resources.displayMetrics.density).toInt()
    private fun styled(text: String): CharSequence = art?.styledText(text) ?: text
    private fun say(text: String) { status.text = styled(text); companion?.setStatus("WiiDS updates", text) }
    private fun text(button: Button, value: String) { button.text = styled(value) }
    private fun idle() {
        progress.visibility = View.GONE; primary.isEnabled = true; back.isEnabled = true
        cancel.visibility = View.GONE; text(primary, if (release == null) "Check for updates" else "Download update")
    }
    private fun fail(error: Throwable) {
        terminalNotice = error.message?.take(240) ?: "The update could not finish. Please try again."
        say(terminalNotice!!)
        if (!preparing) {
            idle()
            if (WiiDsUpdater.pending(this) != null) { text(primary, "Install update"); cancel.visibility = View.VISIBLE }
        }
    }

    private fun checkLatest() {
        if (checking || preparing || WiiDsUpdater.pending(this) != null) return
        checking = true; release = null; primary.isEnabled = false; progress.visibility = View.VISIBLE; progress.isIndeterminate = true
        say("Checking the latest WiiDS release...")
        Thread {
            val result = runCatching { WiiDsUpdateRelease.latest() }
            handler.post {
                checking = false
                if (isFinishing || isDestroyed) return@post
                result.onSuccess {
                    if (it.newerThan(WiiDsUpdater.installedCode(this))) {
                        release = it
                        say("WiiDS ${it.version} (build ${it.versionCode}) is available. Download size: ${it.apk.bytes / 1_048_576} MB.")
                    } else say("You're up to date. This copy already has the latest WiiDS build.")
                    idle()
                }.onFailure { fail(it) }
            }
        }.apply { name = "WiiDsReleaseCheck"; isDaemon = true }.start()
    }

    private fun primaryAction() {
        terminalNotice = null
        val pending = WiiDsUpdater.pending(this)
        if (pending != null) { if (WiiDsUpdater.progress(this, pending)?.status == DownloadManager.STATUS_SUCCESSFUL) requestInstall(); return }
        val next = release ?: run { checkLatest(); return }
        runCatching { WiiDsUpdater.begin(this, next) }.onSuccess { refreshDownload() }.onFailure { fail(it) }
    }

    private fun refreshDownload() {
        if (preparing || checking) return
        terminalNotice?.let { say(it); return }
        val pending = WiiDsUpdater.pending(this) ?: return
        if (!pending.release.newerThan(WiiDsUpdater.installedCode(this))) {
            runCatching { WiiDsUpdater.cancel(this) }; release = null; say("The update is installed. You're up to date."); idle(); return
        }
        val state = runCatching { WiiDsUpdater.progress(this, pending) }.getOrElse { say("Could not read download progress. Reopen this screen to try again."); return }
        cancel.visibility = View.VISIBLE; primary.isEnabled = false; progress.visibility = View.VISIBLE
        when (state?.status) {
            DownloadManager.STATUS_SUCCESSFUL -> {
                progress.visibility = View.GONE; primary.isEnabled = true; text(primary, "Install update")
                say("WiiDS ${pending.release.version} is downloaded. Close games, then choose Install update. Its checksum and signing key will be checked before Android asks you to confirm.")
            }
            DownloadManager.STATUS_FAILED, null -> {
                progress.visibility = View.GONE; text(primary, "Check for updates")
                val message = if (state?.reason == DownloadManager.ERROR_INSUFFICIENT_SPACE) "Not enough storage for the update." else "The download did not finish. Check your connection and free space."
                say("$message Cancel this download, then try again.")
            }
            else -> {
                val done = state.downloaded.coerceAtLeast(0); val total = pending.release.apk.bytes
                progress.isIndeterminate = state.status == DownloadManager.STATUS_PENDING
                progress.progress = ((done * 1000 / total).coerceIn(0, 1000)).toInt()
                text(primary, "Downloading update")
                say(if (state.status == DownloadManager.STATUS_PAUSED) "Download paused by Android. It will continue when the connection is ready. You can return to this screen later."
                    else "Downloading: ${done / 1_048_576} of ${total / 1_048_576} MB. You can leave this screen and return when it finishes.")
            }
        }
    }

    private fun requestInstall() {
        if (preparing) return
        if (!packageManager.canRequestPackageInstalls()) {
            ThorDialogs.builder(this).setTitle("Allow WiiDS updates")
                .setMessage("Android needs permission for WiiDS to install its updates. Enable Allow from this source, then return here.")
                .setPositiveButton("Open Android settings") { _, _ ->
                    permissionPending = true
                    runCatching { startActivity(Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:$packageName"))) }
                        .onFailure { permissionPending = false; fail(it) }
                }.setNegativeButton("Cancel", null).show()
            return
        }
        val pending = WiiDsUpdater.pending(this) ?: return
        runCatching {
            synchronized(GameInstaller) {
                SaveManagerProfiles.blocker(this)?.let { error(it) }
                SaveManagerProfiles.requireStopped(this)
                check(!GameInstaller.poll().running && !EclipseMod.state.running && !MsmbwiiMod.state.running && !RetroRewindInstallStatus.running) { "Finish the game import before installing an update." }
                check(WiiDsUpdater.installing.compareAndSet(false, true)) { "An update is already being prepared." }
                if (!ThorGameSession.tryBeginUpdate()) { WiiDsUpdater.installing.set(false); error("Close the game and restart WiiDS before installing the update.") }
            }
        }.onFailure { fail(it); return }
        preparing = true; cancelled = false; primary.isEnabled = false; cancel.visibility = View.GONE; back.isEnabled = false
        say("Checking the update and preparing Android's installer..."); progress.visibility = View.VISIBLE; progress.isIndeterminate = true
        val action = "$packageName.UPDATE_INSTALL.${UUID.randomUUID()}"
        receiver = object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) {
                if (!preparing || intent.action != action || intent.getIntExtra(PackageInstaller.EXTRA_SESSION_ID, -1) != WiiDsUpdater.session(context)) return
                when (intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE)) {
                    PackageInstaller.STATUS_PENDING_USER_ACTION -> {
                        val confirm = if (Build.VERSION.SDK_INT >= 33) intent.getParcelableExtra(Intent.EXTRA_INTENT, Intent::class.java)
                            else @Suppress("DEPRECATION") intent.getParcelableExtra(Intent.EXTRA_INTENT)
                        if (confirm == null) finishInstall("Android's installation confirmation was unavailable.")
                        else runCatching { startActivity(confirm) }.onFailure { finishInstall("Could not open Android's installer. Please try again.") }
                    }
                    PackageInstaller.STATUS_SUCCESS -> finishInstall("Update installed. Reopen WiiDS.")
                    PackageInstaller.STATUS_FAILURE_ABORTED -> finishInstall("Installation cancelled. Your current WiiDS is unchanged.")
                    else -> finishInstall("Android could not install the update. Check free space and try again.")
                }
            }
        }
        if (Build.VERSION.SDK_INT >= 33) registerReceiver(receiver, IntentFilter(action), RECEIVER_NOT_EXPORTED)
        else @Suppress("DEPRECATION") registerReceiver(receiver, IntentFilter(action))
        val app = applicationContext
        Thread {
            val installer = app.packageManager.packageInstaller; var id = -1
            try {
                check(WiiDsUpdater.progress(app, pending)?.status == DownloadManager.STATUS_SUCCESSFUL) { "The download is not complete." }
                val apk = WiiDsUpdater.file(app, pending.release)
                check(apk.isFile && !java.nio.file.Files.isSymbolicLink(apk.toPath()) && apk.length() == pending.release.apk.bytes) { "The downloaded file is incomplete." }
                WiiDsUpdater.verifyPackage(app, apk, pending.release)
                val params = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL).apply {
                    setAppPackageName(app.packageName); setSize(apk.length())
                    if (Build.VERSION.SDK_INT >= 31) setRequireUserAction(PackageInstaller.SessionParams.USER_ACTION_REQUIRED)
                    if (Build.VERSION.SDK_INT >= 33) setPackageSource(PackageInstaller.PACKAGE_SOURCE_DOWNLOADED_FILE)
                }
                synchronized(sessionLock) {
                    check(!cancelled) { "Update cancelled." }
                    id = installer.createSession(params); WiiDsUpdater.session(app, id)
                }
                installer.openSession(id).use { session ->
                    session.openWrite("WiiDS.apk", 0, apk.length()).use { output ->
                        apk.inputStream().use { input -> WiiDsUpdateRelease.copyVerified(input, output, pending.release.apk.bytes, pending.release.apk.sha256, { cancelled }) }
                        session.fsync(output)
                    }
                    check(!cancelled) { "Update cancelled." }
                    handler.post { if (!isDestroyed) say("Update verified. Confirm the Android installation, then reopen WiiDS.") }
                    val callback = Intent(action).setPackage(app.packageName)
                    val flags = PendingIntent.FLAG_UPDATE_CURRENT or if (Build.VERSION.SDK_INT >= 31) PendingIntent.FLAG_MUTABLE else 0
                    synchronized(sessionLock) {
                        check(!cancelled) { "Update cancelled." }
                        session.commit(PendingIntent.getBroadcast(app, id, callback, flags).intentSender)
                    }
                }
            } catch (error: Exception) {
                if (id >= 0) runCatching { installer.abandonSession(id) }
                handler.post { finishInstall(error.message ?: "The update could not be prepared.") }
            }
        }.apply { name = "WiiDsInstallUpdate"; isDaemon = true }.start()
    }

    private fun finishInstall(message: String) {
        if (!preparing) return
        val id = WiiDsUpdater.session(this)
        if (id >= 0) runCatching { packageManager.packageInstaller.abandonSession(id) }
        WiiDsUpdater.session(this, -1); WiiDsUpdater.installing.set(false); ThorGameSession.endUpdate(); preparing = false
        receiver?.let { runCatching { unregisterReceiver(it) } }; receiver = null
        terminalNotice = message
        if (!isDestroyed) { idle(); text(primary, "Install update"); cancel.visibility = View.VISIBLE; say(message) }
    }

    override fun onResume() {
        super.onResume(); resumed = true; companion?.start(); handler.post(poll)
        window.addFlags(android.view.WindowManager.LayoutParams.FLAG_FULLSCREEN)
        if (Build.VERSION.SDK_INT >= 30) { window.setDecorFitsSystemWindows(false); window.insetsController?.hide(android.view.WindowInsets.Type.systemBars()) }
        if (permissionPending) { permissionPending = false; if (packageManager.canRequestPackageInstalls()) requestInstall() }
    }
    override fun onPause() { resumed = false; handler.removeCallbacks(poll); companion?.stop(); super.onPause() }
    override fun onDestroy() {
        synchronized(sessionLock) {
            cancelled = true
            if (preparing) finishInstall("Update preparation cancelled. Reopen WiiDS updates to try again.")
        }
        receiver?.let { runCatching { unregisterReceiver(it) } }; receiver = null
        companion?.stop(); super.onDestroy()
    }
    @Deprecated("Android compatibility") override fun onBackPressed() { if (!preparing) finish() }
    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        if (keyCode == KeyEvent.KEYCODE_BUTTON_B && event.repeatCount == 0) { if (!preparing) finish(); return true }
        if (keyCode == KeyEvent.KEYCODE_BUTTON_A && event.repeatCount == 0) { currentFocus?.takeIf { it.isEnabled }?.performClick(); return true }
        return super.onKeyDown(keyCode, event)
    }
}
