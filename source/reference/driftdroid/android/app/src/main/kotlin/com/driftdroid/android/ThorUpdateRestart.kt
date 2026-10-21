package com.driftdroid.android

import android.content.Context
import android.content.Intent
import android.app.Activity
import android.app.ActivityManager
import android.app.AlarmManager
import android.app.PendingIntent
import android.os.Process
import java.io.IOException

/** An explicit user-approved update survives teardown and delayed Android alarm delivery. */
internal object ThorUpdateRestart {
    private fun preferences(context: Context) = context.getSharedPreferences("thor_update_restart", Context.MODE_PRIVATE)
    fun requested(context: Context): Boolean = preferences(context).getBoolean("requested", false)
    fun request(context: Context) {
        if (!preferences(context).edit().putBoolean("requested", true).commit())
            throw IOException("Could not save the update restart request")
    }
    fun clear(context: Context) {
        if (!preferences(context).edit().remove("requested").commit())
            throw IOException("Could not clear the update restart request")
        PendingIntent.getActivity(context, 7713, next(context),
            PendingIntent.FLAG_NO_CREATE or PendingIntent.FLAG_ONE_SHOT or PendingIntent.FLAG_IMMUTABLE)?.let {
            (context.getSystemService(Context.ALARM_SERVICE) as AlarmManager).cancel(it)
            it.cancel()
        }
    }
    private fun next(context: Context) = Intent(context, ModePickerActivity::class.java)
        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)

    fun restart(activity: Activity) {
        request(activity)
        launchHandoff(activity, null)
    }

    fun restartProduct(activity: Activity, product: String) = launchHandoff(activity, product)

    private fun launchHandoff(activity: Activity, product: String?) {
        val handoff = Intent(activity, ThorRestartActivity::class.java)
            .putExtra(ThorRestartActivity.EXTRA_OLD_PID, Process.myPid())
            .putExtra(ThorRestartActivity.EXTRA_UPDATING, product == null)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_MULTIPLE_TASK)
        if (product != null) handoff.putExtra(ModePickerActivity.EXTRA_RESTART_PRODUCT, product)
        // The separate-process Activity is placed on top while we are still foreground.
        // An alarm after process death has no permission to launch a background Activity.
        activity.startActivity(handoff)
        // The bridge already owns a different task. Remove only this obsolete game task;
        // its native Activity must never be restored alongside the fresh updater process.
        val oldTask = (activity.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager).appTasks
            .firstOrNull { it.taskInfo.taskId == activity.taskId }
        oldTask?.finishAndRemoveTask()
        Process.killProcess(Process.myPid())
    }
}
