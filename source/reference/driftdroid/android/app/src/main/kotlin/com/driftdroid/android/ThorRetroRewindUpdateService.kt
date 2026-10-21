package com.driftdroid.android

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.system.Os
import android.system.OsConstants
import java.io.File
import java.io.IOException
import java.util.concurrent.CancellationException
import java.util.concurrent.atomic.AtomicBoolean

/** Explicit user actions only; never silently downloads on app launch or resumes an install. */
class ThorRetroRewindUpdateService : Service() {
    private val cancelled = AtomicBoolean(false)
    private val mainHandler = Handler(Looper.getMainLooper())
    @Volatile private var worker: Thread? = null
    private var lastNotification = 0L

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (!BuildConfig.THOR_BUILD) { stopSelf(startId); return START_NOT_STICKY }
        if (intent?.action == CANCEL) {
            cancelled.set(true)
            if (worker == null) stopSelf(startId)
            return START_NOT_STICKY
        }
        if (intent == null || worker != null) return START_NOT_STICKY
        val action = intent.action
        if (action !in setOf(CHECK, UPDATE, RECOVER)) { stopSelf(startId); return START_NOT_STICKY }
        val manager = getSystemService(NotificationManager::class.java)
        if (Build.VERSION.SDK_INT >= 26) manager.createNotificationChannel(
            NotificationChannel(CHANNEL, "Retro Rewind updates", NotificationManager.IMPORTANCE_LOW))
        val notification = notification("Preparing Retro Rewind update…")
        if (Build.VERSION.SDK_INT >= 29) startForeground(ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
        else startForeground(ID, notification)
        cancelled.set(false)
        ThorRetroRewindUpdateStatus.snapshot = ThorRetroRewindUpdateStatus.Snapshot(
            state = ThorRetroRewindUpdateStatus.State.CHECKING, running = true,
            message = if (action == CHECK) "Checking Retro Rewind releases…" else "Preparing the update…")
        val thread = Thread({
            var leased = false
            try {
                if (action != CHECK) {
                    check(ThorGameSession.tryBeginUpdate()) { "Close the game before installing an update." }
                    leased = true
                }
                val engine = ThorRetroRewindUpdater(
                    root = packRoot(this), cache = File(cacheDir, "thor-retro-rewind-updates"),
                    gameRetained = { ThorGameSession.hasRetainedRuntime() },
                    cancelled = cancelled::get,
                    syncDirectory = ::syncDirectory,
                    progress = { update ->
                        ThorRetroRewindUpdateStatus.snapshot = update
                        val now = System.nanoTime()
                        if (now - lastNotification >= 700_000_000L) {
                            lastNotification = now
                            try { manager.notify(ID, notification(update.message)) } catch (_: SecurityException) { }
                        }
                    },
                )
                when (action) {
                    CHECK -> engine.check()
                    RECOVER -> engine.recoverPending()
                    UPDATE -> engine.update()
                }
                // A pack just installed or updated: bake its lower-screen maps now, not only when
                // the launcher next resumes (it may already be showing). Incremental and idempotent.
                if (action == UPDATE && ThorRetroRewindUpdateStatus.snapshot.state == ThorRetroRewindUpdateStatus.State.COMPLETE)
                    com.driftdroid.android.rom.WiiDsArt.repairRetroRewind(applicationContext)
            } catch (failure: Throwable) {
                val old = ThorRetroRewindUpdateStatus.snapshot
                ThorRetroRewindUpdateStatus.snapshot = old.copy(
                    state = if (failure is CancellationException) ThorRetroRewindUpdateStatus.State.CANCELLED
                        else ThorRetroRewindUpdateStatus.State.ERROR,
                    message = if (failure.suppressed.isNotEmpty())
                        "Recovery needs another attempt. Keep the game closed and retry." else
                        failure.message ?: "The update could not finish. Try again.",
                    canUpdate = false, running = false,
                )
            } finally {
                val finishedWorker = Thread.currentThread()
                // onStartCommand also runs on the main looper. Keep the old worker registered
                // until its foreground notification and lease are released, so a new request
                // cannot start a worker whose notification this cleanup would then remove.
                mainHandler.post {
                    if (worker === finishedWorker) {
                        if (leased) ThorGameSession.endUpdate()
                        worker = null
                        stopForeground(STOP_FOREGROUND_REMOVE)
                        // Cancel and ignored duplicate requests advance Android's startId.
                        // The still-current worker owns all of them, so stop the latest start.
                        stopSelf()
                    }
                }
            }
        }, "ThorRetroRewindUpdater")
        worker = thread
        thread.start()
        return START_NOT_STICKY
    }

    /** Persist rename/journal directory entries before the engine advances the transaction. */
    private fun syncDirectory(directory: File) {
        val descriptor = Os.open(directory.absolutePath, OsConstants.O_RDONLY, 0)
        try {
            if (!OsConstants.S_ISDIR(Os.fstat(descriptor).st_mode))
                throw IOException("Update sync target is not a directory")
            Os.fsync(descriptor)
        } finally {
            Os.close(descriptor)
        }
    }

    private fun notification(message: String): Notification =
        if (Build.VERSION.SDK_INT >= 26) Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.mipmap.ic_launcher).setContentTitle("Retro Rewind")
            .setContentText(message).setOngoing(true).build()
        else @Suppress("DEPRECATION") Notification.Builder(this)
            .setSmallIcon(R.mipmap.ic_launcher).setContentTitle("Retro Rewind")
            .setContentText(message).setOngoing(true).build()

    companion object {
        private const val CHECK = "com.driftdroid.android.thor.RR_CHECK"
        private const val UPDATE = "com.driftdroid.android.thor.RR_UPDATE"
        private const val RECOVER = "com.driftdroid.android.thor.RR_RECOVER"
        private const val CANCEL = "com.driftdroid.android.thor.RR_CANCEL"
        private const val CHANNEL = "thor_retro_rewind_updates"
        private const val ID = 0x52520101

        private fun packRoot(context: Context) = File(context.filesDir, "WiiCompiled/RetroRewind6")
        fun needsRecovery(context: Context) = ThorRetroRewindUpdater.needsRecovery(packRoot(context))
        fun startCheck(context: Context) = start(context, CHECK)
        fun startUpdate(context: Context) = start(context, UPDATE)
        fun startRecovery(context: Context) = start(context, RECOVER)
        fun cancel(context: Context) {
            if (ThorRetroRewindUpdateStatus.snapshot.running)
                context.startService(Intent(context, ThorRetroRewindUpdateService::class.java).setAction(CANCEL))
        }
        private fun start(context: Context, action: String) {
            val intent = Intent(context, ThorRetroRewindUpdateService::class.java).setAction(action)
            if (Build.VERSION.SDK_INT >= 26) context.startForegroundService(intent) else context.startService(intent)
        }
    }
}
