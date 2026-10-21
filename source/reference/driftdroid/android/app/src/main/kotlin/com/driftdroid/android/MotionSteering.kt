package com.driftdroid.android

import android.content.Context
import android.content.SharedPreferences
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import com.driftdroid.android.touch.TouchInputBridge
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.hypot
import kotlin.math.sign

/**
 * Gravity-based tilt steering, matching KartPad's Android calibration curve (their
 * KartPadMotionSteering.kt) rather than a from-scratch guess - reported directly as "horrible" on
 * the first pass, which used atan2(x, z) (tilt toward/away from the face) instead of atan2(x, y)
 * (roll left/right around the phone's long axis, the actual "steering wheel" motion in landscape),
 * plus a crude linear response with no dead zone. This version uses the same axis pairing, dead
 * zone, and ramp-to-full-lock curve as KartPad's.
 *
 * Feeds the same virtual gamepad axis the touch stick already uses
 * (TouchInputBridge.AXIS_LEFT_X), mutually exclusive with it via
 * TouchControlsOverlay.setMotionSteeringActive.
 */
internal class MotionSteering(context: Context) : SensorEventListener {
    private val applicationContext = context.applicationContext
    private val sensorManager = context.getSystemService(SensorManager::class.java)
    private val sensor: Sensor? =
        sensorManager?.getDefaultSensor(Sensor.TYPE_GRAVITY)
            ?: sensorManager?.getDefaultSensor(Sensor.TYPE_ACCELEROMETER)
    private val prefs: SharedPreferences =
        applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    private var registered = false
    private var calibrated = false
    private var lastAngle = Double.NaN
    private var centerAngle = 0.0

    val sensorAvailable: Boolean get() = sensor != null

    var enabled: Boolean
        get() = prefs.getBoolean(KEY_ENABLED, false)
        set(value) {
            prefs.edit().putBoolean(KEY_ENABLED, value).apply()
        }

    var inverted: Boolean
        get() = prefs.getBoolean(KEY_INVERTED, false)
        set(value) {
            prefs.edit().putBoolean(KEY_INVERTED, value).apply()
            publishCurrentAngle()
        }

    var sensitivity: Float
        get() = prefs.getFloat(KEY_SENSITIVITY, 1f)
        set(value) {
            prefs.edit().putFloat(KEY_SENSITIVITY, value.coerceIn(0.5f, 2f)).apply()
            publishCurrentAngle()
        }

    fun start() {
        if (registered || sensor == null) return
        calibrated = false
        TouchInputBridge.nativeSetTouchAxis(TouchInputBridge.AXIS_LEFT_X, 0f)
        registered = sensorManager?.registerListener(this, sensor, SensorManager.SENSOR_DELAY_GAME) ?: false
    }

    fun stop() {
        if (!registered) return
        sensorManager?.unregisterListener(this)
        registered = false
        calibrated = false
        TouchInputBridge.nativeSetTouchAxis(TouchInputBridge.AXIS_LEFT_X, 0f)
    }

    /** Re-centers on however the phone is currently held, without needing to toggle steering off
     * and back on - useful mid-race if your resting grip has drifted. */
    fun recenter() {
        if (!lastAngle.isFinite()) return
        centerAngle = lastAngle
        calibrated = true
        publishCurrentAngle()
    }

    override fun onSensorChanged(event: SensorEvent) {
        val x = event.values.getOrElse(0) { 0f }
        val y = event.values.getOrElse(1) { 0f }
        // Ignore near-zero-magnitude readings (e.g. during free-fall or a sensor glitch) rather
        // than let atan2 produce a noisy angle from almost-nothing.
        if (hypot(x.toDouble(), y.toDouble()) < 0.08) return
        lastAngle = atan2(y.toDouble(), x.toDouble())
        if (!calibrated) {
            centerAngle = lastAngle
            calibrated = true
        }
        publishCurrentAngle()
    }

    override fun onAccuracyChanged(sensor: Sensor, accuracy: Int) {}

    private fun publishCurrentAngle() {
        if (!enabled || !calibrated || !lastAngle.isFinite()) {
            TouchInputBridge.nativeSetTouchAxis(TouchInputBridge.AXIS_LEFT_X, 0f)
            return
        }
        val value = steeringValue(lastAngle, centerAngle, sensitivity, inverted)
        TouchInputBridge.nativeSetTouchAxis(TouchInputBridge.AXIS_LEFT_X, value)
    }

    companion object {
        private const val PREFS_NAME = "motion_steering"
        private const val KEY_ENABLED = "enabled"
        private const val KEY_INVERTED = "inverted"
        private const val KEY_SENSITIVITY = "sensitivity"
        private const val DEAD_ZONE = 0.045

        internal fun steeringValue(angle: Double, center: Double, sensitivity: Float, inverted: Boolean): Float {
            if (!angle.isFinite() || !center.isFinite()) return 0f
            val boundedSensitivity = sensitivity.coerceIn(0.5f, 2f)
            var delta = angle - center
            while (delta > PI) delta -= 2.0 * PI
            while (delta < -PI) delta += 2.0 * PI
            val magnitude = abs(delta)
            if (magnitude <= DEAD_ZONE) return 0f
            val fullLock = 0.70 / boundedSensitivity
            var value = ((magnitude - DEAD_ZONE) / (fullLock - DEAD_ZONE)).coerceAtMost(1.0) * delta.sign
            if (inverted) value = -value
            return value.toFloat()
        }
    }
}
