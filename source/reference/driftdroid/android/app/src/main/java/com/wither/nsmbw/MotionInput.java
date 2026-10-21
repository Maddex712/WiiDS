package com.wither.nsmbw;

import android.app.Activity;
import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.SystemClock;
import android.view.Display;
import android.view.Surface;

/**
 * The phone as the remote's body. A phone held in landscape is held like a sideways Wii Remote, so
 * turning it like a steering wheel tilts the remote, and a sharp jolt in any direction is a shake.
 */
final class MotionInput implements SensorEventListener {
    interface Listener {
        void onMotion(float tilt, boolean shake);
    }

    private static final float LOW_PASS = 0.15f;          // share of each sample that enters gravity
    private static final float SHAKE_THRESHOLD = 13f;     // m/s^2 of motion beyond gravity
    private static final long SHAKE_HOLD_MS = 180;        // long enough for the game's 4-frame test
    private static final float TILT_DEAD_ZONE_DEG = 4f;   // a slightly crooked grip does nothing
    private static final float TILT_FULL_DEG = 30f;       // phone angle that is a full remote tilt
    private static final float MIN_IN_PLANE = 3f;         // m/s^2; less means the phone lies flat

    private final Activity activity;
    private final Listener listener;
    private final SensorManager sensors;
    private final Sensor accelerometer;
    private final float[] gravity = new float[3];
    private boolean haveGravity;
    private boolean tiltOn, shakeOn;
    private long shakeUntil;
    private float lastTilt;
    private boolean lastShake;

    MotionInput(Activity activity, Listener listener) {
        this.activity = activity;
        this.listener = listener;
        sensors = (SensorManager) activity.getSystemService(Context.SENSOR_SERVICE);
        accelerometer = sensors != null ? sensors.getDefaultSensor(Sensor.TYPE_ACCELEROMETER) : null;
    }

    void start(boolean tilt, boolean shake) {
        tiltOn = tilt;
        shakeOn = shake;
        haveGravity = false;
        if (accelerometer != null && (tilt || shake)) {
            sensors.registerListener(this, accelerometer, SensorManager.SENSOR_DELAY_GAME);
        }
    }

    void stop() {
        if (sensors != null) {
            sensors.unregisterListener(this);
        }
        report(0f, false);
    }

    @Override
    public void onSensorChanged(SensorEvent event) {
        final float[] v = event.values;
        if (!haveGravity) {
            System.arraycopy(v, 0, gravity, 0, 3);
            haveGravity = true;
        } else {
            for (int i = 0; i < 3; ++i) gravity[i] += (v[i] - gravity[i]) * LOW_PASS;
        }

        final long now = SystemClock.uptimeMillis();
        if (shakeOn) {
            final float lx = v[0] - gravity[0], ly = v[1] - gravity[1], lz = v[2] - gravity[2];
            if (lx * lx + ly * ly + lz * lz > SHAKE_THRESHOLD * SHAKE_THRESHOLD) {
                shakeUntil = now + SHAKE_HOLD_MS;
            }
        }
        final boolean shake = now < shakeUntil;

        float tilt = 0f;
        if (tiltOn) {
            // Gravity in screen coordinates (x right, y up) for the way the phone is turned.
            final float gx = gravity[0], gy = gravity[1];
            float sx = gx, sy = gy;
            final Display display = activity.getDisplay();
            switch (display != null ? display.getRotation() : Surface.ROTATION_90) {
                case Surface.ROTATION_90: sx = -gy; sy = gx; break;
                case Surface.ROTATION_180: sx = -gx; sy = -gy; break;
                case Surface.ROTATION_270: sx = gy; sy = -gx; break;
                default: break;
            }
            if (Math.hypot(sx, sy) >= MIN_IN_PLANE) {
                // Turning the phone clockwise moves "up" counter-clockwise on the screen.
                float degrees = (float) Math.toDegrees(Math.atan2(-sx, sy));
                final float magnitude = Math.max(0f, Math.abs(degrees) - TILT_DEAD_ZONE_DEG);
                tilt = Math.min(1f, magnitude / (TILT_FULL_DEG - TILT_DEAD_ZONE_DEG)) * Math.signum(degrees);
            }
        }
        report(tilt, shake);
    }

    private void report(float tilt, boolean shake) {
        if (Math.abs(tilt - lastTilt) < 0.01f && shake == lastShake) return;
        lastTilt = tilt;
        lastShake = shake;
        listener.onMotion(tilt, shake);
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {}
}
