package com.driftdroid.android

import android.app.Activity
import android.content.Intent
import android.os.Build
import android.os.VibrationAttributes
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.provider.Settings
import android.widget.Toast

/**
 * Rumble on the AYN Thor: its built-in controls have no motor of their own, so the games' rumble
 * plays on the device's vibrator (SDLControllerManager's BuiltInRumble, SDLGamepadRumble for
 * Sunshine/Eclipse, GalaxyActivity.triggerHaptic). Android's own Vibration setting turns all of it
 * off, and apps can't read that setting, so this page offers a test buzz played the same way.
 */
object RumbleSettings {
    private const val VIBRATION_SETTINGS = "android.settings.VIBRATION_SETTINGS"

    fun show(activity: Activity) {
        ThorDialogs.builder(activity)
            .setTitle("Rumble")
            .setMessage(
                "Mario Kart Wii, Retro Rewind, New Super Mario Bros. Wii, MSMBWii, Super Mario Sunshine, " +
                    "Eclipse and the Galaxy games rumble the Thor (or a controller with its own motors).\n\n" +
                    "If Test rumble does nothing, Android's Vibration setting is off: turn on Vibration " +
                    "(Settings, Sound & vibration, Vibration & haptics)."
            )
            .setNeutralButton("Vibration settings") { _, _ -> openVibrationSettings(activity) }
            .setNegativeButton("Close", null)
            .setPositiveButton("Test rumble", null)
            .create()
            .apply {
                setOnShowListener {
                    // Stays open, so the test can be tried again after changing the setting.
                    getButton(android.app.AlertDialog.BUTTON_POSITIVE).setOnClickListener { test(activity) }
                }
            }
            .show()
    }

    private fun test(activity: Activity) {
        val vibrator: Vibrator? = if (Build.VERSION.SDK_INT >= 31) {
            activity.getSystemService(VibratorManager::class.java)?.defaultVibrator
        } else {
            @Suppress("DEPRECATION")
            activity.getSystemService(Vibrator::class.java)
        }
        if (vibrator == null || !vibrator.hasVibrator()) {
            Toast.makeText(activity, "This device has no vibration motor.", Toast.LENGTH_SHORT).show()
            return
        }
        val buzz = VibrationEffect.createOneShot(400, 255)
        if (Build.VERSION.SDK_INT >= 33) {
            vibrator.vibrate(buzz, VibrationAttributes.createForUsage(VibrationAttributes.USAGE_MEDIA))
        } else {
            vibrator.vibrate(buzz)
        }
    }

    private fun openVibrationSettings(activity: Activity) {
        for (action in listOf(VIBRATION_SETTINGS, Settings.ACTION_SOUND_SETTINGS, Settings.ACTION_SETTINGS)) {
            if (runCatching { activity.startActivity(Intent(action)) }.isSuccess) return
        }
    }
}
